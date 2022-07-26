// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// This is txtempus, a LF time signal transmitter.
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
//
// Time-signal simulating transmitter, supporting various types such
// as DCF77, WWVB, ... to be run on the Raspberry Pi.
// Make sure to stay within the regulation limits of HF transmissions!

#include "user-input.h"
#include <limits.h>
#include <getopt.h>

UserInput::UserInput(int argc, char *argv[])
    : chosen_time(""),
      service{},
      zone_offset(0),
      ttl(INT_MAX),
      verbose(false),
      dryrun(false),
      show_help(false),
      show_version(false) {
    Parse(argc, argv);
}


void UserInput::Parse(int argc, char *argv[]) {
  int option_flag = 0;
  struct option long_options[] =
  {
      {"version", no_argument, &option_flag, 1},
      {nullptr, 0, nullptr, 0}
  };

  constexpr auto short_options = "t:z:r:vs:hn";

  int option_index = 0;
  int opt{};

  while ((opt = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
    switch (opt) {
    case 0:   // long options
      if (option_flag == 1)
        show_version = true;
      break;

    // short options
    case 'v':
      verbose = true;
      break;
    case 't':
      chosen_time = optarg;
      break;
    case 'z':
      zone_offset = atoi(optarg);
      break;
    case 'r':
      ttl = atoi(optarg);
      break;
    case 's':
      service = optarg;
      break;
    case 'n':
      dryrun = true;
      verbose = true;
      ttl = 1;
      break;
    default:
      show_help = true;
      return;
    }
  }
}
