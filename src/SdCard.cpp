#include "SdCard.h"
#include "BoardConfig.h"
#include "DevLog.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>

SdCard sdCard;

static SPIClass sdSpi(HSPI);

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
  if (freeInternal < bytes + 48 * 1024) {
    return nullptr;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

void deckFree(void *p) {
  if (p) {
    heap_caps_free(p);
  }
}

bool SdCard::Mount() {
  pinMode(PIN_SD_AUX, OUTPUT);
  digitalWrite(PIN_SD_AUX, HIGH);
  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  sdSpi.begin((int8_t)PIN_SD_SCK, (int8_t)PIN_SD_MISO, (int8_t)PIN_SD_MOSI, (int8_t)PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSpi, 20000000)) {
    mounted = false;
    DEV_LOG("SD: no card");
    return false;
  }
  if (!SD.exists("/moth")) {
    SD.mkdir("/moth");
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
  return SD.exists(path);
}

bool SdCard::Mkdir(const char *path) {
  if (!Ensure() || !path) {
    return false;
  }
  if (SD.exists(path)) {
    return true;
  }
  return SD.mkdir(path);
}

bool SdCard::Remove(const char *path) {
  if (!Ensure() || !path) {
    return false;
  }
  return SD.remove(path);
}

bool SdCard::ReadAll(const char *path, uint8_t *dst, int maxBytes, int *outLen) {
  if (outLen) {
    *outLen = 0;
  }
  if (!dst || maxBytes <= 0 || !path || !Ensure()) {
    return false;
  }
  File file = SD.open(path, FILE_READ);
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
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  size_t wrote = file.write(src, len);
  file.close();
  return (int)wrote == len;
}

int SdCard::List(const char *path, char names[][24], int maxNames, bool directories) {
  if (!names || maxNames <= 0 || !path || !Ensure()) {
    return 0;
  }
  File dir = SD.open(path);
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
