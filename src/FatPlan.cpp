#include "FatPlan.h"

#include <string.h>

static uint32_t ceilDiv(uint32_t n, uint32_t d) {
  if (d == 0) {
    return 0;
  }
  return (n + d - 1) / d;
}

bool fatPlanFor(uint64_t verifiedBytes, uint64_t marginBytes, uint64_t requestedBytes, FatPlan *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (marginBytes == 0) {
    marginBytes = kFatMarginBytes;
  }
  if (verifiedBytes <= marginBytes + 64ull * 1024ull * 1024ull) {
    return false;
  }
  uint64_t ceiling = verifiedBytes - marginBytes;
  uint64_t want = requestedBytes == 0 || requestedBytes > ceiling ? ceiling : requestedBytes;
  want &= ~((1ull << 20) - 1ull);
  if (want < 64ull * 1024ull * 1024ull) {
    return false;
  }
  uint32_t endSector = (uint32_t)(want / 512ull);
  if (endSector <= kFatPartStart + 65536u) {
    return false;
  }
  uint32_t partSectors = endSector - kFatPartStart;
  static const uint32_t kSpcTry[] = {8, 16, 32, 64, 4, 2, 1};
  for (unsigned t = 0; t < sizeof(kSpcTry) / sizeof(kSpcTry[0]); t++) {
    uint32_t spc = kSpcTry[t];
    uint32_t reserved = 32;
    uint32_t fats = 2;
    uint32_t clusters = 0;
    uint32_t spf = 0;
    for (int pass = 0; pass < 8; pass++) {
      uint32_t data = partSectors - reserved - fats * spf;
      clusters = data / spc;
      uint32_t entries = clusters + 2;
      uint32_t next = ceilDiv(entries * 4, 512);
      if (next == spf && clusters >= 65525u && clusters < 0x0FFFFFF0u) {
        break;
      }
      spf = next;
    }
    uint32_t data = partSectors - reserved - fats * spf;
    clusters = data / spc;
    if (clusters < 65525u || clusters >= 0x0FFFFFF0u) {
      continue;
    }
    if (reserved + fats * spf >= partSectors) {
      continue;
    }
    out->partStart = kFatPartStart;
    out->partSectors = partSectors;
    out->reserved = reserved;
    out->sectorsPerCluster = spc;
    out->sectorsPerFat = spf;
    out->fats = fats;
    out->rootCluster = 2;
    out->clusters = clusters;
    out->dataStart = kFatPartStart + reserved + fats * spf;
    out->volumeBytes = want;
    out->ok = true;
    return true;
  }
  return false;
}

static void put16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

void fat32BootSector(const FatPlan *plan, uint8_t sector[512]) {
  if (!sector) {
    return;
  }
  memset(sector, 0, 512);
  if (!plan || !plan->ok) {
    return;
  }
  sector[0] = 0xEB;
  sector[1] = 0x58;
  sector[2] = 0x90;
  memcpy(sector + 3, "MSDOS5.0", 8);
  put16(sector + 11, 512);
  sector[13] = (uint8_t)plan->sectorsPerCluster;
  put16(sector + 14, plan->reserved);
  sector[16] = (uint8_t)plan->fats;
  sector[21] = 0xF8;
  put32(sector + 32, plan->partSectors);
  put32(sector + 36, plan->sectorsPerFat);
  put32(sector + 44, plan->rootCluster);
  put16(sector + 48, 1);
  put16(sector + 50, 6);
  sector[64] = 0x80;
  sector[66] = 0x29;
  put32(sector + 67, 0x4D4F5448u);
  memcpy(sector + 71, "MOTHBOOT   ", 11);
  memcpy(sector + 82, "FAT32   ", 8);
  sector[510] = 0x55;
  sector[511] = 0xAA;
}

void fat32FsInfo(uint8_t sector[512]) {
  if (!sector) {
    return;
  }
  memset(sector, 0, 512);
  put32(sector + 0, 0x41615252u);
  put32(sector + 484, 0x61417272u);
  put32(sector + 488, 0xFFFFFFFFu);
  put32(sector + 492, 0xFFFFFFFFu);
  sector[510] = 0x55;
  sector[511] = 0xAA;
}

void fat32FatHead(uint8_t sector[512]) {
  if (!sector) {
    return;
  }
  memset(sector, 0, 512);
  put32(sector + 0, 0x0FFFFFF8u);
  put32(sector + 4, 0x0FFFFFFFu);
  put32(sector + 8, 0x0FFFFFFFu);
}
