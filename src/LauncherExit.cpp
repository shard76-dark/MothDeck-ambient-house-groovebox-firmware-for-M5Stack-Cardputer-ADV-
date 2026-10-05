#include "LauncherExit.h"
#include "DevLog.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

static bool different(const esp_partition_t *part, const esp_partition_t *running) {
  return part && (!running || part->address != running->address);
}

// Launcher (bmorcelli/Launcher, read from current main) keeps itself in an
// APP_TEST slot. Installed apps are real OTA subtypes, and Launcher selects
// one by writing otadata (launcherPartitionSetOtaBoot). The matching clear
// is launcherPartitionClearOtaBoot, which erases that partition.
//
// esp_ota_set_boot_partition() is the wrong call for APP_TEST. In ESP-IDF
// 5.5 it only special-cases FACTORY (erase otadata). Every other subtype,
// including TEST (0x20), is passed to esp_rewrite_ota_data(), which keeps
// only the low nibble. 0x20 therefore selects OTA slot 0, not Launcher.
// A FACTORY slot, when one exists and it is not the image we are running,
// can be selected with the API. The TEST slot is selected by erasing
// otadata and restarting, which is the clear path Launcher itself uses.
bool exitToLauncher() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *test =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  const esp_partition_t *factory =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);

  if (different(factory, running)) {
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err == ESP_OK) {
      DEV_LOG("Launcher: factory selected, restarting");
      delay(40);
      esp_restart();
      return true;
    }
  }

  if (!different(test, running) && !different(factory, running)) {
    DEV_LOG("Launcher: no resident partition");
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
