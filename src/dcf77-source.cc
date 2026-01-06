// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// Copyright (C) 2018 Henner Zeller <h.zeller@acm.org>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <cstdint>
#include <cstdio>
#include <ctime>

#include "carrier-power.h"
#include "dcf77-weather.h"
#include "time-signal-source.h"

// Helper to convert a number to BCD (Binary Coded Decimal).
static uint64_t to_bcd(uint8_t n) { return (((n / 10) % 10) << 4) | (n % 10); }

// Calculate even parity for a range of bits.
static uint64_t parity(uint64_t d, uint8_t from, uint8_t to_including) {
  uint8_t result = 0;
  for (int bit = from; bit <= to_including; ++bit) {
    if (d & (1LL << bit)) result++;
  }
  return result & 0x1;
}

// Prepare the 59-bit time code for the next minute.
// DCF77 transmits the time for the *upcoming* minute, so we add 60 seconds.
void DCF77TimeSignalSource::PrepareMinute(time_t t) {
  t += 60;  // We're sending the _upcoming_ minute.
  struct tm breakdown;
  localtime_r(&t, &breakdown);

  // https://de.wikipedia.org/wiki/DCF77
  // Little endian bits. So we store big-endian bits and start transmitting
  // from bit 0
  time_bits_ = 0;
  time_bits_ |= (breakdown.tm_isdst ? 1 : 0) << 17;
  time_bits_ |= (breakdown.tm_isdst ? 0 : 1) << 18;
  time_bits_ |= (1 << 20);  // start time bit.
  time_bits_ |= to_bcd(breakdown.tm_min) << 21;
  time_bits_ |= to_bcd(breakdown.tm_hour) << 29;
  time_bits_ |= to_bcd(breakdown.tm_mday) << 36;
  time_bits_ |= to_bcd(breakdown.tm_wday ? breakdown.tm_wday : 7) << 42;
  time_bits_ |= to_bcd(breakdown.tm_mon + 1) << 45;
  time_bits_ |= to_bcd(breakdown.tm_year % 100) << 50;

  time_bits_ |= parity(time_bits_, 21, 27) << 28;
  time_bits_ |= parity(time_bits_, 29, 34) << 35;
  time_bits_ |= parity(time_bits_, 36, 57) << 58;
}

// Get the amplitude modulation pattern for one second.
// Each bit is encoded as either 100ms (0) or 200ms (1) of reduced carrier.
TimeSignalSource::SecondModulation
DCF77TimeSignalSource::GetModulationForSecond(int second) {
  if (second >= 59) return {{CarrierPower::HIGH, 0}};  // Synchronization
  const bool bit = time_bits_ & (1LL << second);
  return {{CarrierPower::LOW, bit ? 200 : 100}, {CarrierPower::HIGH, 0}};
}

// DCF77 with Meteotime weather data.
// This extends the base DCF77 source by injecting encrypted weather data
// into bits 1-14 (the "spare" bits not used for time encoding).
void DCF77WeatherTimeSignalSource::PrepareMinute(time_t t) {
  DCF77TimeSignalSource::PrepareMinute(t);

  t += 60;
  struct tm breakdown;
  localtime_r(&t, &breakdown);

  int local_minute = breakdown.tm_min;
  int local_hour = breakdown.tm_hour;

  int german_utc_hour = local_hour - 1;
  if (breakdown.tm_isdst) german_utc_hour--;
  if (german_utc_hour < 0) german_utc_hour += 24;

  int current_region = GetRegionForTime(german_utc_hour, local_minute);
  int next_minute = (local_minute + 3) % 60;
  int next_hour = german_utc_hour;
  if (next_minute < local_minute) next_hour = (next_hour + 1) % 24;
  int next_region = GetRegionForTime(next_hour, next_minute);

  int chunk_index = local_minute % 3;
  if (chunk_index == 0) {
    // Start of new 3-minute cycle - apply any pending weather changes safely.
    // This ensures atomic updates that won't corrupt mid-cycle transmissions.
    weather_store_.ApplyPending();

    weather_cipher_ = generate_weather_cipher(t - 60, local_minute, local_hour,
                                               german_utc_hour, weather_store_);
    chunk_index_ = 0;

    if (verbose_) {
      int dataset_type = GetDatasetType(german_utc_hour, local_minute);
      int forecast_day = GetForecastDayFromDatasetType(dataset_type);
      const RegionWeather& rw = weather_store_.GetRegion(current_region, forecast_day);
      const char* day_names[] = {"today", "tomorrow", "day+2", "day+3"};
      fprintf(stderr,
              "\n[Weather] Region %d: %s (forecast: %s)\n"
              "          Day: %s, Night: %s, Temp: %d/%d°C\n",
              current_region, region_names[current_region], day_names[forecast_day],
              weather_day_names[rw.weather_day & 0xf],
              weather_night_names[rw.weather_night & 0xf], rw.temperature_day,
              rw.temperature_night);
      if ((dataset_type & 1) == 0) {
        fprintf(stderr, "          Extreme: %s, Rain: %d%%\n",
                extreme_weather_names[rw.extreme & 0xf], rw.rain_probability);
      } else {
        fprintf(stderr, "          Wind: %s %s Bft\n",
                wind_direction_names[rw.wind_direction & 0xf],
                wind_strength_names[rw.wind_strength < 8 ? rw.wind_strength : 7]);
      }
      fprintf(stderr, "          Next: Region %d (%s)\n", next_region,
              region_names[next_region]);
      if (weather_store_.HasPending()) {
        fprintf(stderr, "          (Changes pending for next cycle)\n");
      }
    }
  }

  uint16_t chunk = get_weather_chunk(weather_cipher_, chunk_index_++);

  // Insert weather chunk into bits 1-14 by masking and OR-ing.
  // Bit 0 is always 0, and bits 15-58 contain the actual time data.
  time_bits_ &= ~(0x7FFEULL);
  time_bits_ |= (static_cast<uint64_t>(chunk) & 0x7FFE);
}

void DCF77WeatherTimeSignalSource::SetRegionWeather(int region, int forecast_day,
                                                     const RegionWeather& w) {
  weather_store_.SetRegion(region, forecast_day, w);
}

void DCF77WeatherTimeSignalSource::SetRegionWeather(int region,
                                                     const RegionWeather& w) {
  weather_store_.SetRegion(region, w);
}

void DCF77WeatherTimeSignalSource::SetAllRegionsWeather(
    const RegionWeather& w) {
  weather_store_.SetAllRegions(w);
}

void DCF77WeatherTimeSignalSource::ResetAllWeather() {
  weather_store_.ResetPending();
}

void DCF77WeatherTimeSignalSource::SetDefaultWeather(const RegionWeather& w) {
  weather_store_.SetDefault(w);
}
