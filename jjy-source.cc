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

// Similar to WWVB, JJY uses BCD, but usually has a zero bit between the digits.
// So let's call it 'padded' BCD.
static uint64_t to_padded5_bcd(int n) {
  return (((n / 100) % 10) << 10) | (((n / 10) % 10) << 5) | (n % 10);
}

// Regular BCD for the year encoding.
static uint64_t to_bcd(int n) {
  return (((n / 100) % 10) << 8) | (((n / 10) % 10) << 4) | (n % 10);
}

static uint64_t parity(uint64_t d, uint8_t from, uint8_t to_including) {
  uint8_t result = 0;
  for (int bit = from; bit <= to_including; ++bit) {
    if (d & (1LL << bit)) result++;
  }
  return result & 0x1;
}

void JJYTimeSignalSource::PrepareMinute(time_t t) {
  struct tm breakdown;
  localtime_r(&t, &breakdown);  // If in JP, this is Japan Standard Time

  // https://en.wikipedia.org/wiki/JJY
  // The JJY format uses Bit-Bigendianess, so we'll start with the first
  // bit left in our integer in bit 59.
  time_bits_ = 0;  // All the unused bits are zero.
  time_bits_ |= to_padded5_bcd(breakdown.tm_min) << (59 - 8);
  time_bits_ |= to_padded5_bcd(breakdown.tm_hour) << (59 - 18);
  time_bits_ |= to_padded5_bcd(breakdown.tm_yday + 1) << (59 - 33);
  time_bits_ |= to_bcd(breakdown.tm_year % 100) << (59 - 48);
  time_bits_ |= to_bcd(breakdown.tm_wday) << (59 - 52);

  time_bits_ |= parity(time_bits_, 59-18, 59-12) << (59 - 36);  // PA1
  time_bits_ |= parity(time_bits_, 59-8, 59-1) << (59 - 37);    // PA2

  // There is a different 'service announcement' encoding in minute 15 and 45,
  // but let's just ignore that for now. Consumer clocks probably don't care.
}

TimeSignalSource::SecondModulation
JJYTimeSignalSource::GetModulationForSecond(int sec) {
  if (sec == 0 || sec % 10 == 9 || sec > 59)
    return {{CarrierPower::HIGH, 200}, {CarrierPower::LOW, 0}};
  const bool bit = time_bits_ & (1LL << (59 - sec));
  return {{CarrierPower::HIGH, bit ? 500 : 800}, {CarrierPower::LOW, 0}};
}
