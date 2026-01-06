// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// DCF77 Meteotime weather data generation
//
// Ported from osmocom-analog project:
// https://github.com/osmocom/osmocom-analog
// Original DCF77 weather implementation (C) 2022 Andreas Eversberg <jolly@eversberg.eu>
//
// Weather encryption based on code from:
// https://github.com/FroggySoft/AlarmClock
// https://github.com/tobozo/esp32-dcf77-weatherman

#include "dcf77-weather.h"

#include <cstdlib>

#include "dcf77-weather-crypt.h"

// Weather descriptions from osmocom-analog
const char* weather_day_names[16] = {
    "Reserved",      "Sunny",       "Partly clouded", "Mostly clouded",
    "Overcast",      "High fog",    "Fog",            "Showers",
    "Light rain",    "Heavy rain",  "Frontal storms", "Heat storms",
    "Sleet showers", "Snow showers", "Sleet",          "Snow"};

const char* weather_night_names[16] = {
    "Reserved",      "Clear",       "Partly clouded", "Mostly clouded",
    "Overcast",      "High fog",    "Fog",            "Showers",
    "Light rain",    "Heavy rain",  "Frontal storms", "Heat storms",
    "Sleet showers", "Snow showers", "Sleet",          "Snow"};

const char* extreme_weather_names[16] = {
    "None",        "Heavy Weather 24hrs", "Heavy weather Day",
    "Heavy weather Night", "Storm 24hrs", "Storm Day",
    "Storm Night", "Wind gusts Day",      "Wind gusts Night",
    "Icy rain morning",    "Icy rain afternoon",  "Icy rain night",
    "Fine dust",   "Ozon",                "Radiation",
    "High water"};

const char* wind_direction_names[16] = {
    "North",     "Northeast", "East",       "Southeast",
    "South",     "Southwest", "West",       "Northwest",
    "Changeable", "Foen",      "Biese N/O",  "Mistral N",
    "Scirocco S", "Tramont W", "reserved",   "reserved"};

const char* wind_strength_names[8] = {"0",   "0-2", "3-4", "5-6",
                                       "7",   "8",   "9",   ">=10"};

const char* rain_probability_names[8] = {"0%",  "15%", "30%", "45%",
                                          "60%", "75%", "90%", "100%"};

// Meteotime region names - 90 regions across Europe.
// Each region broadcasts its own weather data in a rotating 3-minute cycle.
const char* region_names[90] = {
    "F - Bordeaux",
    "F - La Rochelle",
    "F - Paris",
    "F - Brest",
    "F - Clermont-Ferrand",
    "F - Beziers",
    "B - Bruxelles",
    "F - Dijon",
    "F - Marseille",
    "F - Lyon",
    "F - Grenoble",
    "CH - La Chaux de Fond",
    "D - Frankfurt am Main",
    "D - Trier",
    "D - Duisburg",
    "GB - Swansea",
    "GB - Manchester",
    "F - le Havre",
    "GB - London",
    "D - Bremerhaven",
    "DK - Herning",
    "DK - Arhus",
    "D - Hannover",
    "DK - Kobenhavn",
    "D - Rostock",
    "D - Ingolstadt",
    "D - Muenchen",
    "I - Bolzano",
    "D - Nuernberg",
    "D - Leipzig",
    "D - Erfurt",
    "CH - Lausanne",
    "CH - Zuerich",
    "CH - Adelboden",
    "CH - Sion",
    "CH - Glarus",
    "CH - Davos",
    "D - Kassel",
    "CH - Locarno",
    "I - Sestriere",
    "I - Milano",
    "I - Roma",
    "NL - Amsterdam",
    "I - Genova",
    "I - Venezia",
    "F - Strasbourg",
    "A - Klagenfurt",
    "A - Innsbruck",
    "A - Salzburg",
    "SK - Wien/Bratislava",
    "CZ - Praha",
    "CZ - Decin",
    "D - Berlin",
    "S - Goeteborg",
    "S - Stockholm",
    "S - Kalmar",
    "S - Joenkoeping",
    "D - Donaueschingen",
    "N - Oslo",
    "D - Stuttgart",
    "I - Napoli",
    "I - Ancona",
    "I - Bari",
    "HU - Budapest",
    "E - Madrid",
    "E - Bilbao",
    "I - Palermo",
    "E - Palma de Mallorca",
    "E - Valencia",
    "E - Barcelona",
    "AND - Andorra",
    "E - Sevilla",
    "P - Lissabon",
    "I - Sassari",
    "E - Gijon",
    "IRL - Galway",
    "IRL - Dublin",
    "GB - Glasgow",
    "N - Stavanger",
    "N - Trondheim",
    "S - Sundsvall",
    "PL - Gdansk",
    "PL - Warszawa",
    "PL - Krakow",
    "S - Umea",
    "S - Oestersund",
    "CH - Samedan",
    "CR - Zagreb",
    "CH - Zermatt",
    "CR - Split",
};

