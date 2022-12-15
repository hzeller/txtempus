// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a Low Frequency/Long Wave time signal transmitter.
// Copyright (C) 2022 Domokos Molnar <domokos@gmail.com>
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

#ifndef SUNXIH3_HARDWARE_CONTROL_IMPLEMENTATION_H
#define SUNXIH3_HARDWARE_CONTROL_IMPLEMENTATION_H

#include "hardware-control.h"
#include <stdint.h>
#include <stdio.h>
#include <map>


// -- Implementation for Allwinner H3 SOC -- https://linux-sunxi.org/Category:H3_Devices
// Tested on OrangePI PC
class HardwareControl::Implementation {
 public:

  // Initialize
  bool Init();

  // Set frequency output on PA5 as close as possible to the requested one.
  // Returns the approximate frequency it could configure or -1 if that was
  // not possible.
  // You need to identify PA5 on your board on the OrngePI PC it is the middle pin of the debug UART
  double StartClock(double frequency_hertz);
  void StopClock();

  // Switches the output of the currently running clock.
  void EnableClockOutput(bool enable);

  // Sets the power of the output by pulling low the voltage divider's mid point
  void SetTxPower(CarrierPower power);

 private:
  // PWM control register offsets 
  std::map<std::string, int>PwmCtrlRegMap;
  // PWM period register offsets
  std::map<std::string, int>PwmCh0PeriodMap;
  // PWM clock presaclers
  std::map<int, int>PwmCh0Prescale;

  // PA6 and PA5 are used
  enum gpio_pin {
      PA5,
      PA6
      };

  struct pwm_params {
      int period;
      int prescale;
      double frequency;
      };
  
  // Registers of the board
  volatile uint32_t *pwmreg = nullptr;
  volatile uint32_t *pioreg = nullptr;
  uint32_t *map_register(off_t register_offset);

  // Setup pin as output or LOW-Z input
  void SetOutput(gpio_pin pin);
  void SetInput(gpio_pin pin);

  // Configure pins
  void DisablePaPulls(void);

  // Calculate PWM parameters based on requested output frequency
  pwm_params CalculatePWMParams(double requested_freq);
  void WaitPwmBusy();

};

using H3BOARD = HardwareControl::Implementation;

#endif // SUNXIH3_HARDWARE_CONTROL_IMPLEMENTATION_H
