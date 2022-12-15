// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
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

#include <inttypes.h>

#include "hardware-control.h"
#include "hardware-control-implementation.h"

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// -- Implementation for Allwinner H3 boards --

// Allwiner H3 Orangepi Register Addresses
#define PWM_BASE 0x01C21400
#define PIO_BASE 0x01C20800

// Register offsets
#define PWM_CH_CTRL 0x0
#define PWM_CH0_PERIOD 0x04
#define PA_CFG0_REG 0x0
#define PA_PULL0_REG 0x1C
#define PA_DATA_REG 0x10

// PA IO configure values
#define P_OUTPUT 0b001 
#define P_INPUT 0b000
#define P_MASK 0b111
#define PA5_PWM0 0b011
#define P_PULL_DISABLE 0b00
#define P_PULL_MASK 0b11

// PA shift values
#define PA6_CFG_SHIFT 24
#define PA5_CFG_SHIFT 20
#define PA6_PULL_SHIFT 12 // Bits [2i+1:2i] (i=0~15) 
#define PA5_PULL_SHIFT 10 // Bits [2i+1:2i] (i=0~15)

// Amount of memory to map after registers to access all offsets
#define REGISTER_BLOCK_SIZE 512

// PWM Base frequency - 24MHz
#define PWM_BASE_FREQUENCY 24e6

// PWM Control register default value - OFF
#define PWM_DEFAULT_OFF 0x0

bool H3BOARD::Init() {
// PWM control offsets
  PwmCtrlRegMap = {
    {"PWM0_RDY", 28}, 
    {"PWM0_BYPASS", 9}, 
    {"PWM_CH0_PUL_START", 8},
    {"PWM_CHANNEL0_MODE", 7},
    {"SCLK_CH0_GATING.", 6},
    {"PWM_CH0_ACT_STA.", 5},
    {"PWM_CH0_EN.", 4},
    {"PWM_CH0_PRESCAL", 0}};
// PWM period offsets
  PwmCh0PeriodMap = {
    {"PWM_CH0_ENTIRE_CYS", 16}, 
    {"PWM_CH0_ENTIRE_ACT_CYS", 0}};
// PWM presacaling values
  PwmCh0Prescale = {
    {120, 0b0000}, 
    {180, 0b0001},
    {360, 0b0011},
    {480, 0b0100},
    {12000, 0b1000},
    {24000, 0b1001},
    {48000, 0b1011},
    {72000, 0b1100},
    {1, 0b1111}};

  pwmreg = map_register(PWM_BASE);
  pioreg = map_register(PIO_BASE);

  if (pwmreg == nullptr || pioreg == nullptr) {
    fprintf(stderr, "Need to be root\n");
    return false;
  }

  if(pwmreg != MAP_FAILED && pioreg != MAP_FAILED)
    DisablePaPulls();

  return pwmreg != MAP_FAILED && pioreg != MAP_FAILED;
}

// Disable pullups on PA5 and PA6
// Disabled is the default but let's make sure
void H3BOARD::DisablePaPulls(void) {
  int shift, mask, value;
  assert(pioreg);  // Call Init() first.

  mask = P_PULL_MASK << PA6_PULL_SHIFT | P_PULL_MASK << PA5_PULL_SHIFT;
  value = P_PULL_DISABLE;

   pioreg[PA_PULL0_REG] = (pioreg[PA_PULL0_REG] & ~mask) | value; 
}

// Set the pin as output - LoZ state - PA6 pulls down if set to zero
void H3BOARD::SetOutput(gpio_pin pin) {
  int shift, mask, value;
  assert(pioreg);  // Call Init() first.

  shift = pin == PA6 ? PA6_CFG_SHIFT : PA5_CFG_SHIFT;
  mask = P_MASK << shift;
  value = P_OUTPUT << shift;

  pioreg[PA_CFG0_REG] = (pioreg[PA_CFG0_REG] & ~mask) | value;

  // Write zero to PA6 to make sure we pull it down
  if (pin == PA6)
    pioreg[PA_DATA_REG] |= 0b1 << 6;
}

