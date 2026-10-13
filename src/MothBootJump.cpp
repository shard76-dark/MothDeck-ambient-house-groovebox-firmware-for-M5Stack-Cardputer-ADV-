#include "MothBootJump.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_pins.h"
#endif

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
#include "esp32-hal-tinyusb.h"
#endif

// TinyUSB maps the internal PHY onto the OTG controller. sw_usb_phy_sel
// lives in the RTC domain, so esp_restart() leaves it set and MothBoot's
// USB-Serial-JTAG port never enumerates. Point the PHY back first.
static void handUsbToSerialJtag() {
#if defined(ARDUINO)
  delay(200);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
  tud_disconnect();
#if defined(ARDUINO)
  delay(40);
#endif
#endif
  usb_serial_jtag_ll_phy_enable_external(false);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_PAD_ENABLE | RTC_CNTL_USB_PAD_ENABLE_OVERRIDE);
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
  pinMode(USBPHY_DM_NUM, OUTPUT_OPEN_DRAIN);
  pinMode(USBPHY_DP_NUM, OUTPUT_OPEN_DRAIN);
  digitalWrite(USBPHY_DM_NUM, LOW);
  digitalWrite(USBPHY_DP_NUM, LOW);
#if defined(ARDUINO)
  delay(80);
#endif
#endif
#endif
}

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
  handUsbToSerialJtag();
  esp_restart();
  return true;
}
