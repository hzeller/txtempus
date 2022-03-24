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

#define __STDC_FORMAT_MACROS
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

// -- Implementation for Raspberry Pi Series --

// Raspberry 1 and 2 have different base addresses for the periphery
#define BCM2708_PERI_BASE        0x20000000
#define BCM2709_PERI_BASE        0x3F000000
#define BCM2711_PERI_BASE        0xFE000000

#define GPIO_REGISTER_OFFSET     0x00200000
#define CLOCK_REGISTER_OFFSET    0x00101000

#define REGISTER_BLOCK_SIZE (4*1024)

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x).
#define INP_GPIO(g)  *(gpio_port_+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g)  *(gpio_port_+((g)/10)) |=  (1<<(((g)%10)*3))
#define ALT0_GPIO(g) *(gpio_port_+((g)/10)) |=  (4<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

// Clock control
#define CLK_PASSWD  (0x5A << 24)
#define CLK_CTL_MASH(x) ((x)<<9)
#define CLK_CTL_BUSY    (1<<7)
#define CLK_CTL_KILL    (1<<5)
#define CLK_CTL_ENAB    (1<<4)
#define CLK_CTL_SRC(x)  ((x)<<0)

#define CLK_DIV_DIVI(x) ((x)<<12)
#define CLK_DIV_DIVF(x) ((x)<< 0)

#define CLK_CMGP0_CTL 28
#define CLK_CMGP0_DIV 29
#define CLK_CMGP1_CTL 30
#define CLK_CMGP1_DIV 31
#define CLK_CMGP2_CTL 32
#define CLK_CMGP2_DIV 33

/*static*/const uint32_t GPIO::kAttenuationGPIOBit = (1<<17);

/*static*/ const uint32_t GPIO::kValidBits
= ((1 <<  0) | (1 <<  1) | // RPi 1 - Revision 1 accessible
   (1 <<  2) | (1 <<  3) | // RPi 1 - Revision 2 accessible
   (1 <<  4) | (1 <<  7) | (1 << 8) | (1 <<  9) |
   (1 << 10) | (1 << 11) | (1 << 14) | (1 << 15)| (1 <<17) | (1 << 18) |
   (1 << 22) | (1 << 23) | (1 << 24) | (1 << 25)| (1 << 27) |
   // support for A+/B+ and RPi2 with additional GPIO pins.
   (1 <<  5) | (1 <<  6) | (1 << 12) | (1 << 13) | (1 << 16) |
   (1 << 19) | (1 << 20) | (1 << 21) | (1 << 26)
);

uint32_t GPIO::RequestOutput(uint32_t outputs) {
  assert(gpio_port_);  // Call Init() first.
  outputs &= kValidBits;     // Sanitize: only bits on GPIO header allowed.
  for (uint32_t b = 0; b <= 27; ++b) {
    if (outputs & (1 << b)) {
      INP_GPIO(b);   // for writing, we first need to set as input.
      OUT_GPIO(b);
    }
  }
  return outputs;
}

uint32_t GPIO::RequestInput(uint32_t inputs) {
  assert(gpio_port_);  // Call Init() first.
  inputs &= kValidBits;     // Sanitize: only bits on GPIO header allowed.
  for (uint32_t b = 0; b <= 27; ++b) {
    if (inputs & (1 << b)) {
      INP_GPIO(b);
    }
  }
  return inputs;
}

// BCM2835-ARM-Peripherals.pdf, page 105 onwards.
double GPIO::StartClock(double requested_freq) {
  // Figure out best clock source to get closest to the requested
  // frequency with MASH=1. We check starting from the highest frequency to
  // find lowest jitter opportunity first.

  static const struct { int src; double frequency; } kClockSources[] = {
    { 5, 1000.0e6 },   // PLLC
    { 6,  500.0e6 },   // PLLD
    { 7,  216.0e6 },   // HDMI  <- this can be problematic if monitor connected
    { 1,   19.2e6 },   // regular oscillator
  };

  int divI = -1;
  int divF = -1;
  int best_clock_source = -1;
  double smallest_error_so_far = 1e9;
  for (size_t i = 0; i < sizeof(kClockSources)/sizeof(kClockSources[0]); ++i) {
    double division = kClockSources[i].frequency / requested_freq;
    if (division < 2 || division > 4095) continue;
    int test_divi = (int) division;
    int test_divf = (division - test_divi) * 1024;
    double freq = kClockSources[i].frequency / (test_divi + test_divf/1024.0);
    double error = fabsl(requested_freq - freq);
    if (error >= smallest_error_so_far) continue;
    smallest_error_so_far = error;
    best_clock_source = i;
    divI = test_divi;
    divF = test_divf;
  }

  if (divI < 0)
    return -1.0;  // Couldn't find any suitable clock.

  assert(divI >= 2 && divI < 4096 && divF >= 0 && divF < 4096);

  StopClock();

  const uint32_t ctl = CLK_CMGP0_CTL;
  const uint32_t div = CLK_CMGP0_DIV;
  const uint32_t src = kClockSources[best_clock_source].src;
  const uint32_t mash = 1;  // Good approximation, low jitter.

  clock_reg_[div] = CLK_PASSWD | CLK_DIV_DIVI(divI) | CLK_DIV_DIVF(divF);
  usleep(10);

  clock_reg_[ctl] = CLK_PASSWD | CLK_CTL_MASH(mash) | CLK_CTL_SRC(src);
  usleep(10);

  clock_reg_[ctl] |= CLK_PASSWD | CLK_CTL_ENAB;

  EnableClockOutput(true);

#if 0
  // There have been reports of different clock source frequencies. This
  // helps figuring out which source was picked.
  fprintf(stderr, "Choose clock %d at %gHz / %.3f = %.3f\n",
          kClockSources[best_clock_source].src,
          kClockSources[best_clock_source].frequency,
          divI + divF/1024.0,
          kClockSources[best_clock_source].frequency / (divI + divF/1024.0));
#endif

  return kClockSources[best_clock_source].frequency / (divI + divF/1024.0);
}

