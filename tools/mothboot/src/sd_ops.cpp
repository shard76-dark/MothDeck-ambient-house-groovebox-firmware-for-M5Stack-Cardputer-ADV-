#include "sd_ops.h"

#include "BoardConfig.h"
#include "FatPlan.h"
#include "SdLimit.h"
#include "SdPattern.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <sd_diskio.h>
#include <string.h>

static SPIClass gSpi(HSPI);
static uint8_t gSec[512];
static uint8_t gGot[512];
static const uint32_t kSentinel = 32;

static void spiUp() {
  pinMode(PIN_SD_AUX, OUTPUT);
  digitalWrite(PIN_SD_AUX, HIGH);
  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  gSpi.begin((int8_t)PIN_SD_SCK, (int8_t)PIN_SD_MISO, (int8_t)PIN_SD_MOSI, (int8_t)PIN_SD_CS);
}

static bool sectorOk(uint8_t pdrv, uint32_t sector, uint32_t salt, bool checkSentinel) {
  sdPatternFill(gSec, sector, salt);
  if (!sd_write_raw(pdrv, gSec, sector)) {
    return false;
  }
  if (!sd_read_raw(pdrv, gGot, sector) || !sdPatternMatch(gGot, sector, salt)) {
    return false;
  }
  if (!checkSentinel || sector == kSentinel) {
    return true;
  }
  if (!sd_read_raw(pdrv, gGot, kSentinel)) {
    return false;
  }
  return sdPatternMatch(gGot, kSentinel, salt);
}

