#include "BleAdvert.h"
#include <string.h>

// MIDI service UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, little-endian
// on the air, as the BLE MIDI spec and the CSS require for a 128-bit UUID.
static const uint8_t kMidiServiceUuidLe[16] = {
  0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
  0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
};

// AD types from the Bluetooth CSS. Flags are legal only in the primary
// advertising packet, never in the scan response.
static const uint8_t kAdFlags = 0x01;
static const uint8_t kAdUuid128Complete = 0x07;
static const uint8_t kAdNameShort = 0x08;
static const uint8_t kAdNameComplete = 0x09;
static const uint8_t kAdInterval = 0x12;
static const uint8_t kAdAppearance = 0x19;

// LE General Discoverable (0x02) and BR/EDR Not Supported (0x04).
static const uint8_t kFlagGeneralDiscoverable = 0x06;

// Appearance 0x0000 is Unknown. There is no SIG appearance for a MIDI
// instrument. A HID keyboard value would make a host such as the MPC
// treat the peripheral as a computer keyboard.
static const uint8_t kAppearanceUnknown[2] = {0x00, 0x00};

// Peripheral preferred connection interval, units of 1.25 ms.
// 0x0006 .. 0x000C is 7.5 ms .. 15 ms, the same range the link update asks for.
static const uint8_t kConnIntervalMin[2] = {0x06, 0x00};
static const uint8_t kConnIntervalMax[2] = {0x0C, 0x00};

static const char *selectName(const char *name, const char *fallback) {
  if (name && name[0]) {
    return name;
  }
  if (fallback && fallback[0]) {
    return fallback;
  }
  return "Mothdeck";
}

static int copyBounded(char *dst, int dstCap, const char *src) {
  int n = 0;
  if (dstCap <= 0) {
    return 0;
  }
  while (src[n] && n < dstCap - 1) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
  return n;
}

static int appendBytes(uint8_t *dst, int used, int cap, const uint8_t *src, int n) {
  if (n <= 0 || used < 0 || used > cap || n > cap - used) {
    return used;
  }
  memcpy(dst + used, src, (size_t)n);
  return used + n;
}

void buildBleMidiAdvert(const char *name, const char *fallback, BleAdvertPackets *out) {
  memset(out, 0, sizeof(*out));
  const char *chosen = selectName(name, fallback);
  int fullLen = copyBounded(out->gapName, (int)sizeof(out->gapName), chosen);

  int airLen = fullLen;
  if (airLen > kBleAdvNameMax) {
    airLen = kBleAdvNameMax;
  }
  if (airLen < 1) {
    fullLen = copyBounded(out->gapName, (int)sizeof(out->gapName), "Mothdeck");
    airLen = fullLen;
  }
  memcpy(out->advName, out->gapName, (size_t)airLen);
  out->advName[airLen] = 0;
  out->nameShortened = fullLen > airLen;

  uint8_t flags[3] = {0x02, kAdFlags, kFlagGeneralDiscoverable};
  int advUsed = appendBytes(out->adv, 0, kBleAdvMax, flags, 3);

  uint8_t nameHdr[2];
  nameHdr[0] = (uint8_t)(airLen + 1);
  nameHdr[1] = out->nameShortened ? kAdNameShort : kAdNameComplete;
  advUsed = appendBytes(out->adv, advUsed, kBleAdvMax, nameHdr, 2);
  advUsed = appendBytes(out->adv, advUsed, kBleAdvMax, (const uint8_t *)out->advName, airLen);

  uint8_t uuidHdr[2] = {0x11, kAdUuid128Complete};
  advUsed = appendBytes(out->adv, advUsed, kBleAdvMax, uuidHdr, 2);
  advUsed = appendBytes(out->adv, advUsed, kBleAdvMax, kMidiServiceUuidLe, 16);
  out->advLen = advUsed;

  // Scan response: full stored name, unknown appearance, preferred
  // connection interval. Flags stay out of this packet.
  uint8_t scanNameHdr[2];
  scanNameHdr[0] = (uint8_t)(fullLen + 1);
  scanNameHdr[1] = kAdNameComplete;
  int scanUsed = appendBytes(out->scan, 0, kBleAdvMax, scanNameHdr, 2);
  scanUsed = appendBytes(out->scan, scanUsed, kBleAdvMax, (const uint8_t *)out->gapName, fullLen);

  uint8_t appearance[4] = {0x03, kAdAppearance, kAppearanceUnknown[0], kAppearanceUnknown[1]};
  scanUsed = appendBytes(out->scan, scanUsed, kBleAdvMax, appearance, 4);

  uint8_t interval[6] = {
    0x05, kAdInterval,
    kConnIntervalMin[0], kConnIntervalMin[1],
    kConnIntervalMax[0], kConnIntervalMax[1]
  };
  scanUsed = appendBytes(out->scan, scanUsed, kBleAdvMax, interval, 6);
  out->scanLen = scanUsed;
}
