#ifndef LauncherDetect_h
#define LauncherDetect_h
#include <stdint.h>
#include <stdbool.h>

// bmorcelli Launcher keeps itself in an APP_TEST slot (subtype 0x20).
// ESP-IDF will not boot that subtype through esp_ota_set_boot_partition().
static const uint8_t kLauncherSlotSubtype = 0x20;
static const int kAppHeaderBytes = 24;
static const uint8_t kAppImageMagic = 0xE9;
static const uint16_t kChipEsp32S3 = 0x0009;

struct FlashSlot {
  bool exists;
  uint8_t subtype;
  uint32_t address;
  uint32_t size;
  const uint8_t *header;
  int headerLen;
};

// True when the 24-byte ESP app header is a plausible ESP32-S3 image.
// Magic is 0xE9. Erased flash (0xFF), a short read, a zero segment count,
// and a non-S3 chip id are rejected.
bool appImageHeaderValid(const uint8_t *header, int len);

// Index of a verified Launcher slot, or -1. The slot must be APP_TEST,
// large enough for a header, not the running image, and carry a valid header.
int launcherSlotIndex(const FlashSlot *slots, int count, uint32_t runningAddress);
bool launcherPresent(const FlashSlot *slots, int count, uint32_t runningAddress);

#endif
