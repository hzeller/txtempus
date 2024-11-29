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
#include <iostream>

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
using namespace std;

bool debug = false;

// -- Implementation for Allwinner H3 boards --

// Allwiner H3 Orangepi Register Addresses
#define PAGESIZE_CORRECTOR 0x800
#define PWM_OFFSET 0xC00
#define REG_BASE 0x01C20800 - PAGESIZE_CORRECTOR

// Register offsets
#define PWM_CTRL_REG (PWM_OFFSET + 0x0 + PAGESIZE_CORRECTOR)/sizeof(uint32_t)
#define PWM_CH0_PERIOD (PWM_OFFSET + 0x04 + PAGESIZE_CORRECTOR)/sizeof(uint32_t)
#define PA_CFG0_REG (0x0 + PAGESIZE_CORRECTOR)/sizeof(uint32_t)
#define PA_PULL0_REG (0x1C + PAGESIZE_CORRECTOR)/sizeof(uint32_t)
#define PA_DATA_REG (0x10 + PAGESIZE_CORRECTOR)/sizeof(uint32_t)

// PA IO configure values
#define P_OUTPUT 0b001
#define P_INPUT 0b000
#define P_MASK 0b111
#define PA5_PWM0 0b011
#define P_PULL_UP 0b01
#define P_PULL_DOWN 0b10
#define P_PULL_DISABLE 0b00
#define P_PULL_MASK 0b11

// PA shift values
#define PA6_CFG_SHIFT 24
#define PA5_CFG_SHIFT 20
#define PA6_PULL_SHIFT 12 // Bits [2i+1:2i] (i=0~15) 
#define PA5_PULL_SHIFT 10 // Bits [2i+1:2i] (i=0~15)

// Amount of memory to map after registers to access all offsets
#define REGISTER_BLOCK_SIZE 2*4096*sizeof(uint32_t)

// PWM Base frequency - 24MHz
#define PWM_BASE_FREQUENCY 24e6

// PWM Control register default value - OFF
#define PWM_DEFAULT_OFF 0x0

bool H3BOARD::Init() {
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

  registers = map_register(REG_BASE);
  if(debug) cout << "Mapped\n";

  if (registers == nullptr || registers == nullptr) {
    fprintf(stderr, "Need to be root\n");
    return false;
  }

  if(registers != MAP_FAILED && registers != MAP_FAILED)
    ConfigurePins();
  if(debug) cout << "Pin configs done\n";

  return registers != MAP_FAILED && registers != MAP_FAILED;
}

// Disable pullups on PA6 and enable it os PA5
void H3BOARD::ConfigurePins(void) {
  uint32_t mask, value;
  assert(registers);  // Call Init() first.

  // Disable Pullup on PA6
  mask = P_PULL_MASK << PA6_PULL_SHIFT;
  value = P_PULL_DISABLE << PA6_PULL_SHIFT;
  registers[PA_PULL0_REG] = (registers[PA_PULL0_REG] & ~mask) | value; 

  // Enable Pullup on PA5
  mask = P_PULL_MASK << PA5_PULL_SHIFT;
  value = P_PULL_UP << PA5_PULL_SHIFT;
  registers[PA_PULL0_REG] = (registers[PA_PULL0_REG] & ~mask) | value; 

  // Setup PA5 as PWM0 output
  mask = P_MASK << PA5_CFG_SHIFT;
  value = PA5_PWM0 << PA5_CFG_SHIFT;
  registers[PA_CFG0_REG] = (registers[PA_CFG0_REG] & ~mask) | value;
}

// Set the pin as output - LoZ state - PA6 pulls down if set to zero
void H3BOARD::SetOutput(gpio_pin pin) {
  uint32_t shift, mask, value;
  assert(registers);  // Call Init() first.

  shift = pin == PA6 ? PA6_CFG_SHIFT : PA5_CFG_SHIFT;
  mask = P_MASK << shift;
  value = P_OUTPUT << shift;

  registers[PA_CFG0_REG] = (registers[PA_CFG0_REG] & ~mask) | value;

  // Write zero to PA6 to make sure we pull it down
  if (pin == PA6)
    registers[PA_DATA_REG] |= 0b1 << 6;
}

