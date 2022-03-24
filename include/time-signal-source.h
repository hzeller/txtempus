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

#ifndef TIMETRANSMITTER_CLOCKGEN_H
#define TIMETRANSMITTER_CLOCKGEN_H

#include <time.h>
#include <stdint.h>
#include <vector>
#include "carrier-power.h"

struct ModulationDuration {
  CarrierPower power;
  int duration_ms;
};

// Base class for different types of time signal sources.
class TimeSignalSource {
public:
  typedef std::vector<ModulationDuration> SecondModulation;

  virtual ~TimeSignalSource(){}

  // Carrier frequency of this particular time source.
  virtual int GetCarrierFrequencyHz() const = 0;

  // Called once at the beginning of a minute starting with
  // the transmission to prepare the necessary data bits to be
  // sent.
  // Note, some time singals are sent to be valid when the
  // end of the minute is reached, so these implementations would need to
  // add 60 seconds to this.
  // The provided time is guaranteed to be an even minute, i.e. divisible by 60.
  virtual void PrepareMinute(time_t t) = 0;

  // Returns a vector of modulation transitions to be sent out for the
  // particular second within the minute mentioned in PrepareMinute().
  // The method should return a sequence of power-levels and durations in
  // milliseconds. The last transition stays for the remainder of the second,
  // so it is good practice to set the last duration to zero to auto-fill.
  // e.g. {{CarrierPower::HIGH, 200},{CarrierPower::LOW, 0}}
  //
  // All numbers must add up to less or equal 1000ms.
  //
  // Value of second can be between 0..59, or up to 60 with leap seconds
  // (leap seconds not implemented yet).
  virtual SecondModulation GetModulationForSecond(int second) = 0;
};


// -- Various implementations.
class DCF77TimeSignalSource : public TimeSignalSource {
public:
  int GetCarrierFrequencyHz() const final { return 77500; }
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

private:
  uint64_t time_bits_;
};

class WWVBTimeSignalSource : public TimeSignalSource {
public:
  int GetCarrierFrequencyHz() const final { return 60000; }
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

private:
  uint64_t time_bits_;
};

class JJYTimeSignalSource : public TimeSignalSource {
public:
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

private:
  uint64_t time_bits_;
};

class JJY60TimeSignalSource : public JJYTimeSignalSource {
  int GetCarrierFrequencyHz() const final { return 60000; }
};
class JJY40TimeSignalSource : public JJYTimeSignalSource {
  int GetCarrierFrequencyHz() const final { return 40000; }
};

class MSFTimeSignalSource : public TimeSignalSource {
public:
  int GetCarrierFrequencyHz() const final { return 60000; }
  void PrepareMinute(time_t t) final;
  SecondModulation GetModulationForSecond(int second) final;

private:
  uint64_t a_bits_, b_bits_;
};

#endif // TIMETRANSMITTER_CLOCKGEN_H
