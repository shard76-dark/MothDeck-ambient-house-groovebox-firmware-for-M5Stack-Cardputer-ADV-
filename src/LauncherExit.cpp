#include "LauncherExit.h"
#include "LauncherDetect.h"
#include "DevLog.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <string.h>

// Read every app slot and accept Launcher only when the APP_TEST image
// header is a real ESP32-S3 app. otadata is erased only after that check.
bool launcherInstalled() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  uint32_t runningAddress = running ? running->address : 0xFFFFFFFFu;
  FlashSlot slots[8];
  uint8_t headers[8][kAppHeaderBytes];
  memset(slots, 0, sizeof(slots));
  memset(headers, 0, sizeof(headers));
  int count = 0;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it && count < 8) {
    const esp_partition_t *part = esp_partition_get(it);
    if (part) {
      FlashSlot &slot = slots[count];
      slot.exists = true;
      slot.subtype = part->subtype;
      slot.address = part->address;
      slot.size = part->size;
      if (part->size >= (uint32_t)kAppHeaderBytes &&
          esp_partition_read(part, 0, headers[count], kAppHeaderBytes) == ESP_OK) {
        slot.header = headers[count];
        slot.headerLen = kAppHeaderBytes;
      }
      count++;
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  return launcherPresent(slots, count, runningAddress);
}

bool exitToLauncher() {
  if (!launcherInstalled()) {
    DEV_LOG("Launcher: not verified, otadata left untouched");
    return false;
  }
  const esp_partition_t *ota =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!ota || esp_partition_erase_range(ota, 0, ota->size) != ESP_OK) {
    DEV_LOG("Launcher: could not clear otadata");
    return false;
  }
  DEV_LOG("Launcher: cleared otadata, restarting");
  delay(40);
  esp_restart();
  return true;
}
