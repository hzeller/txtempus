// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// This is txtempus, a LF time signal transmitter.
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
//
// Time-signal simulating transmitter, supporting various types such
// as DCF77, WWVB, ... to be run on the Raspberry Pi.
// Make sure to stay within the regulation limits of HF transmissions!

#define _XOPEN_SOURCE

#include <sched.h>
#include <strings.h>
#include <time.h>  // NOLINT(modernize-deprecated-headers) for clock_nanosleep

#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The following is to work around clang-tidy being confused and not
// understanding that unistd.h indeed provides getopt(). So let's include
// unistd.h for correctness, and then soothe clang-tidy with decls.
// TODO: how make it just work with including unistd.h ?
#include <unistd.h>                            // NOLINT
extern "C" {                                   //
extern char *optarg;                           // NOLINT
extern int optind;                             // NOLINT
int getopt(int, char *const *, const char *);  // NOLINT
}

#include <memory>

#include "carrier-power.h"
#include "dcf77-control.h"
#include "dcf77-weather.h"
#include "hardware-control.h"
#include "time-signal-source.h"

static bool verbose = false;
static bool dryrun = false;
static bool carrier_only = false;

namespace {
volatile sig_atomic_t interrupted = 0;
extern "C" {
void InterruptHandler(int signo) { interrupted = signo; }
}

// Truncate "t" so that it is multiple of "d"
time_t TruncateTo(time_t t, int d) { return t - t % d; }

void WaitUntil(const struct timespec &ts) {
  if (dryrun) return;
  // NOLINTNEXTLINE(misc-include-cleaner) macros should be defined by time.h
  clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, nullptr);
}

void StartCarrier(HardwareControl *hw, int frequency) {
  if (dryrun) return;
  double f = hw->StartClock(frequency);
  if (verbose) {
    fprintf(stderr, "Requesting %d Hz, getting %.3f Hz carrier\n", frequency,
            f);
  }
}

void SetTxPower(HardwareControl *hw, CarrierPower power) {
  if (dryrun) return;
  if (carrier_only) power = CarrierPower::HIGH;
  hw->SetTxPower(power);
}

time_t ParseLocalTime(const char *time_string) {
  struct tm tm = {};
  const char *final_pos = strptime(time_string, "%Y-%m-%d %H:%M", &tm);
  if (!final_pos || *final_pos) return 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

void PrintLocalTime(time_t t) {
  char buf[32];
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  fprintf(stderr, "%s", buf);
}

// Show a full modulation of one second as little ASCII-art.
void PrintModulationChart(const TimeSignalSource::SecondModulation &mod) {
  static const int kMsPerDash = 100;
  fprintf(stderr, " [");
  int running_ms = 0;
  int target_ms = 0;
  bool power = false;
  for (const ModulationDuration &m : mod) {
    power = (m.power == CarrierPower::HIGH);
    target_ms += m.duration_ms;
    for (/**/; running_ms < target_ms; running_ms += kMsPerDash) {
      fprintf(stderr, "%s", power ? "#" : "_");
    }
  }
  for (/**/; running_ms < 1000; running_ms += kMsPerDash) {
    fprintf(stderr, "%s", power ? "#" : "_");
  }
  fprintf(stderr, "]\n");
}

// Factory function to create the appropriate time signal source.
// DCF77 has two variants: standard and weather-enabled (Meteotime).
std::unique_ptr<TimeSignalSource> CreateTimeSourceFromName(const char *n,
                                                            bool weather_mode) {
  if (strcasecmp(n, "DCF77") == 0) {
    if (weather_mode) {
      return std::make_unique<DCF77WeatherTimeSignalSource>();
    }
    return std::make_unique<DCF77TimeSignalSource>();
  }
  if (strcasecmp(n, "WWVB") == 0) {
    return std::make_unique<WWVBTimeSignalSource>();
  }
  if (strcasecmp(n, "JJY40") == 0) {
    return std::make_unique<JJY40TimeSignalSource>();
  }
  if (strcasecmp(n, "JJY60") == 0) {
    return std::make_unique<JJY60TimeSignalSource>();
  }
  if (strcasecmp(n, "MSF") == 0) return std::make_unique<MSFTimeSignalSource>();
  return nullptr;
}

int usage(const char *msg, const char *progname) {
  fprintf(stderr,
          "%susage: %s [options]\n"
          "Options:\n"
          "\t-s <service>          : Service; one of "
          "'DCF77', 'WWVB', 'JJY40', 'JJY60', 'MSF'\n"
          "\t-r <minutes>          : Run for limited number of minutes. "
          "(default: no limit)\n"  // in truth: a couple thousand years...
          "\t-t 'YYYY-MM-DD HH:MM' : Transmit the given local time "
          "(default: now)\n"
          "\t-z <minutes>          : Transmit the time offset from local "
          "(default: 0 minutes)\n"
          "\t-v                    : Verbose.\n"
          "\t-c                    : Carrier wave only.\n"
          "\t-n                    : Dryrun, only showing modulation "
          "envelope.\n"
          "\t-h                    : This help.\n"
          "\n"
          "DCF77 Weather Options (Meteotime):\n"
          "\t-W                    : Enable weather encoding (DCF77 only)\n"
          "\t--set-all <day> <night> <temp_d> <temp_n>\n"
          "\t                      : Set same weather for all 90 regions\n"
          "\t--set <region> <day> <night> <temp_d> <temp_n>\n"
          "\t                      : Set weather for specific region (0-89)\n"
          "\t--control-fifo <path>  : Control FIFO for runtime updates\n"
          "\t                        (default: /tmp/txtempus.fifo when -W)\n"
          "\n"
          "Weather codes: 0=Reserved, 1=Sunny/Clear, 2=Partly clouded,\n"
          "  3=Mostly clouded, 4=Overcast, 5=High fog, 6=Fog, 7=Showers,\n"
          "  8=Light rain, 9=Heavy rain, 10=Frontal storms, 11=Heat storms,\n"
          "  12=Sleet showers, 13=Snow showers, 14=Sleet, 15=Snow\n"
          "\n"
          "Runtime control via FIFO (when -W enabled):\n"
          "  echo 'set 26 1 1 25 18' > /tmp/txtempus.fifo\n"
          "  echo 'set_all 8 8 12 8' > /tmp/txtempus.fifo\n"
          "  echo 'reset' > /tmp/txtempus.fifo\n"
          "  (Responses appear on txtempus stderr)\n",
          msg, progname);
  return 1;
}

}  // end anonymous namespace

