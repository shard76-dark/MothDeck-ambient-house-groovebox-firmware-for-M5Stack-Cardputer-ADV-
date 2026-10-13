#include "SdLimit.h"

#include <nvs.h>
#include <nvs_flash.h>

static const char *kNs = "moth";
static const char *kKey = "sd_bytes";

static bool openNs(nvs_handle_t *out, nvs_open_mode_t mode) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    return false;
  }
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return false;
  }
  return nvs_open(kNs, mode, out) == ESP_OK;
}

uint64_t mothSdVerifiedBytes() {
  nvs_handle_t handle;
  if (!openNs(&handle, NVS_READONLY)) {
    return 0;
  }
  uint64_t n = 0;
  if (nvs_get_u64(handle, kKey, &n) != ESP_OK) {
    n = 0;
  }
  nvs_close(handle);
  return n;
}

void mothSdStoreVerified(uint64_t bytes) {
  nvs_handle_t handle;
  if (!openNs(&handle, NVS_READWRITE)) {
    return;
  }
  if (nvs_set_u64(handle, kKey, bytes) == ESP_OK) {
    nvs_commit(handle);
  }
  nvs_close(handle);
}

bool mothSdVolumeOk(uint64_t volumeBytes) {
  uint64_t cap = mothSdVerifiedBytes();
  if (cap == 0) {
    return true;
  }
  return volumeBytes > 0 && volumeBytes <= cap;
}
