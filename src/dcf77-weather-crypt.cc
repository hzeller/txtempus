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
//
// This implements a modified DES cipher used by Meteotime for DCF77
// weather data encryption.

#include "dcf77-weather-crypt.h"

#include <cstdint>
#include <cstring>

namespace {

// Container for converting between 4 bytes and uint32
union ByteUInt {
  struct {
    uint8_t Byte0;
    uint8_t Byte1;
    uint8_t Byte2;
    uint8_t Byte3;
  } s;
  uint32_t FullUint;
};

// Bit pattern for 0D,0E from 0B-0D
static const uint32_t mUintArrBitPattern12[12] = {
    0x80000, 0x00010, 0x00008, 0x00100, 0x00080, 0x01000,
    0x00800, 0x10000, 0x08000, 0x00001, 0x00000, 0x00000};

// 12-15 from 16-19 (time)
static const uint32_t mUintArrBitPattern30_1[30] = {
    0x00000200, 0x00000020, 0x02000000, 0x00000000, 0x00000000, 0x00000080,
    0x40000000, 0x01000000, 0x04000000, 0x00000000, 0x00010000, 0x00000000,
    0x00400000, 0x00000010, 0x00200000, 0x00080000, 0x00004000, 0x00000000,
    0x00020000, 0x00100000, 0x00008000, 0x00000040, 0x00001000, 0x00000400,
    0x00000001, 0x80000000, 0x00000008, 0x00000002, 0x00040000, 0x10000000};

// Bit pattern for 12-15 from 1A (time2)
static const uint32_t mUintArrBitPattern30_2[30] = {
    0x00, 0x00, 0x00, 0x08, 0x20, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 12-14 from 1C-1E (result from F)
static const uint32_t mUintArrBitPattern20[20] = {
    0x000004, 0x002000, 0x008000, 0x400000, 0x000100, 0x100000, 0x000400,
    0x800000, 0x040000, 0x020000, 0x000008, 0x000200, 0x004000, 0x000002,
    0x001000, 0x080000, 0x000800, 0x200000, 0x010000, 0x000001};

// S-box lookup tables
static const uint64_t mByteArrLookupTable1C_1[8] = {
    0xBB0E22C573DFF76D, 0x90E9A1381C844A56, 0x648D280BD1BA9352,
    0x1CC5A7F0E97F364E, 0xC1773DB3AAE00C6F, 0x1488F62BD2995E45,
    0x1F7096D3B30BFCEE, 0x8142CA34A5582967};

static const uint64_t mByteArrLookupTable1C_2[8] = {
    0xAB3DFC7465E60E4F, 0x9711D85983C2BA20, 0xC51BD2584937017D,
    0x93FAE02F66B4AC8E, 0xB7CC43FF5866EB35, 0x822A99DD007114AE,
    0x4EB1F7701852AA9F, 0xD56BCC3D0483E926};

static const uint64_t mByteArrLookupTable1C_3[8] = {
    0x0A02000F06070D08, 0x030C0B050901040E, 0x0209050D0C0E0F08,
    0x06070B01000A0403, 0x08000D0F010C0306, 0x0B0409050A07020E,
    0x030D000C09060F0B, 0x010E080A02070405};

// Container for all cipher state during encryption/decryption.
// This is a modified DES cipher with 40-bit blocks and keys.
struct DataContainer {
  ByteUInt mByteUint1;  // Registers R12 to R15 (working register)
  ByteUInt mByteUint2;  // Registers R08 to R0A (L half of Feistel)
  ByteUInt mByteUint3;  // Registers R0B to R0E (R half of Feistel)
  ByteUInt mByteUint4;  // Registers R1C to R1E (S-box output)
  uint8_t mByteUpperTime2;   // Upper 8 bits of key schedule
  uint32_t mUintLowerTime;   // Lower 32 bits of key schedule
};

// Validate and extract weather data from decrypted plaintext.
// Returns -1 if the checksum (0x2501) doesn't match.
int32_t GetWeatherFromPlain(const uint8_t *PlainBytes) {
  // Check magic/checksum value
  uint32_t checkSum = PlainBytes[2] & 0x0f;
  checkSum <<= 8;
  checkSum |= PlainBytes[1];
  checkSum <<= 4;
  checkSum |= PlainBytes[0] >> 4;
  if (checkSum != 0x2501) return -1;

  uint32_t result = PlainBytes[0] & 0x0f;
  result <<= 8;
  result |= PlainBytes[4];
  result <<= 8;
  result |= PlainBytes[3];
  result <<= 4;
  result |= PlainBytes[2] >> 4;

  return static_cast<int32_t>(result);
}

// Pack weather data with checksum into plaintext format for encryption.
void GetPlainFromWeather(uint32_t weather, uint8_t *result) {
  weather <<= 4;
  result[1] = 0x50;
  result[2] = (weather & 0xf0) | 0x02;
  weather >>= 8;
  result[3] = weather & 0xff;
  weather >>= 8;
  result[4] = weather & 0xff;
  weather >>= 8;
  result[0] = (weather & 0x0f) | 0x10;
}

void CopyTimeToByteUint(const uint8_t *data, const uint8_t *key,
                        DataContainer *container) {
  container->mUintLowerTime = 0;
  for (int i = 0; i < 4; i++) {
    container->mUintLowerTime <<= 8;
    container->mUintLowerTime |= key[3 - i];
  }
  container->mByteUpperTime2 = key[4];

  // copy R
  container->mByteUint3.s.Byte0 = data[2];
  container->mByteUint3.s.Byte1 = data[3];
  container->mByteUint3.s.Byte2 = data[4];
  container->mByteUint3.FullUint >>= 4;

  // copy L
  container->mByteUint2.s.Byte0 = data[0];
  container->mByteUint2.s.Byte1 = data[1];
  container->mByteUint2.s.Byte2 = static_cast<uint8_t>(data[2] & 0x0F);
}

// Rotate the 40-bit key right by 1 or 2 positions per round.
// The rotation count depends on the round number (DES key schedule).
void ShiftTimeRight(int round, DataContainer *container) {
  int count = ((round == 16) || (round == 8) || (round == 7) || (round == 3))
                  ? 2
                  : 1;

  while (count-- != 0) {
    uint8_t tmp = 0;
    if ((container->mUintLowerTime & 0x00100000) != 0) tmp = 1;

    container->mUintLowerTime &= 0xFFEFFFFF;
    if ((container->mUintLowerTime & 1) != 0)
      container->mUintLowerTime |= 0x00100000;
    container->mUintLowerTime >>= 1;

    if ((container->mByteUpperTime2 & 1) != 0)
      container->mUintLowerTime |= 0x80000000;
    container->mByteUpperTime2 >>= 1;
    if (tmp != 0) container->mByteUpperTime2 |= 0x80;
  }
}

// Rotate the 40-bit key left by 1 or 2 positions per round.
void ShiftTimeLeft(int round, DataContainer *container) {
  int count = ((round == 16) || (round == 8) || (round == 7) || (round == 3))
                  ? 2
                  : 1;

  while (count-- != 0) {
    uint8_t tmp = 0;
    if ((container->mByteUpperTime2 & 0x80) != 0) tmp = 1;
    container->mByteUpperTime2 <<= 1;

    if ((container->mUintLowerTime & 0x80000000) != 0)
      container->mByteUpperTime2 |= 1;

    container->mUintLowerTime <<= 1;
    if ((container->mUintLowerTime & 0x00100000) != 0)
      container->mUintLowerTime |= 1;

    container->mUintLowerTime &= 0xFFEFFFFF;
    if (tmp != 0) container->mUintLowerTime |= 0x00100000;
  }
}

// Expand the R half from 20 bits to 32 bits using a fixed permutation.
void ExpandR(DataContainer *container) {
  container->mByteUint3.FullUint &= 0x000FFFFF;
  uint32_t tmp = 0x00100000;
  for (int i = 0; i < 12; i++) {
    if ((container->mByteUint3.FullUint & mUintArrBitPattern12[i]) != 0)
      container->mByteUint3.FullUint |= tmp;
    tmp <<= 1;
  }
}

// Expand the L half from 20 bits to 32 bits using a fixed permutation.
void ExpandL(DataContainer *container) {
  container->mByteUint2.FullUint &= 0x000FFFFF;
  uint32_t tmp = 0x00100000;
  for (int i = 0; i < 12; i++) {
    if ((container->mByteUint2.FullUint & mUintArrBitPattern12[i]) != 0)
      container->mByteUint2.FullUint |= tmp;
    tmp <<= 1;
  }
}

// Compress the 40-bit key schedule into a 30-bit round key.
void CompressKey(DataContainer *container) {
  container->mByteUint1.FullUint = 0;
  uint32_t tmp = 0x00000001;
  for (int i = 0; i < 30; i++) {
    if ((container->mUintLowerTime & mUintArrBitPattern30_1[i]) != 0 ||
        (container->mByteUpperTime2 & mUintArrBitPattern30_2[i]) != 0)
      container->mByteUint1.FullUint |= tmp;
    tmp <<= 1;
  }
}

// Apply the S-box substitution using the lookup tables.
// This is the non-linear core of the cipher.
void DoSbox(DataContainer *container) {
  uint8_t helper = container->mByteUint1.s.Byte3;
  container->mByteUint1.s.Byte3 = container->mByteUint1.s.Byte2;

  for (int i = 5; i > 0; i--) {
    if ((i & 1) == 0) {
      uint8_t tmp = static_cast<uint8_t>(container->mByteUint1.s.Byte0 >> 4);
      tmp |= static_cast<uint8_t>((container->mByteUint1.s.Byte0 & 0x0f) << 4);
      container->mByteUint1.s.Byte0 = tmp;
    }
    container->mByteUint1.s.Byte3 &= 0xF0;
    uint8_t tmp = static_cast<uint8_t>(
        (container->mByteUint1.s.Byte0 & 0x0F) | container->mByteUint1.s.Byte3);

    if ((i & 4) != 0)
      tmp = mByteArrLookupTable1C_1[(tmp & 0x38) >> 3] >>
            (56 - (tmp & 0x07) * 8);
    if ((i & 2) != 0)
      tmp = mByteArrLookupTable1C_2[(tmp & 0x38) >> 3] >>
            (56 - (tmp & 0x07) * 8);
    else if (i == 1)
      tmp = mByteArrLookupTable1C_3[(tmp & 0x38) >> 3] >>
            (56 - (tmp & 0x07) * 8);

    if ((i & 1) != 0)
      container->mByteUint4.s.Byte0 = static_cast<uint8_t>(tmp & 0x0F);
    else
      container->mByteUint4.s.Byte0 |= static_cast<uint8_t>(tmp & 0xF0);

    if ((i & 1) == 0) {
      uint8_t tmp2 = container->mByteUint1.s.Byte3;
      container->mByteUint1.FullUint >>= 8;
      container->mByteUint1.s.Byte3 = tmp2;
      container->mByteUint4.FullUint <<= 8;
    }

    container->mByteUint1.s.Byte3 >>= 1;
    if ((helper & 1) != 0) container->mByteUint1.s.Byte3 |= 0x80;
    helper >>= 1;

    container->mByteUint1.s.Byte3 >>= 1;
    if ((helper & 1) != 0) container->mByteUint1.s.Byte3 |= 0x80;
    helper >>= 1;
  }
}

// Apply the P-box permutation to the S-box output.
void DoPbox(DataContainer *container) {
  container->mByteUint1.FullUint = 0xFF000000;
  uint32_t tmp = 0x00000001;
  for (int i = 0; i < 20; i++) {
    if ((container->mByteUint4.FullUint & mUintArrBitPattern20[i]) != 0)
      container->mByteUint1.FullUint |= tmp;
    tmp <<= 1;
  }
}

// Decrypt one 40-bit block using 16 rounds of modified DES.
void Decrypt(const uint8_t *cipher, const uint8_t *key, uint8_t *plain) {
  DataContainer container;
  CopyTimeToByteUint(cipher, key, &container);

  for (int i = 16; i > 0; i--) {
    ShiftTimeRight(i, &container);
    ExpandR(&container);
    CompressKey(&container);

    container.mByteUint1.FullUint ^= container.mByteUint3.FullUint;
    container.mByteUint3.s.Byte2 &= 0x0F;

    DoSbox(&container);
    DoPbox(&container);

    container.mByteUint1.FullUint ^= container.mByteUint2.FullUint;
    container.mByteUint2.FullUint = container.mByteUint3.FullUint & 0x00FFFFFF;
    container.mByteUint3.FullUint = container.mByteUint1.FullUint & 0x00FFFFFF;
  }

  container.mByteUint3.FullUint <<= 4;
  container.mByteUint2.s.Byte2 &= 0x0F;
  container.mByteUint2.s.Byte2 |=
      static_cast<uint8_t>(container.mByteUint3.s.Byte0 & 0xF0);

  plain[0] = container.mByteUint2.s.Byte0;
  plain[1] = container.mByteUint2.s.Byte1;
  plain[2] = container.mByteUint2.s.Byte2;
  plain[3] = container.mByteUint3.s.Byte1;
  plain[4] = container.mByteUint3.s.Byte2;
}

// Encrypt one 40-bit block using 16 rounds of modified DES.
void Encrypt(const uint8_t *plain, const uint8_t *key, uint8_t *cipher) {
  DataContainer container;
  CopyTimeToByteUint(plain, key, &container);

  for (int i = 1; i < 17; i++) {
    ExpandL(&container);
    CompressKey(&container);

    container.mByteUint1.FullUint ^= container.mByteUint2.FullUint;
    container.mByteUint3.s.Byte2 &= 0x0F;

    DoSbox(&container);
    DoPbox(&container);

    container.mByteUint1.FullUint ^= container.mByteUint3.FullUint;
    container.mByteUint3.FullUint = container.mByteUint2.FullUint & 0x00FFFFFF;
    container.mByteUint2.FullUint = container.mByteUint1.FullUint & 0x00FFFFFF;

    ShiftTimeLeft(i, &container);
  }

  container.mByteUint3.FullUint <<= 4;
  container.mByteUint2.s.Byte2 &= 0x0F;
  container.mByteUint2.s.Byte2 |=
      static_cast<uint8_t>(container.mByteUint3.s.Byte0 & 0xF0);

  cipher[0] = container.mByteUint2.s.Byte0;
  cipher[1] = container.mByteUint2.s.Byte1;
  cipher[2] = container.mByteUint2.s.Byte2;
  cipher[3] = container.mByteUint3.s.Byte1;
  cipher[4] = container.mByteUint3.s.Byte2;
}

}  // namespace

// Public API: Decode weather from 40-bit cipher using 40-bit key.
// Returns the 23-bit weather data or -1 on checksum failure.
int32_t weather_decode(uint64_t cipher, uint64_t key) {
  uint8_t CipherBytes[5];
  uint8_t KeyBytes[5];
  uint8_t PlainBytes[5];

  for (int i = 0; i < 5; i++) CipherBytes[i] = cipher >> (i * 8);
  for (int i = 0; i < 5; i++) KeyBytes[i] = key >> (i * 8);

  Decrypt(CipherBytes, KeyBytes, PlainBytes);

  return GetWeatherFromPlain(PlainBytes);
}

// Public API: Encode 23-bit weather data into 40-bit cipher.
uint64_t weather_encode(uint32_t weather, uint64_t key) {
  uint8_t KeyBytes[5];
  uint8_t PlainBytes[5];
  uint8_t CipherBytes[5];

  GetPlainFromWeather(weather, PlainBytes);

  for (int i = 0; i < 5; i++) KeyBytes[i] = key >> (i * 8);

  Encrypt(PlainBytes, KeyBytes, CipherBytes);

  uint64_t cipher = 0;
  for (int i = 0; i < 5; i++)
    cipher |= static_cast<uint64_t>(CipherBytes[i]) << (i * 8);

  return cipher;
}
