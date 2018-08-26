// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
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

#ifndef RPI_GPIO_H
#define RPI_GPIO_H

#include <stdint.h>

class GPIO {
 public:
  // Available bits that actually have pins.
  static const uint32_t kValidBits;

  GPIO();

  // Initialize before use. Returns 'true' if successful, 'false' otherwise
  // (e.g. due to a permission problem).
  bool Init();

  // Initialize outputs for given bits.
  // Returns the bits that are physically available and could be set for output.
  uint32_t RequestOutput(uint32_t outputs);

  // Request given bits for input.
  // Returns the bits that available and could be reserved.
  uint32_t RequestInput(uint32_t inputs);

  // Set the bits that are '1' in the output. Leave the rest untouched.
  inline void SetBits(uint32_t value) { *gpio_set_bits_ = value; }

  // Clear the bits that are '1' in the output. Leave the rest untouched.
  inline void ClearBits(uint32_t value) { *gpio_clr_bits_ = value; }

  // Set frequency output on GPIO4 as close as possible to the requested one.
  // Returns the approximate frequency it could configure or -1 if that was
  // not possible.
  double StartClock(double frequency_hertz);
  void StopClock();

 private:
  volatile uint32_t *gpio_port_;
  volatile uint32_t *gpio_set_bits_;
  volatile uint32_t *gpio_clr_bits_;
  volatile uint32_t *clock_reg_;
};

#endif  // RPI_GPIO_H