// Set the pin as Input - HiZ state
void H3BOARD::SetInput(gpio_pin pin) {
  uint32_t shift, mask, value;
  assert(registers);  // Call Init() first.

  shift = pin == PA6 ? PA6_CFG_SHIFT : PA5_CFG_SHIFT;
  mask = P_MASK << shift;
  value = P_INPUT << shift;

  registers[PA_CFG0_REG] = (registers[PA_CFG0_REG] & ~mask) | value; 
}

void H3BOARD::EnableClockOutput(bool enable) {
  uint32_t mask;
  assert(registers);  // Call Init() first.

  mask = 0b1 << PWM_CH0_EN;
  if(enable) {
    registers[PWM_CTRL_REG] |= mask;
  } else {
    registers[PWM_CTRL_REG] &= ~mask;
  }
}

H3BOARD::pwm_params H3BOARD::CalculatePWMParams(double requested_freq) {
  pwm_params params;
  params.prescale = -1;
  unsigned error = 1e9;
  unsigned clk_freq, effective_freq, cycles;

  for (const auto& kx: PwmCh0Prescale) {
    clk_freq = (unsigned int) PWM_BASE_FREQUENCY /  kx.first;
    cycles = round((clk_freq / requested_freq)) - 1;
    effective_freq = (unsigned int) clk_freq / (cycles + 1);
    if(debug) fprintf(stderr,"Prescale: %d  Freq: %d\n",kx.first,effective_freq);
    if (error > fabs(requested_freq - effective_freq) && cycles > 1 && cycles < 65536) {
      params.prescale =  kx.first;
      params.period = cycles;
      params.frequency = effective_freq;
      error = fabs(requested_freq - effective_freq);
    }
  }
  return params;
}

// Setup the PWM
double H3BOARD::StartClock(double requested_freq) {
  pwm_params params;
  uint32_t pwm_control, pwm_period;

  // Get PWM parameters for the requested frequency 
  params =  CalculatePWMParams(requested_freq);
  assert(params.prescale != -1);
  if(debug) cout << "Frequency calculations done\n";

  // Start Gating clock
  pwm_control =   0b1 << SCLK_CH0_GATING |
                  PwmCh0Prescale[params.prescale] << PWM_CH0_PRESCAL;
  registers[PWM_CTRL_REG] = pwm_control;

  WaitPwmPeriodReady();

  // Setup PWM period to 50% duty cycle
  if(debug) fprintf(stderr,"Presacale: %d  Period: %d\n",params.prescale,params.period);
  pwm_period = params.period << PWM_CH0_ENTIRE_CYS |
               (params.period / 2) << PWM_CH0_ENTIRE_ACT_CYS;
  registers[PWM_CH0_PERIOD] = pwm_period;

  usleep(50);
  EnableClockOutput(true);
  if(debug) cout << "Output enabled\n";

  return params.frequency;
}

void H3BOARD::StopClock() {
  uint32_t pwm_control_mask;
  
  pwm_control_mask = 0b1 << PWM_CH0_EN;
  registers[PWM_CTRL_REG] &= ~pwm_control_mask;

  usleep (100);

  pwm_control_mask = 0b1 << SCLK_CH0_GATING;
  registers[PWM_CTRL_REG] &= ~pwm_control_mask;

  if(debug) cout << "Clock stopped\n";
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
                     PROT_READ|PROT_WRITE,  // Enable r/w on registers.
                     MAP_SHARED,
                     mem_fd,                // File to map
                     address                // Memory address before registers at a page boundary
                     );
  close(mem_fd);

  if (result == MAP_FAILED) {
    perror("mmap error: ");
    fprintf(stderr, "MMapping to address 0x%lx\n",address);
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
    SetOutput(PA6);
    EnableClockOutput(true);
    break;
  case CarrierPower::HIGH:
    SetInput(PA6);   // High-Z
    EnableClockOutput(true);
    break;
  }
}

// Wait until PWM register is not busy
void H3BOARD::WaitPwmPeriodReady() {
  if(debug) cout << "Waiting for PWM period register availability\n";   
  while (registers[PWM_CTRL_REG] & (0b1 << PWM0_RDY))
    usleep(10);
}