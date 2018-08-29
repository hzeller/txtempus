// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2018 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>
//
// DCF77 simulating transmitter, to be run on the Raspberry Pi.
// Make sure to stay within the regulation limits of HF transmissions!

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>

#include "gpio.h"
#include "time-signal-source.h"

// The GPIO bit that is pulled down for attenuation of the signal.
const uint32_t kAttenuationGPIOBit = (1<<17);

volatile sig_atomic_t interrupted = 0;
static void InterruptHandler(int signo) {
  interrupted = signo;
}

static time_t TruncateTo(time_t t, int v) { return t - t % v; }

static void WaitUntil(const struct timespec &ts) {
  clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);
}

static void StartCarrier(GPIO *gpio, int frequency) {
  double f = gpio->StartClock(frequency);
  fprintf(stderr, "Requesting %d Hz, getting %.3f Hz carrier\n", frequency, f);
}

static void SetPower(GPIO *gpio, bool high) {
  if (high) {
    gpio->RequestInput(kAttenuationGPIOBit);   // High-Z
  } else {
    gpio->RequestOutput(kAttenuationGPIOBit);  // Pull down.
    gpio->ClearBits(kAttenuationGPIOBit);
  }
}

int main() {
  GPIO gpio;
  if (!gpio.Init()) {
    fprintf(stderr, "Need to be root\n");
    return 1;
  }

  // Currently only DCF77, but more implementations about to follow
  // WWVB, JJY, NPL
  TimeSignalSource *time_source = new DCF77TimeSignalSource();

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // Make sure the kernel knows that we're serious about accuracy of sleeps.
  struct sched_param sp;
  sp.sched_priority = 99;
  sched_setscheduler(0, SCHED_FIFO, &sp);

  StartCarrier(&gpio, time_source->GetCarrierFrequencyHz());

  // If tarted in the middle of a minute, we quickly slide down to current sec.
  const time_t first_minute = TruncateTo(time(NULL),  60);
  struct timespec target_wait;
  for (time_t minute_start = first_minute; !interrupted; minute_start += 60) {
    time_source->PrepareMinute(minute_start);

    for (int second = 0; second < 60 && !interrupted; ++second) {
      const TimeSignalSource::SecondModulation &modulation
        = time_source->GetModulationForSecond(second);

      // First, let's wait until we reach the beginning of that second
      target_wait.tv_sec = minute_start + second;
      target_wait.tv_nsec = 0;
      WaitUntil(target_wait);
      if (interrupted) break;

      // Depending on the time source, there can be multiple amplitude
      // modulation changes per second.
      for (auto m : modulation) {
        SetPower(&gpio, m.power == CarrierPower::HIGH);
        if (m.duration_ms == 0) break; // last one.
        target_wait.tv_nsec += m.duration_ms * 1000000;
        WaitUntil(target_wait);
      }
      fprintf(stderr, ":%02d\b\b\b", second);
    }
  }

  fprintf(stderr, "\nReceived interrupt (sig %d). Exiting.\n", interrupted);
  gpio.StopClock();
}
