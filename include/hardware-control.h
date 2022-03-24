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

#ifndef HARDWARE_CONTROL_H
#define HARDWARE_CONTROL_H

#include <memory>
#include "carrier-power.h"

class HardwareControl {
 public:
  // To add a new platform support: 
  // 1. Add include/[new_platform_name]/hardware-control-implementation.h file and
  //    implement "HardwareControl::Implementation" class for the platform.
  // 2. Add cmake/[new_platform_name]-control.cmake file and set platform-specific configuration:
  //    SRC_FILES: source files
  //    INCLUDE_DIRS: include directories
  //    PLATFORM_DEPENDENCIES: dependencies 
  // 3. Append [new_platform_name] to "SUPPORTED_PLATFORMS" in CMakeLists.txt.
  class Implementation;
  
  HardwareControl();
  ~HardwareControl();

  // Initialize before use. Returns 'true' if successful, 'false' otherwise
  // (e.g. due to a permission problem).
  bool Init();

  // Set frequency output as close as possible to the requested one.
  // Returns the approximate frequency it could configure or -1 if that was
  // not possible.
  double StartClock(double frequency_hertz);
  void StopClock();

  // Switches the output of the currently running clock.
  void EnableClockOutput(bool b);

  void SetTxPower(CarrierPower power);

 private:
  // pimpl idiom to hide platform-specific information
  std::unique_ptr<Implementation> pimpl;
};

#endif  // HARDWARE_CONTROL_H
