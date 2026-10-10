#include "SdCard.h"
#include "DspHot.h"
#include "BoardConfig.h"
#include "DevLog.h"
#include "WavPcm.h"
#include <Arduino.h>
#if MOTHDECK_BOARD_TTGO
#include <SD_MMC.h>
// PSRAM's cache workaround owns HSPI on a classic ESP32. The T8 socket is
// the SDMMC 1-bit pins, so the card does not use an SPI host.
#define CARD_FS SD_MMC
#else
#include <SD.h>
#include <SPI.h>
#define CARD_FS SD
static SPIClass sdSpi(HSPI);
#endif
#include <string.h>

SdCard sdCard;

void *deckAlloc(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  if (psramFound()) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
      return p;
    }
  }
  size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  // Leave a little internal RAM for BLE and the audio DMA. The old 48KB
  // floor rejected every kit and plugin once a loop had been cached.
  if (freeInternal < bytes + 8 * 1024) {
    return nullptr;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

size_t deckFreeInternal() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void deckFree(void *p) {
  if (p) {
    heap_caps_free(p);
  }
}

bool SdCard::Mount() {
#if MOTHDECK_BOARD_TTGO
  // D3 held high keeps the card in SD mode. CLK/CMD/D0 are the slot pins.
  pinMode(PIN_SD_CS, INPUT_PULLUP);
  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  if (!SD_MMC.setPins(PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO) || !SD_MMC.begin("/sdcard", true)) {
    mounted = false;
    DEV_LOG("SD: no card");
    return false;
  }
#else
#if PIN_SD_AUX >= 0
  pinMode(PIN_SD_AUX, OUTPUT);
  digitalWrite(PIN_SD_AUX, HIGH);
#endif
  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  sdSpi.begin((int8_t)PIN_SD_SCK, (int8_t)PIN_SD_MISO, (int8_t)PIN_SD_MOSI, (int8_t)PIN_SD_CS);
  if (!CARD_FS.begin(PIN_SD_CS, sdSpi, 20000000)) {
    mounted = false;
    DEV_LOG("SD: no card");
    return false;
  }
#endif
  if (!CARD_FS.exists("/moth")) {
    CARD_FS.mkdir("/moth");
  }
  mounted = true;
  DEV_LOG("SD: mounted");
  return true;
}

bool SdCard::Ensure() {
  if (mounted) {
    return true;
  }
  uint32_t now = millis();
  if (lastTry != 0 && (uint32_t)(now - lastTry) < 1500) {
    return false;
  }
  lastTry = now;
  return Mount();
}

bool SdCard::Exists(const char *path) {
  if (!Ensure() || !path) {
    return false;
  }
  return CARD_FS.exists(path);
}

bool SdCard::Mkdir(const char *path) {
  if (!Ensure() || !path) {
    return false;
  }
  if (CARD_FS.exists(path)) {
    return true;
  }
  return CARD_FS.mkdir(path);
}

bool SdCard::Remove(const char *path) {
  if (!Ensure() || !path) {
    return false;
  }
  return CARD_FS.remove(path);
}

bool SdCard::ReadAll(const char *path, uint8_t *dst, int maxBytes, int *outLen) {
  if (outLen) {
    *outLen = 0;
  }
  if (!dst || maxBytes <= 0 || !path || !Ensure()) {
    return false;
  }
  File file = CARD_FS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  int size = (int)file.size();
  if (size < 0 || size > maxBytes) {
    file.close();
    return false;
  }
  int n = file.read(dst, size);
  file.close();
  if (n != size) {
    return false;
  }
  if (outLen) {
    *outLen = n;
  }
  return true;
}

bool SdCard::ReadPrefix(const char *path, uint8_t *dst, int maxBytes, int *outLen) {
  if (outLen) {
    *outLen = 0;
  }
  if (!dst || maxBytes <= 0 || !path || !Ensure()) {
    return false;
  }
  File file = CARD_FS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  int n = file.read(dst, maxBytes);
  file.close();
  if (n < 0) {
    return false;
  }
  if (outLen) {
    *outLen = n;
  }
  return n > 0;
}

bool SdCard::ReadText(const char *path, char *dst, int maxBytes) {
  if (!dst || maxBytes < 2) {
    return false;
  }
  dst[0] = 0;
  int n = 0;
  if (!ReadAll(path, (uint8_t *)dst, maxBytes - 1, &n)) {
    dst[0] = 0;
    return false;
  }
  dst[n] = 0;
  return true;
}

bool SdCard::WriteAll(const char *path, const uint8_t *src, int len) {
  if (!path || !src || len < 0 || !Ensure()) {
    return false;
  }
  CARD_FS.remove(path);
  File file = CARD_FS.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  size_t wrote = file.write(src, len);
  file.close();
  return (int)wrote == len;
}

static void sdErr(char *err, int errLen, const char *msg) {
  if (err && errLen > 0) {
    snprintf(err, errLen, "%s", msg ? msg : "");
  }
}

static int decodeChunk(const uint8_t *p, int frames, int channels, int bits, int16_t *dst) {
  for (int i = 0; i < frames; i++) {
    int32_t acc = 0;
    for (int c = 0; c < channels; c++) {
      int32_t s;
      if (bits == 8) {
        s = ((int32_t)(*p++) - 128) << 8;
      } else {
        s = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        p += 2;
      }
      acc += s;
    }
    if (channels == 2) {
      acc /= 2;
    }
    dst[i] = (int16_t)dspSat16(acc);
  }
  return frames;
}

bool SdCard::LoadWavMono(const char *path, int16_t *dst, int dstFrames, int *got, int *rate, char *err, int errLen) {
  if (got) {
    *got = 0;
  }
  if (rate) {
    *rate = 0;
  }
  if (!dst || dstFrames < 2 || !path || !Ensure()) {
    sdErr(err, errLen, "No sample buffer");
    return false;
  }
  File file = CARD_FS.open(path, FILE_READ);
  if (!file) {
    sdErr(err, errLen, "Sample missing");
    return false;
  }
  uint8_t hdr[768];
  int n = file.read(hdr, sizeof(hdr));
  WavInfo info;
  if (!parseWavPrefix(hdr, n, &info)) {
    file.close();
    sdErr(err, errLen, info.error[0] ? info.error : "Bad WAV");
    return false;
  }
  int frames = info.frames;
  if (frames > dstFrames) {
    frames = dstFrames;
  }
  if (frames < 2) {
    file.close();
    sdErr(err, errLen, "Sample too short");
    return false;
  }
  if (!file.seek(info.dataOffset)) {
    file.close();
    sdErr(err, errLen, "Sample seek failed");
    return false;
  }
  int frameBytes = info.channels * (info.bits / 8);
  const int kBatch = 256;
  uint8_t chunk[kBatch * 4];
  int wrote = 0;
  while (wrote < frames) {
    int batch = frames - wrote;
    if (batch > kBatch) {
      batch = kBatch;
    }
    int need = batch * frameBytes;
    int gotBytes = file.read(chunk, need);
    if (gotBytes < need) {
      break;
    }
    decodeChunk(chunk, batch, info.channels, info.bits, dst + wrote);
    wrote += batch;
  }
  file.close();
  if (wrote < 2) {
    sdErr(err, errLen, "Sample read failed");
    return false;
  }
  if (got) {
    *got = wrote;
  }
  if (rate) {
    *rate = info.rate > 0 ? info.rate : 22050;
  }
  sdErr(err, errLen, "");
  return true;
}

int SdCard::List(const char *path, char names[][24], int maxNames, bool directories) {
  if (!names || maxNames <= 0 || !path || !Ensure()) {
    return 0;
  }
  File dir = CARD_FS.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }
  int count = 0;
  for (;;) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    bool dirent = entry.isDirectory();
    const char *full = entry.name();
    entry.close();
    if (dirent != directories || !full) {
      continue;
    }
    const char *base = full;
    for (const char *p = full; *p; p++) {
      if (*p == '/') {
        base = p + 1;
      }
    }
    if (!base[0] || base[0] == '.') {
      continue;
    }
    if (count < maxNames) {
      memset(names[count], 0, 24);
      strncpy(names[count], base, 23);
      count++;
    }
  }
  dir.close();
  return count;
}
