// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// DCF77 Meteotime weather encryption
//
// Ported from osmocom-analog project:
// https://github.com/osmocom/osmocom-analog
// Original DCF77 weather implementation (C) 2022 Andreas Eversberg <jolly@eversberg.eu>
//
// Original encryption code based on:
// https://github.com/FroggySoft/AlarmClock/blob/master/dcf77.cpp
// https://github.com/tobozo/esp32-dcf77-weatherman/blob/master/dcf77.cpp

#ifndef DCF77_WEATHER_CRYPT_H
#define DCF77_WEATHER_CRYPT_H

#include <cstdint>

// Encode weather data using modified DES
// weather: 22-bit weather data (bits 0-21)
// key: 40-bit key derived from time
// Returns: 40-bit encrypted cipher
uint64_t weather_encode(uint32_t weather, uint64_t key);

// Decode encrypted weather data (for testing)
// cipher: 40-bit encrypted data
// key: 40-bit key derived from time  
// Returns: 22-bit weather data or -1 on checksum error
int32_t weather_decode(uint64_t cipher, uint64_t key);

#endif  // DCF77_WEATHER_CRYPT_H
