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
#include <sys/time.h>
#include <time.h>
#include <signal.h>

#include "gpio.h"

// The GPIO bit that is pulled down for attenuation of the signal.
const uint32_t kAttenuationGPIOBit = (1<<17);

// DCF77 attenutation times to transmit a zero or one.
const int kZeroBitNanos = 100 * 1000000;
const int kOneBitNanos  = 200 * 1000000;

typedef uint64_t MinuteData;

volatile sig_atomic_t interrupted = 0;
static void InterruptHandler(int signo) {
  interrupted = signo;
}

static uint64_t to_bcd(uint8_t n) { return (((n / 10) % 10) << 4) | (n % 10); }
static uint64_t parity(uint64_t d, uint8_t from, uint8_t to_including) {
  uint8_t result = 0;
  for (int bit = from; bit <= to_including; ++bit) {
    if (d & (1LL << bit)) result++;
  }
  return result & 0x1;
}

static MinuteData CreateDCF77Bits(time_t t) {
  struct tm breakdown;
  localtime_r(&t, &breakdown);

  // https://de.wikipedia.org/wiki/DCF77
  MinuteData result = 0;
  result |= (breakdown.tm_isdst ? 1 : 0) << 17;
  result |= (breakdown.tm_isdst ? 0 : 1) << 18;
  result |= (1<<20);  // start time bit.
  result |= to_bcd(breakdown.tm_min)  << 21;
  result |= to_bcd(breakdown.tm_hour) << 29;
  result |= to_bcd(breakdown.tm_mday) << 36;
  result |= to_bcd(breakdown.tm_wday ? breakdown.tm_wday : 7) << 42;
  result |= to_bcd(breakdown.tm_mon + 1) << 45;
  result |= to_bcd(breakdown.tm_year % 100) << 50;

  result |= parity(result, 21, 27) << 28;
  result |= parity(result, 29, 34) << 35;
  result |= parity(result, 36, 57) << 58;

  return result;
}

static time_t TruncateTo(time_t t, int v) { return t - t % v; }

static void WaitUntil(const struct timespec &ts) {
  clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);
}

static void StartCarrier(GPIO *gpio) {
  double f = gpio->StartClock(77500);
  fprintf(stderr, "Approximating carrier with %.3f Hz\n", f);
}

static void Attenuate(GPIO *gpio, bool do_attenutate) {
  // To attenuate, we pull down a GPIO, otherwise we go into high impedance.
  if (do_attenutate) {
    gpio->RequestOutput(kAttenuationGPIOBit);
    gpio->ClearBits(kAttenuationGPIOBit);
  } else {
    gpio->RequestInput(kAttenuationGPIOBit);   // High-Z
  }
}

int main() {
  GPIO gpio;
  if (!gpio.Init()) {
    fprintf(stderr, "Need to be root\n");
    return 1;
  }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // Make sure the kernel knows that we're serious about accuracy of sleeps.
  struct sched_param sp;
  sp.sched_priority = 99;
  sched_setscheduler(0, SCHED_FIFO, &sp);

  StartCarrier(&gpio);

  // If tarted in the middle of a minute, we quickly slide down to current sec.
  const time_t first_minute = TruncateTo(time(NULL),  60);
  struct timespec target_wait;
  for (time_t minute_start = first_minute; !interrupted; minute_start += 60) {
    // We're sending the data for the minute leading up to, so +60s away:
    const MinuteData time_bits = CreateDCF77Bits(minute_start + 60);

    // Only up to 58: There is not attenuation on second 59 for synchronization.
    for (int second = 0; second < 59 && !interrupted; ++second) {
      const bool second_bit = time_bits & (1LL << second);
      target_wait.tv_sec = minute_start + second;
      target_wait.tv_nsec = 0;
      WaitUntil(target_wait);
      if (interrupted) break;
      target_wait.tv_nsec = second_bit ? kOneBitNanos : kZeroBitNanos;
      Attenuate(&gpio, true);
      WaitUntil(target_wait);
      Attenuate(&gpio, false);
      fprintf(stderr, ":%02d %d\b\b\b\b\b", second, second_bit);
    }
  }

  fprintf(stderr, "\nReceived interrupt (sig %d). Exiting.\n", interrupted);
  gpio.StopClock();
}
