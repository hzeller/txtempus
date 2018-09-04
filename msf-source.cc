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
static uint64_t odd_parity(uint64_t d, uint8_t from, uint8_t to_including) {
  uint8_t result = 0;
  for (int bit = from; bit <= to_including; ++bit) {
    if (d & (1LL << bit)) result++;
  }
  return (result & 0x1) == 0;
}

void MSFTimeSignalSource::PrepareMinute(time_t t) {
  t += 60;  // We're sending the _upcoming_ minute.
  struct tm breakdown;
  localtime_r(&t, &breakdown);  // Local time, e.g. British standard time.

  // https://en.wikipedia.org/wiki/Time_from_NPL_(MSF)
  // The MSF format uses Bit-Bigendianess, so we'll start with the first
  // bit left in our integer in bit 59.

  a_bits_ = 0b1111110; // Last bits of a, identifying upcoming minute transition
  a_bits_ |= to_bcd(breakdown.tm_year % 100) << (59 - 24);
  a_bits_ |= to_bcd(breakdown.tm_mon + 1) << (59 - 29);
  a_bits_ |= to_bcd(breakdown.tm_mday) << (59 - 35);
  a_bits_ |= to_bcd(breakdown.tm_wday) << (59 - 38);
  a_bits_ |= to_bcd(breakdown.tm_hour) << (59 - 44);
  a_bits_ |= to_bcd(breakdown.tm_min) << (59 - 51);

  b_bits_ = 0;
  // First couple of bits: DUT; not being set.
  // (59 - 53): summer time warning. Not set.
  b_bits_ |= odd_parity(a_bits_, 59-24, 59-17) << (59 - 54); // Year parity
  b_bits_ |= odd_parity(a_bits_, 59-35, 59-25) << (59 - 55); // Day parity
  b_bits_ |= odd_parity(a_bits_, 59-38, 59-36) << (59 - 56); // Weekday parity
  b_bits_ |= odd_parity(a_bits_, 59-51, 59-39) << (59 - 57); // Time parity
  b_bits_ |= breakdown.tm_isdst << (59 - 58);
}

TimeSignalSource::SecondModulation
MSFTimeSignalSource::GetModulationForSecond(int second) {
  if (second == 0)
    return {{CarrierPower::OFF, 500}, {CarrierPower::HIGH, 0}};
  const bool a = a_bits_ & (1LL << (59 - second));
  const bool b = b_bits_ & (1LL << (59 - second));
  return { { CarrierPower::OFF, 100 },
           { a ? CarrierPower::OFF : CarrierPower::HIGH, 100 },
           { b ? CarrierPower::OFF : CarrierPower::HIGH, 100 },
           { CarrierPower::HIGH, 0}
         };
}