// Pack weather data into a 23-bit value for encryption.
// The format depends on the dataset type (even/odd) which determines
// whether we include rain/extreme or wind data.
uint32_t RegionWeather::pack(int dataset_type) const {
  uint32_t weather = 0;

  // Bits 0-3: day weather
  int wd = weather_day;
  if (wd < 0 || wd > 15) wd = 1;
  weather |= wd;

  // Bits 4-7: night weather
  int wn = weather_night;
  if (wn < 0 || wn > 15) wn = 1;
  weather |= wn << 4;

  if ((dataset_type & 1) == 0) {
    // Even datasets: extreme weather, rain probability, day temp
    int ext = extreme;
    if (ext < 0 || ext > 15) ext = 0;
    weather |= ext << 8;

    // Map rain probability to 0-7
    int rain_mapped = 0;
    int best_diff = 100;
    static const int probs[] = {0, 15, 30, 45, 60, 75, 90, 100};
    for (int i = 0; i < 8; i++) {
      int diff = std::abs(probs[i] - rain_probability);
      if (diff < best_diff) {
        best_diff = diff;
        rain_mapped = i;
      }
    }
    weather |= rain_mapped << 12;

    // Temperature (day)
    int temp = temperature_day + 22;
    if (temp < 0) temp = 0;
    if (temp > 63) temp = 63;
    weather |= temp << 16;
  } else {
    // Odd datasets: wind direction, wind strength, night temp
    int wdir = wind_direction;
    if (wdir < 0 || wdir > 15) wdir = 8;
    weather |= wdir << 8;

    // Map wind strength to 0-7
    int ws = wind_strength;
    int wind_mapped;
    if (ws < 1)
      wind_mapped = 0;
    else if (ws < 7)
      wind_mapped = (ws + 1) / 2;
    else if (ws < 10)
      wind_mapped = ws - 3;
    else
      wind_mapped = 7;
    weather |= wind_mapped << 12;

    // Temperature (night)
    int temp = temperature_night + 22;
    if (temp < 0) temp = 0;
    if (temp > 63) temp = 63;
    weather |= temp << 16;
  }

  // Bit 22: magic bit (always 1)
  weather |= 0x1 << 22;

  return weather;
}

// Thread-safe weather store. Changes are staged via SetRegion/SetAllRegions
// and only applied at 3-minute boundaries via ApplyPending().
// Supports multi-day forecasts: key = region * 4 + forecast_day.
void RegionWeatherStore::SetRegion(int region, int forecast_day, 
                                    const RegionWeather& weather) {
  if (region >= 0 && region < 90 && forecast_day >= 0 && forecast_day < 4) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_[MakeKey(region, forecast_day)] = weather;
    pending_dirty_ = true;
  }
}

// Set weather for all forecast days of a region (for CLI compatibility)
void RegionWeatherStore::SetRegion(int region, const RegionWeather& weather) {
  if (region >= 0 && region < 90) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int day = 0; day < 4; day++) {
      pending_[MakeKey(region, day)] = weather;
    }
    pending_dirty_ = true;
  }
}

void RegionWeatherStore::SetAllRegions(const RegionWeather& weather) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int region = 0; region < 90; region++) {
    for (int day = 0; day < 4; day++) {
      pending_[MakeKey(region, day)] = weather;
    }
  }
  pending_dirty_ = true;
}

void RegionWeatherStore::ResetPending() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.clear();
  pending_dirty_ = true;  // Will clear active on apply
}

void RegionWeatherStore::ApplyPending() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_dirty_) {
    // Merge pending into active (or replace if reset was called)
    if (pending_.empty()) {
      active_.clear();  // Reset case
    } else {
      for (const auto& kv : pending_) {
        active_[kv.first] = kv.second;
      }
    }
    pending_.clear();
    pending_dirty_ = false;
  }
}

