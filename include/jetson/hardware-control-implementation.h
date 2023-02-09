// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
// Copyright (C) 2022 Jueon Park <bluegbgb@gmail.com>
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

#ifndef JETSON_HARDWARE_CONTROL_IMPLEMENTATION_H
#define JETSON_HARDWARE_CONTROL_IMPLEMENTATION_H

#define __STDC_FORMAT_MACROS

#include "hardware-control.h"
#include <iostream>
#include <JetsonGPIO.h>

// -- Implementation for Nvidia Jetson Series --

class HardwareControl::Implementation {
 private:
  int carrierPin;
  int attenuationPin;
  bool isInitialized = false;
  bool isOn = false;
  std::unique_ptr<GPIO::PWM> pwm;

 public:
  bool Init() {
    if (isInitialized)
      return carrierPin > 0;

    isInitialized = true;

    if (GPIO::model == "JETSON_TX1" || GPIO::model == "JETSON_TX2") {
      // cannot control pwm through JetsonGPIO library
      std::cerr << "Your model(" << GPIO::model << ") is not supported." << std::endl;
      return false;
    }
    else if (GPIO::model == "JETSON_XAVIER" || GPIO::model == "CLARA_AGX_XAVIER" || GPIO::model == "JETSON_ORIN") {
      // pwm pin : 15, 18
      carrierPin = 18;
      attenuationPin = 16;
    }
    else {
      // pwm pin : 32, 33
      carrierPin = 33;
      attenuationPin = 35;
    }

    GPIO::setmode(GPIO::BOARD);
    GPIO::setup(carrierPin, GPIO::OUT);
    GPIO::setup(attenuationPin, GPIO::OUT);

    return true;
  }


  double StartClock(double frequency_hertz) {
    if (pwm == nullptr)
      pwm = std::unique_ptr<GPIO::PWM>(new GPIO::PWM(carrierPin, frequency_hertz));

    pwm->start(50.0); // duty cycle: 50%
    isOn = true;

    return frequency_hertz;
  }

  void StopClock() {
    if (pwm) {
      pwm->stop();
      isOn = false;
    }
  }

  void EnableClockOutput(bool on) {
    if(on == isOn)
      return;

    if (on) {
      pwm->start(50.0);
      isOn = true;
    }
    else {
      StopClock();
    }
  }

  void SetTxPower(CarrierPower power) {
    switch (power) {
    case CarrierPower::LOW:
      EnableClockOutput(true);
      ApplyAttenuation();
      break;
    case CarrierPower::OFF:
      EnableClockOutput(false);
      break;
    case CarrierPower::HIGH:
      EnableClockOutput(true);
      StopAttenuation();
      break;
    }
  }

  void ApplyAttenuation() {
    GPIO::output(attenuationPin, GPIO::HIGH);
  }

  void StopAttenuation() {
    GPIO::output(attenuationPin, GPIO::LOW);
  }
};


#endif // JETSON_HARDWARE_CONTROL_IMPLEMENTATION_H
