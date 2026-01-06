// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// Control FIFO for runtime weather updates

#include "dcf77-control.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dcf77-weather.h"

DCF77ControlSocket::DCF77ControlSocket(DCF77WeatherTimeSignalSource* source)
    : source_(source) {}

DCF77ControlSocket::~DCF77ControlSocket() { Stop(); }

// Create the control FIFO at the given path.
// Old FIFO is removed if it exists.
bool DCF77ControlSocket::Start(const std::string& fifo_path) {
  socket_path_ = fifo_path;
  unlink(fifo_path.c_str());

  // Create the FIFO
  if (mkfifo(fifo_path.c_str(), 0666) < 0) {
    perror("mkfifo");
    return false;
  }

  // Open FIFO in non-blocking read mode
  // We open for read+write so it doesn't block waiting for a writer
  server_fd_ = open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
  if (server_fd_ < 0) {
    perror("open fifo");
    unlink(fifo_path.c_str());
    return false;
  }

  return true;
}

// Clean up FIFO resources.
void DCF77ControlSocket::Stop() {
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
  if (!socket_path_.empty()) {
    unlink(socket_path_.c_str());
    socket_path_.clear();
  }
}

// Non-blocking poll for incoming commands.
// Called once per second from the main transmit loop.
void DCF77ControlSocket::Poll() {
  if (server_fd_ < 0) return;

  char buf[512];
  ssize_t n = read(server_fd_, buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    char* line = strtok(buf, "\n");
    while (line) {
      ProcessCommand(line);
      line = strtok(nullptr, "\n");
    }
  } else if (n == 0) {
    // EOF - reopen FIFO to allow new writers
    close(server_fd_);
    server_fd_ = open(socket_path_.c_str(), O_RDONLY | O_NONBLOCK);
  }
}

