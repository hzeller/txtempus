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

#include "time-signal-source.h"

// WWVB uses BCD, but always has a zero bit between the digits.
// So let's call it 'padded' BCD.
static uint64_t to_padded5_bcd(int n) {
  return (((n / 100) % 10) << 10) | (((n / 10) % 10) << 5) | (n % 10);
}

static uint64_t is_leap_year(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

void WWVBTimeSignalSource::PrepareMinute(time_t t) {
  struct tm breakdown;
  struct tm tomorrow;
  time_t tomorrow_t;
  gmtime_r(&t, &breakdown);  // Time transmission is always in UTC.

  // https://en.wikipedia.org/wiki/WWVB
  // The WWVB format uses Bit-Bigendianess, so we'll start with the first
  // bit left in our integer in bit 59.
  time_bits_ = 0;  // All the unused bits are zero.
  time_bits_ |= to_padded5_bcd(breakdown.tm_min) << (59 - 8);
  time_bits_ |= to_padded5_bcd(breakdown.tm_hour) << (59 - 18);
  time_bits_ |= to_padded5_bcd(breakdown.tm_yday + 1) << (59 - 33);
  time_bits_ |= to_padded5_bcd(breakdown.tm_year % 100) << (59 - 53);
  time_bits_ |= is_leap_year(breakdown.tm_year + 1900) << (59 - 55);

  // Need local time for now and tomorrow to determine DST status.
  localtime_r(&t, &breakdown);
  tomorrow_t = t + 86400;
  localtime_r(&tomorrow_t, &tomorrow);
  time_bits_ |= (tomorrow.tm_isdst ? 1 : 0) << (59 - 57);
  time_bits_ |= (breakdown.tm_isdst ? 1 : 0) << (59 - 58);
}

TimeSignalSource::SecondModulation
WWVBTimeSignalSource::GetModulationForSecond(int sec) {
  if (sec == 0 || sec % 10 == 9 || sec > 59)
    return {{CarrierPower::LOW, 800}, {CarrierPower::HIGH, 0}};
  const bool bit = time_bits_ & (1LL << (59 - sec));
  return {{CarrierPower::LOW, bit ? 500 : 200}, {CarrierPower::HIGH, 0}};
}
