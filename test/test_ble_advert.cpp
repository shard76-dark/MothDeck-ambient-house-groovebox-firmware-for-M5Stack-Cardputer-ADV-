#include "BleAdvert.h"
#include "BoardConfig.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

static bool sameBytes(const uint8_t *got, int gotLen, const uint8_t *want, int wantLen) {
  if (gotLen != wantLen) {
    return false;
  }
  return std::memcmp(got, want, (size_t)wantLen) == 0;
}

static bool hasAdType(const uint8_t *data, int len, uint8_t type) {
  int i = 0;
  while (i + 1 < len) {
    int field = data[i];
    if (field <= 0 || i + 1 + field > len) {
      return false;
    }
    if (data[i + 1] == type) {
      return true;
    }
    i += 1 + field;
  }
  return false;
}

static void testDefaultName() {
  expect(std::strcmp(MOTHDECK_BLE_NAME_DEFAULT, "MothDeck") == 0, "default BLE name is MothDeck");
  expect(std::strlen(MOTHDECK_BLE_NAME_DEFAULT) <= (size_t)kBleAdvNameMax, "default name fits beside the MIDI UUID");

  const uint8_t adv[] = {
    0x02, 0x01, 0x06,
    0x09, 0x09, 'M', 'o', 't', 'h', 'D', 'e', 'c', 'k',
    0x11, 0x07,
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
  };
  const uint8_t scan[] = {
    0x09, 0x09, 'M', 'o', 't', 'h', 'D', 'e', 'c', 'k',
    0x03, 0x19, 0x00, 0x00,
    0x05, 0x12, 0x06, 0x00, 0x12, 0x00
  };

  BleAdvertPackets pkts;
  buildBleMidiAdvert(nullptr, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  expect(pkts.advLen == 31, "default advert is 31 bytes");
  expect(sameBytes(pkts.adv, pkts.advLen, adv, (int)sizeof(adv)), "default advert is flags, MothDeck, MIDI UUID");
  expect(sameBytes(pkts.scan, pkts.scanLen, scan, (int)sizeof(scan)), "default scan response is the full name, appearance, interval");
  expect(!pkts.nameShortened, "default name is complete in the advert");
  expect(std::strcmp(pkts.advName, "MothDeck") == 0, "on-air name is MothDeck");
  expect(std::strcmp(pkts.gapName, "MothDeck") == 0, "GAP name is MothDeck");
  expect(!hasAdType(pkts.scan, pkts.scanLen, 0x01), "scan response does not carry flags");
}

static void testLongName() {
  BleAdvertPackets pkts;
  buildBleMidiAdvert("MothSynth", "MothDeck", &pkts);
  expect(pkts.nameShortened, "MothSynth is shortened in the advert");
  expect(std::strcmp(pkts.advName, "MothSynt") == 0, "MothSynth advert name is MothSynt");
  expect(std::strcmp(pkts.gapName, "MothSynth") == 0, "MothSynth GAP name stays complete");
  expect(pkts.adv[0] == 0x02 && pkts.adv[1] == 0x01 && pkts.adv[2] == 0x06, "long name keeps general-discoverable flags");
  expect(pkts.adv[3] == 0x09 && pkts.adv[4] == 0x08, "shortened name uses AD type 0x08");
  expect(std::memcmp(pkts.adv + 5, "MothSynt", 8) == 0, "shortened bytes are the first 8 characters");
  expect(pkts.advLen == 31, "shortened advert still fills 31 bytes with the UUID");
  expect(pkts.adv[13] == 0x11 && pkts.adv[14] == 0x07, "MIDI UUID stays in the primary packet");
  expect(pkts.scan[0] == 0x0A && pkts.scan[1] == 0x09, "scan response carries the complete name");
  expect(std::memcmp(pkts.scan + 2, "MothSynth", 9) == 0, "scan response name is MothSynth");

  buildBleMidiAdvert("abcdefghijklmnop", "MothDeck", &pkts);
  expect(pkts.nameShortened && std::strcmp(pkts.advName, "abcdefgh") == 0, "16-character name shortens to 8");
  expect(std::strcmp(pkts.gapName, "abcdefghijklmnop") == 0, "16-character GAP name is kept");
  expect(pkts.scanLen <= 31 && pkts.advLen <= 31, "both payloads stay inside 31 bytes");
  expect(hasAdType(pkts.adv, pkts.advLen, 0x07), "primary packet still has the MIDI UUID");
  expect(hasAdType(pkts.scan, pkts.scanLen, 0x09), "scan response has the complete name");
}

static void testEmptyFallsBack() {
  BleAdvertPackets pkts;
  buildBleMidiAdvert("", "MothDeck", &pkts);
  expect(std::strcmp(pkts.gapName, "MothDeck") == 0 && !pkts.nameShortened, "empty name uses the fallback");
  buildBleMidiAdvert(nullptr, nullptr, &pkts);
  expect(std::strcmp(pkts.gapName, "MothDeck") == 0, "missing name and fallback still advertise MothDeck");
  expect(pkts.adv[2] == 0x06, "fallback advert is general discoverable");
}

int main() {
  testDefaultName();
  testLongName();
  testEmptyFallsBack();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
