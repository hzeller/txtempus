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

#ifndef TIMETRANSMITTER_CLOCKGEN_H
#define TIMETRANSMITTER_CLOCKGEN_H

#include <cstdint>
#include <ctime>
#include <vector>

#include "carrier-power.h"
#include "dcf77-weather.h"

struct ModulationDuration {
  CarrierPower power;
  int duration_ms;
};

// Base class for different types of time signal sources.
class TimeSignalSource {
 public:
  using SecondModulation = std::vector<ModulationDuration>;

  virtual ~TimeSignalSource() = default;

  // Carrier frequency of this particular time source.
  virtual int GetCarrierFrequencyHz() const = 0;

  // Called once at the beginning of a minute starting with
  // the transmission to prepare the necessary data bits to be
  // sent.
  // Note, some time signals are sent to be valid when the
  // end of the minute is reached, so these implementations would need to
  // add 60 seconds to this.
  // The provided time is guaranteed to be an even minute, i.e. divisible by 60.
  virtual void PrepareMinute(time_t t) = 0;

  // Returns a vector of modulation transitions to be sent out for the
  // particular second within the minute mentioned in PrepareMinute().
  // The method should return a sequence of power-levels and durations in
  // milliseconds. The last transition stays for the remainder of the second,
  // so it is good practice to set the last duration to zero to auto-fill.
  // e.g. {{CarrierPower::HIGH, 200},{CarrierPower::LOW, 0}}
  //
  // All numbers must add up to less or equal 1000ms.
  //
  // Value of second can be between 0..59, or up to 60 with leap seconds
  // (leap seconds not implemented yet).
  virtual SecondModulation GetModulationForSecond(int second) = 0;
};

// -- Various implementations.

class DCF77TimeSignalSource : public TimeSignalSource {
 public:
  int GetCarrierFrequencyHz() const final { return 77500; }
  void PrepareMinute(time_t t) override;
  SecondModulation GetModulationForSecond(int second) override;

 protected:
  uint64_t time_bits_ = 0;
};

// DCF77 with Meteotime weather data (per-region, double-buffered)
class DCF77WeatherTimeSignalSource : public DCF77TimeSignalSource {
 public:
  void PrepareMinute(time_t t) override;

  // Set weather for a specific region and forecast day (staged, applies next cycle)
  // forecast_day: 0=today, 1=tomorrow, 2=day+2, 3=day+3
  void SetRegionWeather(int region, int forecast_day, const RegionWeather& weather);

  // Set weather for a specific region, all forecast days (staged)
  void SetRegionWeather(int region, const RegionWeather& weather);

  // Set same weather for all 90 regions (staged)
  void SetAllRegionsWeather(const RegionWeather& weather);

  // Reset all region weather (staged)
  void ResetAllWeather();

  // Set default weather for unconfigured regions
  void SetDefaultWeather(const RegionWeather& weather);

  void SetVerbose(bool v) { verbose_ = v; }

  const RegionWeatherStore& GetWeatherStore() const { return weather_store_; }
  RegionWeatherStore& GetWeatherStore() { return weather_store_; }

 private:
  RegionWeatherStore weather_store_;
  uint64_t weather_cipher_ = 0;
  int chunk_index_ = 0;
  bool verbose_ = false;
};

class WWVBTimeSignalSource : public TimeSignalSource {
 public:
  int GetCarrierFrequencyHz() const final { return 60000; }
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

 private:
  uint64_t time_bits_;
};

class JJYTimeSignalSource : public TimeSignalSource {
 public:
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

 private:
  uint64_t time_bits_;
};

class JJY60TimeSignalSource : public JJYTimeSignalSource {
  int GetCarrierFrequencyHz() const final { return 60000; }
};
class JJY40TimeSignalSource : public JJYTimeSignalSource {
  int GetCarrierFrequencyHz() const final { return 40000; }
};

class MSFTimeSignalSource : public TimeSignalSource {
 public:
  int GetCarrierFrequencyHz() const final { return 60000; }
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

 private:
  uint64_t a_bits_, b_bits_;
};

#endif  // TIMETRANSMITTER_CLOCKGEN_H