bool sdCheckFull(SdProgressFn progress, uint64_t *verifiedOut, uint64_t *reportedOut, uint32_t *bpsOut) {
  if (verifiedOut) {
    *verifiedOut = 0;
  }
  if (reportedOut) {
    *reportedOut = 0;
  }
  if (bpsOut) {
    *bpsOut = 0;
  }
  SD.end();
  spiUp();
  uint8_t pdrv = sdcard_init(PIN_SD_CS, &gSpi, 20000000);
  if (pdrv == 0xFF) {
    if (progress) {
      progress(0, "Insert a card", 0, 0);
    }
    return false;
  }
  uint32_t sectors = sdcard_num_sectors(pdrv);
  uint32_t sectorBytes = sdcard_sector_size(pdrv);
  if (sectorBytes == 0) {
    sectorBytes = 512;
  }
  uint64_t reported = (uint64_t)sectors * sectorBytes;
  if (reportedOut) {
    *reportedOut = reported;
  }
  if (sectors < 2048 || sectorBytes != 512) {
    sdcard_uninit(pdrv);
    if (progress) {
      progress(0, "Insert a card", 0, reported);
    }
    return false;
  }
  uint32_t salt = esp_random();
  sdPatternFill(gSec, kSentinel, salt);
  if (!sd_write_raw(pdrv, gSec, kSentinel)) {
    sdcard_uninit(pdrv);
    return false;
  }

  uint32_t lastGood = 0;
  uint32_t firstBad = sectors;
  uint32_t step = 2048;
  while (step < sectors) {
    if (!sectorOk(pdrv, step, salt, true)) {
      firstBad = step;
      break;
    }
    lastGood = step;
    if (step > sectors / 2) {
      break;
    }
    uint32_t next = step << 1;
    if (next < step) {
      break;
    }
    step = next;
    if (progress) {
      int pct = (int)((uint64_t)lastGood * 10ull / sectors);
      progress(pct, "Checking card", (uint64_t)lastGood * 512ull, reported);
    }
  }
  if (firstBad > lastGood + 1) {
    uint32_t lo = lastGood;
    uint32_t hi = firstBad;
    while (hi > lo + 1) {
      uint32_t mid = lo + (hi - lo) / 2;
      if (mid <= lo) {
        break;
      }
      if (sectorOk(pdrv, mid, salt, true)) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    lastGood = lo;
    firstBad = hi;
  }
  if (firstBad == sectors && lastGood + 1 < sectors) {
    if (sectorOk(pdrv, sectors - 1, salt, true)) {
      lastGood = sectors - 1;
    } else {
      firstBad = sectors - 1;
    }
  }
  uint32_t end = firstBad;
  if (end > sectors) {
    end = sectors;
  }
  uint32_t t0 = millis();
  uint32_t done = 0;
  const uint32_t chunk = 8;
  for (uint32_t s = 0; s < end; s += chunk) {
    uint32_t n = chunk;
    if (s + n > end) {
      n = end - s;
    }
    bool chunkOk = true;
    for (uint32_t i = 0; i < n; i++) {
      sdPatternFill(gSec, s + i, salt);
      if (!sd_write_raw(pdrv, gSec, s + i)) {
        chunkOk = false;
        n = i;
        break;
      }
    }
    for (uint32_t i = 0; i < n; i++) {
      if (!sd_read_raw(pdrv, gGot, s + i) || !sdPatternMatch(gGot, s + i, salt)) {
        chunkOk = false;
        done = s + i;
        end = done;
        break;
      }
    }
    if (!chunkOk) {
      break;
    }
    done = s + n;
    if (progress && (done == end || (done & 2047u) == 0)) {
      int pct = end ? (int)((uint64_t)done * 100ull / end) : 100;
      if (pct > 100) {
        pct = 100;
      }
      progress(pct, "Checking card", (uint64_t)done * 512ull, reported);
    }
  }
  uint32_t elapsed = millis() - t0;
  if (elapsed == 0) {
    elapsed = 1;
  }
  uint64_t verified = (uint64_t)done * 512ull;
  uint32_t bps = (uint32_t)(verified / elapsed * 1000ull);
  if (bpsOut) {
    *bpsOut = bps;
  }
  if (verifiedOut) {
    *verifiedOut = verified;
  }
  sdcard_uninit(pdrv);
  if (verified >= 64ull * 1024ull * 1024ull) {
    mothSdStoreVerified(verified);
  }
  if (progress) {
    progress(100, "Card checked", verified, reported);
  }
  return verified >= 64ull * 1024ull * 1024ull;
}

static bool writeSector(uint8_t pdrv, uint32_t sector, const uint8_t *src) {
  memcpy(gSec, src, 512);
  return sd_write_raw(pdrv, gSec, sector);
}

static void makeMbr(uint8_t sector[512], const FatPlan *plan) {
  memset(sector, 0, 512);
  sector[446] = 0x00;
  sector[450] = 0x0C;
  sector[447] = 0xFE;
  sector[448] = 0xFF;
  sector[449] = 0xFF;
  sector[451] = 0xFE;
  sector[452] = 0xFF;
  sector[453] = 0xFF;
  sector[454] = (uint8_t)plan->partStart;
  sector[455] = (uint8_t)(plan->partStart >> 8);
  sector[456] = (uint8_t)(plan->partStart >> 16);
  sector[457] = (uint8_t)(plan->partStart >> 24);
  sector[458] = (uint8_t)plan->partSectors;
  sector[459] = (uint8_t)(plan->partSectors >> 8);
  sector[460] = (uint8_t)(plan->partSectors >> 16);
  sector[461] = (uint8_t)(plan->partSectors >> 24);
  sector[510] = 0x55;
  sector[511] = 0xAA;
}

bool sdFormatCard(uint64_t requestedBytes, SdProgressFn progress) {
  uint64_t verified = mothSdVerifiedBytes();
  FatPlan plan;
  if (!fatPlanFor(verified, kFatMarginBytes, requestedBytes, &plan)) {
    if (progress) {
      progress(0, verified ? "Check the card" : "Check the card", 0, verified);
    }
    return false;
  }
  SD.end();
  spiUp();
  uint8_t pdrv = sdcard_init(PIN_SD_CS, &gSpi, 20000000);
  if (pdrv == 0xFF) {
    if (progress) {
      progress(0, "Insert a card", 0, verified);
    }
    return false;
  }
  if (progress) {
    progress(1, "Preparing card", plan.volumeBytes, verified);
  }
  uint8_t boot[512];
  uint8_t info[512];
  uint8_t fat[512];
  fat32BootSector(&plan, boot);
  fat32FsInfo(info);
  fat32FatHead(fat);
  makeMbr(gSec, &plan);
  bool ok = sd_write_raw(pdrv, gSec, 0);
  ok = ok && writeSector(pdrv, plan.partStart, boot);
  ok = ok && writeSector(pdrv, plan.partStart + 1, info);
  ok = ok && writeSector(pdrv, plan.partStart + 6, boot);
  memset(gSec, 0, 512);
  uint32_t fatBytes = plan.fats * plan.sectorsPerFat;
  for (uint32_t i = 0; ok && i < fatBytes; i++) {
    const uint8_t *src = i == 0 || i == plan.sectorsPerFat ? fat : gSec;
    ok = writeSector(pdrv, plan.partStart + plan.reserved + i, src);
    if (progress && (i & 63u) == 0) {
      int pct = 5 + (int)((uint64_t)i * 80ull / fatBytes);
      progress(pct, "Preparing card", plan.volumeBytes, verified);
    }
  }
  memset(gSec, 0, 512);
  for (uint32_t i = 0; ok && i < plan.sectorsPerCluster; i++) {
    ok = writeSector(pdrv, plan.dataStart + i, gSec);
  }
  sdcard_uninit(pdrv);
  if (!ok) {
    if (progress) {
      progress(0, "Try again", 0, verified);
    }
    return false;
  }
  spiUp();
  if (!SD.begin(PIN_SD_CS, gSpi, 20000000)) {
    if (progress) {
      progress(0, "Try again", 0, verified);
    }
    return false;
  }
  SD.mkdir("/apps");
  SD.mkdir("/moth");
  SD.mkdir("/moth/drums");
  SD.mkdir("/moth/loops");
  SD.mkdir("/moth/instruments");
  if (progress) {
    progress(100, "Card ready", plan.volumeBytes, verified);
  }
  return true;
}

bool sdMountFiles() {
  if (SD.cardType() != CARD_NONE) {
    return true;
  }
  spiUp();
  return SD.begin(PIN_SD_CS, gSpi, 20000000);
}

void sdUnmountFiles() {
  SD.end();
}

uint64_t sdVolumeBytes() {
  if (!sdMountFiles()) {
    return 0;
  }
  return SD.totalBytes();
}

uint64_t sdFreeBytes() {
  if (!sdMountFiles()) {
    return 0;
  }
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  if (used > total) {
    return 0;
  }
  return total - used;
}