// Set the pin as Input - HiZ state
void H3BOARD::SetInput(gpio_pin pin) {
  int shift, mask, value;
  assert(pioreg);  // Call Init() first.

  shift = pin == PA6 ? PA6_CFG_SHIFT : PA5_CFG_SHIFT;
  mask = P_MASK << shift;
  value = P_INPUT << shift;

  pioreg[PA_CFG0_REG] = (pioreg[PA_CFG0_REG] & ~mask) | value; 
}

void H3BOARD::EnableClockOutput(bool enable) {
  int mask, value;
  assert(pioreg);  // Call Init() first.
  
  if(enable) {
    mask = P_MASK << PA5_CFG_SHIFT;
    value = PA5_PWM0 << PA5_CFG_SHIFT;

    pioreg[PA_CFG0_REG] = (pioreg[PA_CFG0_REG] & ~mask) | value;
  } else {
    SetInput(PA5);
  }
}

H3BOARD::pwm_params H3BOARD::CalculatePWMParams(double requested_freq) {
  pwm_params params;
  params.prescale = -1;
  unsigned error = 1e9;
  unsigned clk_freq, effective_freq, cycles;

  for (const auto& kx: PwmCh0Prescale) {
    clk_freq = (unsigned int) PWM_BASE_FREQUENCY / (int) kx.first;
    cycles = (clk_freq / requested_freq) - 1;
    effective_freq = (unsigned int) clk_freq / (cycles + 1);
    if (error > fabs(requested_freq - effective_freq) && cycles > 0 && cycles < 65536) {
      params.prescale = kx.first;
      params.period = cycles;
      params.frequency = effective_freq;
    }
  }
  return params;
}


// Setup the PWM
double H3BOARD::StartClock(double requested_freq) {
  pwm_params params;
  int pwm_control, pwm_period;

  params =  CalculatePWMParams(requested_freq);
  assert(params.prescale != -1);

  StopClock();
  // Setup PWM period tpo 50% duty cycle
  pwm_period = params.period << PwmCh0PeriodMap["PWM_CH0_ENTIRE_CYS"] |
               (params.period / 2) << PwmCh0PeriodMap["PWM_CH0_ENTIRE_ACT_CYS"];
  pwmreg[PWM_CH0_PERIOD] = pwm_period;

  // Setup PWM control register
  WaitPwmBusy();
  pwm_control =  0b1 << PwmCtrlRegMap["PWM_CH0_PUL_START"] | 
                  0b1 << PwmCtrlRegMap["PWM_CH0_EN"] |
                  PwmCh0Prescale[params.prescale] << PwmCtrlRegMap["PWM_CH0_PRESCAL"];
  pwmreg[PWM_CH_CTRL] = pwm_control;

  EnableClockOutput(true);

  return params.frequency;
}

void H3BOARD::StopClock() {
  WaitPwmBusy();
  pwmreg[PWM_CH_CTRL] = PWM_DEFAULT_OFF;
  pwmreg[PWM_CH0_PERIOD] = PWM_DEFAULT_OFF;
  EnableClockOutput(false);
}

uint32_t *H3BOARD::map_register(off_t address) {

  int mem_fd;
  if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
    perror("can't open /dev/mem: ");
    return nullptr;
  }

  uint32_t *result =
    (uint32_t*) mmap(nullptr,               // Any adddress in our space will do
                     REGISTER_BLOCK_SIZE,   // Map length
                     PROT_READ|PROT_WRITE,  // Enable r/w on GPIO registers.
                     MAP_SHARED,
                     mem_fd,                // File to map
                     address                // Offset to bcm register
                     );
  close(mem_fd);

  if (result == MAP_FAILED) {
    perror("mmap error: ");
    fprintf(stderr, "MMapping to address 0x%lx\n",
            address);
    return nullptr;
  }
  return result;
}


void H3BOARD::SetTxPower(CarrierPower power) {
  switch (power) {
  case CarrierPower::OFF:
    EnableClockOutput(false);
    break;
  case CarrierPower::LOW:
    SetOutput(P6);
    EnableClockOutput(true);
    break;
  case CarrierPower::HIGH:
    SetInput(P6);   // High-Z
    EnableClockOutput(true);
    break;
  }
}

// Wait until PWM register is not busy
void H3BOARD::WaitPwmBusy() {
  while (pwmreg[PWM_CH_CTRL] & (0b1 << PwmCtrlRegMap["PWM0_RDY"])) {
    usleep(5);
  }
}