#include "LauncherDetect.h"

bool appImageHeaderValid(const uint8_t *header, int len) {
  if (!header || len < kAppHeaderBytes) {
    return false;
  }
  if (header[0] != kAppImageMagic) {
    return false;
  }
  uint8_t segments = header[1];
  if (segments < 1 || segments > 16) {
    return false;
  }
  if (header[2] > 5) {
    return false;
  }
  uint32_t entry = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) |
                   ((uint32_t)header[7] << 24);
  bool iram = entry >= 0x40000000u && entry < 0x40400000u;
  bool irom = entry >= 0x42000000u && entry < 0x44000000u;
  if (!iram && !irom) {
    return false;
  }
  uint16_t chip = (uint16_t)header[12] | ((uint16_t)header[13] << 8);
  if (chip != kChipEsp32S3) {
    return false;
  }
  if (header[23] > 1) {
    return false;
  }
  return true;
}

int launcherSlotIndex(const FlashSlot *slots, int count, uint32_t runningAddress) {
  if (!slots || count <= 0) {
    return -1;
  }
  for (int i = 0; i < count; i++) {
    const FlashSlot &slot = slots[i];
    if (!slot.exists || slot.subtype != kLauncherSlotSubtype) {
      continue;
    }
    if (slot.size < (uint32_t)kAppHeaderBytes) {
      continue;
    }
    if (slot.address == runningAddress) {
      continue;
    }
    if (!appImageHeaderValid(slot.header, slot.headerLen)) {
      continue;
    }
    return i;
  }
  return -1;
}

bool launcherPresent(const FlashSlot *slots, int count, uint32_t runningAddress) {
  return launcherSlotIndex(slots, count, runningAddress) >= 0;
}
