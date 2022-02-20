// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
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

#include "hardware-control.h"
#include "hardware-control-implementation.h"

HardwareControl::HardwareControl() : pimpl(std::unique_ptr<Implementation>(new Implementation())){}
HardwareControl::~HardwareControl() = default;
bool HardwareControl::Init() { return pimpl->Init(); }
double HardwareControl::StartClock(double frequency_hertz) { return pimpl->StartClock(frequency_hertz); }
void HardwareControl::StopClock() { pimpl->StopClock(); }
void HardwareControl::EnableClockOutput(bool b) { pimpl->EnableClockOutput(b); }
void HardwareControl::SetTxPower(CarrierPower power) { pimpl->SetTxPower(power); }