void GPIO::StopClock() {
  const uint32_t ctl = CLK_CMGP0_CTL;
  clock_reg_[ctl] = CLK_PASSWD | CLK_CTL_KILL;

  // Wait until clock confirms not to be busy anymore.
  while (clock_reg_[ctl] & CLK_CTL_BUSY) {
    usleep(10);
  }
  EnableClockOutput(false);
}

void GPIO::EnableClockOutput(bool on) {
  if (on) {
    ALT0_GPIO(4);  // Pinmux GPIO4 into outputting clock.
  } else {
    INP_GPIO(4);
  }
}

// We are not interested in the _exact_ model, just good enough to determine
// What to do.
enum RaspberryPiModel {
  PI_MODEL_1,
  PI_MODEL_2,
  PI_MODEL_3,
  PI_MODEL_4
};

static int ReadFileToBuffer(char *buffer, size_t size, const char *filename) {
  const int fd = open(filename, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t r = read(fd, buffer, size - 1); // assume one read enough
  buffer[r >= 0 ? r : 0] = '\0';
  close(fd);
  return r;
}

static RaspberryPiModel DetermineRaspberryModel() {
  char buffer[4096];
  if (ReadFileToBuffer(buffer, sizeof(buffer), "/proc/cpuinfo") < 0) {
    fprintf(stderr, "Reading cpuinfo: Could not determine Pi model\n");
    return PI_MODEL_3;  // safe guess fallback.
  }
  static const char RevisionTag[] = "Revision";
  const char *revision_key;
  if ((revision_key = strstr(buffer, RevisionTag)) == nullptr) {
    fprintf(stderr, "non-existent Revision: Could not determine Pi model\n");
    return PI_MODEL_3;
  }
  unsigned int pi_revision;
  if (sscanf(index(revision_key, ':') + 1, "%x", &pi_revision) != 1) {
    fprintf(stderr, "Unknown Revision: Could not determine Pi model\n");
    return PI_MODEL_3;
  }

  // https://www.raspberrypi.org/documentation/hardware/raspberrypi/revision-codes/README.md
  const unsigned pi_type = (pi_revision >> 4) & 0xff;
  switch (pi_type) {
  case 0x00: /* A */
  case 0x01: /* B, Compute Module 1 */
  case 0x02: /* A+ */
  case 0x03: /* B+ */
  case 0x05: /* Alpha ?*/
  case 0x06: /* Compute Module1 */
  case 0x09: /* Zero */
  case 0x0c: /* Zero W */
    return PI_MODEL_1;

  case 0x04:  /* Pi 2 */
  case 0x12:  /* Zero W 2 (behaves close to Pi 2) */
    return PI_MODEL_2;

  case 0x11: /* Pi 4 */
    // A first test did not seem to work. Maybe the registers changed ?
    fprintf(stderr, "Note: Frequency generation is known to not work on Pi4; "
            "Use older Pis for now.\n");
    return PI_MODEL_4;

  default:  /* a bunch of versions representing Pi 3 */
    return PI_MODEL_3;
  }
}

static RaspberryPiModel GetPiModel() {
  static RaspberryPiModel pi_model = DetermineRaspberryModel();
  return pi_model;
}

static uint32_t *mmap_bcm_register(off_t register_offset) {
  off_t base = BCM2709_PERI_BASE;  // safe fallback guess.
  switch (GetPiModel()) {
  case PI_MODEL_1: base = BCM2708_PERI_BASE; break;
  case PI_MODEL_2: base = BCM2709_PERI_BASE; break;
  case PI_MODEL_3: base = BCM2709_PERI_BASE; break;
  case PI_MODEL_4: base = BCM2711_PERI_BASE; break;
  }

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
                     base + register_offset // Offset to bcm register
                     );
  close(mem_fd);

  if (result == MAP_FAILED) {
    perror("mmap error: ");
    fprintf(stderr, "MMapping from base 0x%lx, offset 0x%lx\n",
            base, register_offset);
    return nullptr;
  }
  return result;
}

bool GPIO::Init() {
  gpio_port_ = mmap_bcm_register(GPIO_REGISTER_OFFSET);
  if (gpio_port_ == nullptr) {
    fprintf(stderr, "Need to be root\n");
    return false;
  }
  gpio_set_bits_ = gpio_port_ + (0x1C / sizeof(uint32_t));
  gpio_clr_bits_ = gpio_port_ + (0x28 / sizeof(uint32_t));
  clock_reg_ = mmap_bcm_register(CLOCK_REGISTER_OFFSET);

  return gpio_port_ != MAP_FAILED && clock_reg_ != MAP_FAILED;
}


void GPIO::SetTxPower(CarrierPower power) {
  switch (power) {
  case CarrierPower::OFF:
    EnableClockOutput(false);
    break;
  case CarrierPower::LOW:
    RequestOutput(kAttenuationGPIOBit);  // Pull down.
    ClearBits(kAttenuationGPIOBit);
    EnableClockOutput(true);
    break;
  case CarrierPower::HIGH:
    RequestInput(kAttenuationGPIOBit);   // High-Z
    EnableClockOutput(true);
    break;
  }
}