// Process a single command from the control FIFO.
// Commands are staged and applied at the next 3-minute boundary.
// Responses are written to stderr (not back to FIFO) for simplicity.
void DCF77ControlSocket::ProcessCommand(const std::string& cmd) {
  // Commands:
  // set <region> <day> <night> <temp_day> <temp_night> [extreme] [rain] [wind_dir] [wind_bft]
  //     Sets weather for ALL forecast days (today through day+3)
  // forecast <region> <forecast_day> <day> <night> <temp_d> <temp_n> [extreme] [rain] [wind_dir] [wind_bft]
  //     Sets weather for SPECIFIC forecast day (0=today, 1=tomorrow, 2=day+2, 3=day+3)
  // set_all <day> <night> <temp_day> <temp_night> [extreme] [rain] [wind_dir] [wind_bft]
  // reset
  // status
  // list

  if (cmd.rfind("forecast ", 0) == 0) {
    // Parse: forecast <region> <forecast_day> <day> <night> <temp_day> <temp_night> ...
    int region, forecast_day, day, night, temp_day, temp_night;
    int extreme = 0, rain = 0, wind_dir = 8, wind_bft = 0;
    int parsed = sscanf(cmd.c_str(), "forecast %d %d %d %d %d %d %d %d %d %d", 
                        &region, &forecast_day, &day, &night, &temp_day, &temp_night, 
                        &extreme, &rain, &wind_dir, &wind_bft);
    if (parsed >= 6 && region >= 0 && region < 90 && forecast_day >= 0 && forecast_day < 4) {
      RegionWeather w;
      w.weather_day = day;
      w.weather_night = night;
      w.temperature_day = temp_day;
      w.temperature_night = temp_night;
      w.extreme = extreme;
      w.rain_probability = rain;
      w.wind_direction = wind_dir;
      w.wind_strength = wind_bft;
      source_->SetRegionWeather(region, forecast_day, w);
      const char* day_names[] = {"today", "tomorrow", "day+2", "day+3"};
      fprintf(stderr,
              "[Control] Staged region %d (%s) %s: %s/%s %d/%d°C\n",
              region, region_names[region], day_names[forecast_day],
              weather_day_names[day & 0xf], weather_night_names[night & 0xf], 
              temp_day, temp_night);
    } else {
      fprintf(stderr,
              "[Control] ERROR: Usage: forecast <region 0-89> <forecast_day 0-3> "
              "<day 0-15> <night 0-15> <temp_day> <temp_night> [extreme] [rain%%] "
              "[wind_dir] [wind_bft]\n"
              "          forecast_day: 0=today, 1=tomorrow, 2=day+2, 3=day+3\n");
    }
  } else if (cmd.rfind("set ", 0) == 0 && cmd.rfind("set_all ", 0) != 0) {
    // Parse: set <region> <day> <night> <temp_day> <temp_night> ...
    // Sets weather for ALL forecast days
    int region, day, night, temp_day, temp_night;
    int extreme = 0, rain = 0, wind_dir = 8, wind_bft = 0;
    int parsed = sscanf(cmd.c_str(), "set %d %d %d %d %d %d %d %d %d", &region,
                        &day, &night, &temp_day, &temp_night, &extreme, &rain,
                        &wind_dir, &wind_bft);
    if (parsed >= 5 && region >= 0 && region < 90) {
      RegionWeather w;
      w.weather_day = day;
      w.weather_night = night;
      w.temperature_day = temp_day;
      w.temperature_night = temp_night;
      w.extreme = extreme;
      w.rain_probability = rain;
      w.wind_direction = wind_dir;
      w.wind_strength = wind_bft;
      source_->SetRegionWeather(region, w);
      fprintf(stderr,
              "[Control] Staged region %d (%s) all days: %s/%s %d/%d°C\n",
              region, region_names[region], weather_day_names[day & 0xf],
              weather_night_names[night & 0xf], temp_day, temp_night);
    } else {
      fprintf(stderr,
              "[Control] ERROR: Usage: set <region 0-89> <day 0-15> <night 0-15> "
              "<temp_day> <temp_night> [extreme] [rain%%] [wind_dir] [wind_bft]\n");
    }
  } else if (cmd.rfind("forecast_all ", 0) == 0) {
    // Parse: forecast_all <forecast_day> <day> <night> <temp_day> <temp_night> ...
    // Sets weather for specific forecast day across ALL 90 regions
    int forecast_day, day, night, temp_day, temp_night;
    int extreme = 0, rain = 0, wind_dir = 8, wind_bft = 0;
    int parsed = sscanf(cmd.c_str(), "forecast_all %d %d %d %d %d %d %d %d %d", 
                        &forecast_day, &day, &night, &temp_day, &temp_night, 
                        &extreme, &rain, &wind_dir, &wind_bft);
    if (parsed >= 5 && forecast_day >= 0 && forecast_day < 4) {
      RegionWeather w;
      w.weather_day = day;
      w.weather_night = night;
      w.temperature_day = temp_day;
      w.temperature_night = temp_night;
      w.extreme = extreme;
      w.rain_probability = rain;
      w.wind_direction = wind_dir;
      w.wind_strength = wind_bft;
      const char* day_names[] = {"today", "tomorrow", "day+2", "day+3"};
      for (int region = 0; region < 90; region++) {
        source_->SetRegionWeather(region, forecast_day, w);
      }
      fprintf(stderr,
              "[Control] Staged all 90 regions for %s: %s/%s %d/%d°C\n",
              day_names[forecast_day], weather_day_names[day & 0xf], 
              weather_night_names[night & 0xf], temp_day, temp_night);
    } else {
      fprintf(stderr,
              "[Control] ERROR: Usage: forecast_all <forecast_day 0-3> <day 0-15> "
              "<night 0-15> <temp_day> <temp_night> [extreme] [rain%%] [wind_dir] [wind_bft]\n"
              "          forecast_day: 0=today, 1=tomorrow, 2=day+2, 3=day+3\n");
    }
  } else if (cmd.rfind("set_all ", 0) == 0) {
    // Sets weather for ALL regions AND ALL forecast days
    int day, night, temp_day, temp_night;
    int extreme = 0, rain = 0, wind_dir = 8, wind_bft = 0;
    int parsed =
        sscanf(cmd.c_str(), "set_all %d %d %d %d %d %d %d %d", &day, &night,
               &temp_day, &temp_night, &extreme, &rain, &wind_dir, &wind_bft);
    if (parsed >= 4) {
      RegionWeather w;
      w.weather_day = day;
      w.weather_night = night;
      w.temperature_day = temp_day;
      w.temperature_night = temp_night;
      w.extreme = extreme;
      w.rain_probability = rain;
      w.wind_direction = wind_dir;
      w.wind_strength = wind_bft;
      source_->SetAllRegionsWeather(w);
      fprintf(stderr,
              "[Control] Staged all 90 regions, all days: %s/%s %d/%d°C\n",
              weather_day_names[day & 0xf], weather_night_names[night & 0xf],
              temp_day, temp_night);
    } else {
      fprintf(stderr,
              "[Control] ERROR: Usage: set_all <day 0-15> <night 0-15> <temp_day> "
              "<temp_night> [extreme] [rain%%] [wind_dir] [wind_bft]\n");
    }
  } else if (cmd == "reset") {
    source_->ResetAllWeather();
    fprintf(stderr, "[Control] Staged reset (applies next cycle)\n");
  } else if (cmd == "status") {
    const auto& store = source_->GetWeatherStore();
    fprintf(stderr, "[Control] Active=%s, Pending=%s\n",
            store.HasData() ? "yes" : "no",
            store.HasPending() ? "yes" : "no");
  } else if (cmd.rfind("list", 0) == 0) {
    fprintf(stderr, "[Control] Configured regions:\n");
    for (int i = 0; i < 90; i++) {
      const RegionWeather& w = source_->GetWeatherStore().GetRegion(i);
      if (source_->GetWeatherStore().HasData()) {
        fprintf(stderr, "  %2d: %s - %s/%s %d/%d°C\n", i,
                region_names[i], weather_day_names[w.weather_day & 0xf],
                weather_night_names[w.weather_night & 0xf], w.temperature_day,
                w.temperature_night);
      }
    }
  } else if (cmd == "help") {
    fprintf(stderr,
            "[Control] Commands:\n"
            "  set <region> <day> <night> <temp_d> <temp_n> [extreme] [rain%%] [wind_dir] [wind_bft]\n"
            "      Set weather for ALL forecast days (today through day+3)\n"
            "  forecast <region> <fday> <day> <night> <temp_d> <temp_n> [...]\n"
            "      Set weather for specific forecast day (fday: 0=today, 1=tomorrow, 2=day+2, 3=day+3)\n"
            "  set_all <day> <night> <temp_d> <temp_n> [...]\n"
            "      Set all 90 regions, all forecast days\n"
            "  forecast_all <fday> <day> <night> <temp_d> <temp_n> [...]\n"
            "      Set all 90 regions for specific forecast day\n"
            "  reset   - Clear all weather data\n"
            "  status  - Show pending/active status\n"
            "  list    - List configured regions\n"
            "  help    - This help\n"
            "Changes are staged and apply at start of next 3-minute cycle.\n");
  } else if (!cmd.empty()) {
    fprintf(stderr, "[Control] ERROR: Unknown command '%s'. Type 'help' for usage.\n",
            cmd.c_str());
  }
}
