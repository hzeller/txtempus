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

#include "time-signal-source.h"

static uint64_t to_bcd(uint8_t n) { return (((n / 10) % 10) << 4) | (n % 10); }
static uint64_t parity(uint64_t d, uint8_t from, uint8_t to_including) {
  uint8_t result = 0;
  for (int bit = from; bit <= to_including; ++bit) {
    if (d & (1LL << bit)) result++;
  }
  return result & 0x1;
}

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
  time_bits_ |= (1<<20);  // start time bit.
  time_bits_ |= to_bcd(breakdown.tm_min)  << 21;
  time_bits_ |= to_bcd(breakdown.tm_hour) << 29;
  time_bits_ |= to_bcd(breakdown.tm_mday) << 36;
  time_bits_ |= to_bcd(breakdown.tm_wday ? breakdown.tm_wday : 7) << 42;
  time_bits_ |= to_bcd(breakdown.tm_mon + 1) << 45;
  time_bits_ |= to_bcd(breakdown.tm_year % 100) << 50;

  time_bits_ |= parity(time_bits_, 21, 27) << 28;
  time_bits_ |= parity(time_bits_, 29, 34) << 35;
  time_bits_ |= parity(time_bits_, 36, 57) << 58;
}

TimeSignalSource::SecondModulation
DCF77TimeSignalSource::GetModulationForSecond(int second) {
  if (second >= 59)
    return {{CarrierPower::HIGH, 0}};  // Synchronization
  const bool bit = time_bits_ & (1LL << second);
  return {{CarrierPower::LOW, bit ? 200 : 100}, {CarrierPower::HIGH, 0}};
}
