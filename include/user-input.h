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

#ifndef USER_INPUT_H
#define USER_INPUT_H

#include <time.h>
#include <string>

class UserInput {
public:
  UserInput(int argc, char *argv[]);
  std::string chosen_time;
  std::string service;
  int zone_offset = 0;
  int ttl;
  bool verbose = false;
  bool dryrun = false;
  bool show_help = false;

private:
  void Parse(int argc, char *argv[]);
};

#endif
