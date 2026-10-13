#include "SdPattern.h"

#include <string.h>

void sdPatternFill(uint8_t *dst, uint32_t sector, uint32_t salt) {
  if (!dst) {
    return;
  }
  uint32_t mix = sector ^ salt ^ 0xA5A5A5A5u;
  for (int i = 0; i < kSdSector; i += 4) {
    uint32_t word = mix + (uint32_t)i * 0x01000193u;
    dst[i] = (uint8_t)word;
    dst[i + 1] = (uint8_t)(word >> 8);
    dst[i + 2] = (uint8_t)(word >> 16);
    dst[i + 3] = (uint8_t)(word >> 24);
  }
  dst[0] = (uint8_t)sector;
  dst[1] = (uint8_t)(sector >> 8);
  dst[2] = (uint8_t)(sector >> 16);
  dst[3] = (uint8_t)(sector >> 24);
}

bool sdPatternMatch(const uint8_t *src, uint32_t sector, uint32_t salt) {
  uint8_t expect[kSdSector];
  if (!src) {
    return false;
  }
  sdPatternFill(expect, sector, salt);
  return memcmp(src, expect, kSdSector) == 0;
}

uint64_t sdVerifiedFromProbes(const SdProbe *probes, int count) {
  uint64_t covered = 0;
  if (!probes || count <= 0) {
    return 0;
  }
  for (int i = 0; i < count; i++) {
    if (!probes[i].match || !probes[i].sentinel) {
      break;
    }
    uint64_t end = probes[i].offset + (uint64_t)kSdSector;
    if (end > covered) {
      covered = end;
    }
  }
  return covered;
}