const RegionWeather& RegionWeatherStore::GetRegion(int region, int forecast_day) const {
  std::lock_guard<std::mutex> lock(mutex_);
  int key = MakeKey(region, forecast_day);
  auto it = active_.find(key);
  if (it != active_.end()) {
    return it->second;
  }
  // Fall back to day 0 if specific day not set
  if (forecast_day > 0) {
    auto day0 = active_.find(MakeKey(region, 0));
    if (day0 != active_.end()) {
      return day0->second;
    }
  }
  return default_weather_;
}

// Calculate which region is being transmitted at the given time.
// The Meteotime system cycles through 90 regions every 270 minutes (4.5 hours).
// Dataset types 0-6 cover regions 0-59, types 7+ cover regions 60-89.
int GetRegionForTime(int german_utc_hour, int local_minute) {
  int dataset = ((german_utc_hour + 2) % 24) * 20 + (local_minute / 3);
  int dataset_type = dataset / 60;
  if (dataset_type < 7) {
    return dataset % 60;
  } else {
    return 60 + (dataset % 30);
  }
}

// Get the dataset type (0-13) for the current time.
// Even types (0,2,4...) carry extreme weather and rain probability.
// Odd types (1,3,5...) carry wind direction and strength.
int GetDatasetType(int german_utc_hour, int local_minute) {
  int dataset = ((german_utc_hour + 2) % 24) * 20 + (local_minute / 3);
  return dataset / 60;
}

// Generate the encryption key from the *next* minute's time.
// The key is formed from BCD-encoded time values packed into 40 bits.
// This matches the format expected by DCF77 receivers for decryption.
uint64_t generate_weather_key(const struct tm& tm) {
  uint64_t key = 0;
  key |= static_cast<uint64_t>(tm.tm_min % 10) << 0;
  key |= static_cast<uint64_t>(tm.tm_min / 10) << 4;
  key |= static_cast<uint64_t>(tm.tm_hour % 10) << 8;
  key |= static_cast<uint64_t>(tm.tm_hour / 10) << 12;
  key |= static_cast<uint64_t>(tm.tm_mday % 10) << 16;
  key |= static_cast<uint64_t>(tm.tm_mday / 10) << 20;
  key |= static_cast<uint64_t>((tm.tm_mon + 1) % 10) << 24;
  key |= static_cast<uint64_t>((tm.tm_mon + 1) / 10) << 28;
  int wday = tm.tm_wday > 0 ? tm.tm_wday : 7;
  key |= static_cast<uint64_t>(wday) << 29;
  key |= static_cast<uint64_t>(tm.tm_year % 10) << 32;
  key |= static_cast<uint64_t>((tm.tm_year / 10) % 10) << 36;
  return key;
}

// Generate the complete encrypted weather cipher for one 3-minute cycle.
// The cipher is split into 3 chunks, one transmitted each minute in bits 1-14.
// Selects the correct forecast day based on dataset type.
uint64_t generate_weather_cipher(time_t timestamp, int local_minute,
                                  int local_hour, int german_utc_hour,
                                  const RegionWeatherStore& store) {
  int region = GetRegionForTime(german_utc_hour, local_minute);
  int dataset_type = GetDatasetType(german_utc_hour, local_minute);
  int forecast_day = GetForecastDayFromDatasetType(dataset_type);

  const RegionWeather& region_weather = store.GetRegion(region, forecast_day);
  uint32_t weather = region_weather.pack(dataset_type);

  time_t next_minute = timestamp + 60;
  struct tm tm;
  localtime_r(&next_minute, &tm);
  uint64_t key = generate_weather_key(tm);

  return weather_encode(weather, key);
}

// Extract one of the 3 chunks from the 40-bit cipher.
// Chunk 0: bits 1-7, 9-14 (shifted appropriately)
// Chunk 1: bits 12-25 (straight extraction)
// Chunk 2: bits 26-39 (straight extraction)
uint16_t get_weather_chunk(uint64_t cipher, int chunk_index) {
  switch (chunk_index) {
    case 0: {
      uint16_t chunk = (cipher & 0x3f) << 1;
      chunk |= ((cipher & 0x0fc0) >> 6) << 9;
      return chunk;
    }
    case 1:
      return (cipher >> 12) & 0x3fff;
    case 2:
      return (cipher >> 26) & 0x3fff;
    default:
      return 0;
  }
}
