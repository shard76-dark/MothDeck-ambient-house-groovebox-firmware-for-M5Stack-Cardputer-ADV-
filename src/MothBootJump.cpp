#include "MothBootJump.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

static bool openNs(nvs_handle_t *out) {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return false;
  }
  return nvs_open("moth", NVS_READWRITE, out) == ESP_OK;
}

static void clearTries() {
  nvs_handle_t handle;
  if (!openNs(&handle)) {
    return;
  }
  nvs_set_u8(handle, "boot_try", 0);
  nvs_commit(handle);
  nvs_close(handle);
}

void mothBootGuard() {
  nvs_handle_t handle;
  if (!openNs(&handle)) {
    return;
  }
  uint8_t tries = 0;
  nvs_get_u8(handle, "boot_try", &tries);
  if (tries >= 3) {
    nvs_set_u8(handle, "boot_try", 0);
    nvs_commit(handle);
    nvs_close(handle);
    mothRebootToLauncher();
    return;
  }
  nvs_set_u8(handle, "boot_try", (uint8_t)(tries + 1));
  nvs_commit(handle);
  nvs_close(handle);
}

void mothMarkAppValid() {
  clearTries();
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return;
  }
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

bool mothRebootToLauncher() {
  const esp_partition_t *factory = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!factory) {
    return false;
  }
  if (esp_ota_set_boot_partition(factory) != ESP_OK) {
    return false;
  }
  esp_restart();
  return true;
}
