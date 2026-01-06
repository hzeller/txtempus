// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// DCF77 Meteotime weather data generation
//
// Ported from osmocom-analog project:
// https://github.com/osmocom/osmocom-analog
// Original DCF77 weather implementation (C) 2022 Andreas Eversberg <jolly@eversberg.eu>

#ifndef DCF77_WEATHER_H
#define DCF77_WEATHER_H

#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>

// Weather data for a single region/day combination
struct RegionWeather {
  int weather_day = 1;       // 0-15 (1=sunny)
  int weather_night = 1;     // 0-15 (1=clear)
  int extreme = 0;           // 0-15 extreme weather code
  int rain_probability = 0;  // 0-100% (mapped to 0-7)
  int wind_direction = 8;    // 0-15 (8=changeable)
  int wind_strength = 0;     // 0-12 Bft (mapped to 0-7)
  int temperature_day = 20;  // -22 to +40°C
  int temperature_night = 15;

  // Pack weather data into 22-bit plaintext for given dataset type
  uint32_t pack(int dataset_type) const;
};

// Storage for weather with multi-day forecast support.
// Key is (region * 4 + forecast_day) where forecast_day is 0-3.
// Updates go to pending buffer, swapped to active at start of new 3-min cycle.
class RegionWeatherStore {
 public:
  // Set weather for a specific region (0-89) and forecast day (0-3)
  // day=0: today, day=1: tomorrow, day=2: day+2, day=3: day+3
  void SetRegion(int region, int forecast_day, const RegionWeather& weather);

  // Set weather for all forecast days of a region (for CLI compatibility)
  void SetRegion(int region, const RegionWeather& weather);

  // Set same weather for all regions and all days
  void SetAllRegions(const RegionWeather& weather);

  // Clear all pending changes
  void ResetPending();

  // Swap pending to active (call at start of 3-minute cycle)
  void ApplyPending();

  // Get weather for a region and forecast day from active buffer
  const RegionWeather& GetRegion(int region, int forecast_day) const;

  // Get weather for a region (day 0) - for backward compatibility
  const RegionWeather& GetRegion(int region) const { return GetRegion(region, 0); }

  // Check if any data is configured
  bool HasData() const { return !active_.empty(); }

  // Check if there are pending changes
  bool HasPending() const { return pending_dirty_; }

  // Set default weather for regions without explicit data
  void SetDefault(const RegionWeather& weather) { default_weather_ = weather; }

 private:
  // Key: region * 4 + forecast_day
  static int MakeKey(int region, int forecast_day) { return region * 4 + forecast_day; }

  std::map<int, RegionWeather> active_;   // Currently transmitting
  std::map<int, RegionWeather> pending_;  // Staged changes
  bool pending_dirty_ = false;            // True if pending has changes
  RegionWeather default_weather_;
  mutable std::mutex mutex_;
};

// Calculate current region from time
int GetRegionForTime(int german_utc_hour, int local_minute);

// Calculate dataset type (0-7) from time
// Types 0,1 = day 0 (today), 2,3 = day 1, 4,5 = day 2, 6,7 = day 3
int GetDatasetType(int german_utc_hour, int local_minute);

// Get forecast day (0-3) from dataset type (0-7)
inline int GetForecastDayFromDatasetType(int dataset_type) {
  return dataset_type / 2;
}

// Generate encryption key from local time
uint64_t generate_weather_key(const struct tm& tm);

// Generate weather cipher for current time
uint64_t generate_weather_cipher(time_t timestamp, int local_minute,
                                  int local_hour, int german_utc_hour,
                                  const RegionWeatherStore& store);

// Get weather chunk for current minute
uint16_t get_weather_chunk(uint64_t cipher, int chunk_index);

// Weather code descriptions
extern const char* weather_day_names[16];
extern const char* weather_night_names[16];
extern const char* extreme_weather_names[16];
extern const char* wind_direction_names[16];
extern const char* wind_strength_names[8];
extern const char* rain_probability_names[8];
extern const char* region_names[90];

#endif  // DCF77_WEATHER_H