int main(int argc, char *argv[]) {
  const time_t now = TruncateTo(time(nullptr), 60);  // Time: full minute
  std::unique_ptr<TimeSignalSource> time_source{};
  time_t chosen_time = now;
  int zone_offset = 0;
  int ttl = INT_MAX;
  const char *service_name = nullptr;
  bool weather_mode = false;
  std::string control_socket_path;

  // Collect region weather settings from CLI.
  // These are applied before the main loop starts.
  struct RegionSetting {
    int region;
    RegionWeather weather;
  };
  std::vector<RegionSetting> region_settings;
  RegionWeather all_regions_weather;
  bool set_all_regions = false;

  // Parse command line options. We use manual parsing instead of getopt()
  // to support long options like --set and --set-all for weather configuration.
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      chosen_time = ParseLocalTime(argv[++i]);
      if (chosen_time <= 0) return usage("Invalid time string\n", argv[0]);
    } else if (strcmp(argv[i], "-z") == 0 && i + 1 < argc) {
      zone_offset = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
      ttl = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      service_name = argv[++i];
    } else if (strcmp(argv[i], "-n") == 0) {
      dryrun = true;
      verbose = true;
      ttl = 1;
    } else if (strcmp(argv[i], "-c") == 0) {
      carrier_only = true;
    } else if (strcmp(argv[i], "-h") == 0) {
      return usage("", argv[0]);
    } else if (strcmp(argv[i], "-W") == 0) {
      weather_mode = true;
    } else if (strcmp(argv[i], "--set-all") == 0 && i + 4 < argc) {
      weather_mode = true;
      set_all_regions = true;
      all_regions_weather.weather_day = atoi(argv[++i]);
      all_regions_weather.weather_night = atoi(argv[++i]);
      all_regions_weather.temperature_day = atoi(argv[++i]);
      all_regions_weather.temperature_night = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--set") == 0 && i + 5 < argc) {
      weather_mode = true;
      RegionSetting rs;
      rs.region = atoi(argv[++i]);
      rs.weather.weather_day = atoi(argv[++i]);
      rs.weather.weather_night = atoi(argv[++i]);
      rs.weather.temperature_day = atoi(argv[++i]);
      rs.weather.temperature_night = atoi(argv[++i]);
      region_settings.push_back(rs);
    } else if (strcmp(argv[i], "--control-fifo") == 0 && i + 1 < argc) {
      control_socket_path = argv[++i];
    } else if (argv[i][0] == '-') {
      return usage("Unknown option\n", argv[0]);
    }
  }

  chosen_time += zone_offset * (time_t)60;
  const int time_offset = chosen_time - now;

  if (!service_name) {
    return usage("Please choose a service name with -s option\n", argv[0]);
  }

  time_source = CreateTimeSourceFromName(service_name, weather_mode);
  if (!time_source) {
    return usage("Unknown service name\n", argv[0]);
  }

  // Set up weather encoding if enabled. The weather source wraps the base
  // DCF77 source and injects Meteotime data into bits 1-14 each minute.
  std::unique_ptr<DCF77ControlSocket> control_socket;
  DCF77WeatherTimeSignalSource *weather_source = nullptr;
  if (weather_mode) {
    weather_source =
        dynamic_cast<DCF77WeatherTimeSignalSource *>(time_source.get());
    if (weather_source) {
      weather_source->SetVerbose(verbose);

      // Apply CLI settings
      if (set_all_regions) {
        weather_source->SetAllRegionsWeather(all_regions_weather);
        if (verbose) {
          fprintf(stderr,
                  "Set all regions: %s/%s %d/%d°C\n",
                  weather_day_names[all_regions_weather.weather_day & 0xf],
                  weather_night_names[all_regions_weather.weather_night & 0xf],
                  all_regions_weather.temperature_day,
                  all_regions_weather.temperature_night);
        }
      }
      for (const auto &rs : region_settings) {
        weather_source->SetRegionWeather(rs.region, rs.weather);
        if (verbose) {
          fprintf(stderr, "Added region %d (%s): %s/%s %d/%d°C\n", rs.region,
                  region_names[rs.region],
                  weather_day_names[rs.weather.weather_day & 0xf],
                  weather_night_names[rs.weather.weather_night & 0xf],
                  rs.weather.temperature_day, rs.weather.temperature_night);
        }
      }

      // Start control socket for runtime weather updates.
      // Allows external programs to modify weather data while transmitting.
      control_socket = std::make_unique<DCF77ControlSocket>(weather_source);
      if (control_socket_path.empty()) {
        control_socket_path = "/tmp/txtempus.fifo";
      }
      if (!dryrun && control_socket->Start(control_socket_path)) {
        if (verbose) {
          fprintf(stderr, "Control socket: %s\n", control_socket_path.c_str());
        }
      }
    }
  }

  HardwareControl hw{};
  if (!dryrun && !hw.Init()) {
    fprintf(stderr, "Initialization failed\n");
    return 1;
  }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // Make sure the kernel knows that we're serious about accuracy of sleeps.
  struct sched_param sp;  // NOLINT(misc-include-cleaner) is in sched.h
  sp.sched_priority = 99;
  sched_setscheduler(0, SCHED_FIFO, &sp);

  StartCarrier(&hw, time_source->GetCarrierFrequencyHz());

  struct timespec target_wait;
  for (time_t minute_start = now; !interrupted && ttl--; minute_start += 60) {
    const time_t transmit_time = minute_start + time_offset;
    if (verbose) PrintLocalTime(transmit_time);
    if (dryrun) fprintf(stderr, " -> tx-modulation\n");
    time_source->PrepareMinute(transmit_time);

    for (int second = 0; second < 60 && !interrupted; ++second) {
      const TimeSignalSource::SecondModulation &modulation =
          time_source->GetModulationForSecond(second);

      // First, let's wait until we reach the beginning of that second
      target_wait.tv_sec = minute_start + second;
      target_wait.tv_nsec = 0;
      WaitUntil(target_wait);
      if (interrupted) break;

      // Poll control socket for weather updates from external clients.
      // Changes are staged and applied at the start of each 3-minute cycle.
      if (control_socket) {
        control_socket->Poll();
      }

      if (verbose && !dryrun) fprintf(stderr, "\b\b\b:%02d", second);

      // Depending on the time source, there can be multiple amplitude
      // modulation changes per second.
      for (const ModulationDuration &m : modulation) {
        SetTxPower(&hw, m.power);
        if (m.duration_ms == 0) break;  // last one.
        target_wait.tv_nsec += m.duration_ms * 1000000L;
        WaitUntil(target_wait);
      }
      if (dryrun) PrintModulationChart(modulation);
    }
    if (verbose) fprintf(stderr, "\n");
  }

  if (!dryrun) hw.StopClock();
}
