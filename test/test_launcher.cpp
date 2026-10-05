#include "LauncherDetect.h"
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

// First 24 bytes of a real ESP32-S3 app image (magic, 6 segments, chip 0x0009).
static const uint8_t kRealHeader[24] = {
    0xE9, 0x06, 0x02, 0x3F, 0x94, 0x5D, 0x37, 0x40, 0xEE, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01};

static FlashSlot slot(uint8_t subtype, uint32_t address, const uint8_t *header, int len) {
  FlashSlot s;
  std::memset(&s, 0, sizeof(s));
  s.exists = true;
  s.subtype = subtype;
  s.address = address;
  s.size = 0x150000;
  s.header = header;
  s.headerLen = len;
  return s;
}

static void testHeader() {
  expect(appImageHeaderValid(kRealHeader, 24), "real ESP32-S3 header is valid");
  expect(!appImageHeaderValid(nullptr, 24), "missing header is absent");
  expect(!appImageHeaderValid(kRealHeader, 8), "short header is absent");

  uint8_t erased[24];
  std::memset(erased, 0xFF, sizeof(erased));
  expect(!appImageHeaderValid(erased, 24), "erased flash is not an app");

  uint8_t corrupt[24];
  std::memcpy(corrupt, kRealHeader, 24);
  corrupt[0] = 0x00;
  expect(!appImageHeaderValid(corrupt, 24), "wrong magic is corrupt");

  std::memcpy(corrupt, kRealHeader, 24);
  corrupt[1] = 0;
  expect(!appImageHeaderValid(corrupt, 24), "zero segment count is corrupt");

  std::memcpy(corrupt, kRealHeader, 24);
  corrupt[12] = 0x00;
  corrupt[13] = 0x00;
  expect(!appImageHeaderValid(corrupt, 24), "ESP32 header is not this board");

  std::memcpy(corrupt, kRealHeader, 24);
  corrupt[4] = corrupt[5] = corrupt[6] = corrupt[7] = 0;
  expect(!appImageHeaderValid(corrupt, 24), "zero entry point is corrupt");
}

static void testSlots() {
  const uint32_t running = 0x200000;
  FlashSlot present[2];
  present[0] = slot(0x10, running, kRealHeader, 24);
  present[1] = slot(kLauncherSlotSubtype, 0x10000, kRealHeader, 24);
  expect(launcherPresent(present, 2, running), "APP_TEST image with a valid header is Launcher");
  expect(launcherSlotIndex(present, 2, running) == 1, "the expected slot is APP_TEST");

  FlashSlot full[2];
  full[0] = slot(0x10, 0x10000, kRealHeader, 24);
  full[1] = slot(0x11, 0x3D0000, kRealHeader, 24);
  expect(!launcherPresent(full, 2, 0x10000), "OTA slots alone are not Launcher");

  expect(!launcherPresent(nullptr, 0, running), "no partition table means Launcher is absent");

  uint8_t erased[24];
  std::memset(erased, 0xFF, sizeof(erased));
  FlashSlot blank = slot(kLauncherSlotSubtype, 0x10000, erased, 24);
  expect(!launcherPresent(&blank, 1, running), "empty APP_TEST slot is not Launcher");

  uint8_t bad[24];
  std::memcpy(bad, kRealHeader, 24);
  bad[0] = 0xE8;
  FlashSlot corrupt = slot(kLauncherSlotSubtype, 0x10000, bad, 24);
  expect(!launcherPresent(&corrupt, 1, running), "corrupt APP_TEST header is not Launcher");

  FlashSlot truncated = slot(kLauncherSlotSubtype, 0x10000, kRealHeader, 4);
  expect(!launcherPresent(&truncated, 1, running), "unreadable header is not Launcher");

  FlashSlot self = slot(kLauncherSlotSubtype, running, kRealHeader, 24);
  expect(!launcherPresent(&self, 1, running), "the running image is not a separate Launcher");

  FlashSlot factory = slot(0x00, 0x10000, kRealHeader, 24);
  expect(!launcherPresent(&factory, 1, running), "a factory image is not the Launcher slot");
}

int main() {
  testHeader();
  testSlots();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
