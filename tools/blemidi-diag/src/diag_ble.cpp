#include "diag_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLESecurity.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <string.h>
#include "esp32-hal-bt.h"
#include "BleAdvert.h"

// These match src/BleMidi.cpp. The diagnostic has to pair and notify the
// same way MothDeck does, or a result on the bench would not apply to it.
static const char *kMidiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *kMidiCharUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
static const uint16_t kAdvIntervalMin = 0x20;
static const uint16_t kAdvIntervalMax = 0x40;
static const uint32_t kBleMinLargest = 36 * 1024;
static const int kPktSlots = 24;
static const int kPktCap = 256;
static const int kLinkSlots = 8;

struct PktSlot {
  uint64_t us;
  uint16_t len;
  uint8_t data[kPktCap];
};

static BLECharacteristic *midiChar = nullptr;
static BLEServer *midiServer = nullptr;
static BleAdvertPackets pkts;
static bool initOk = false;
static bool serverOk = false;
static volatile bool connected = false;
static uint16_t connHandle = 0xFFFF;
static bool connParamsPending = false;
static uint32_t connParamDue = 0;
static uint8_t connParamTries = 0;
static volatile bool restartAdvert = false;
static char failReason[24] = "not started";
static DiagLinkInfo linkInfo;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static PktSlot pktSlots[kPktSlots];
static volatile uint16_t pktHead = 0;
static volatile uint16_t pktTail = 0;
static uint32_t pktDrops = 0;

static DiagLinkEvent linkSlots[kLinkSlots];
static volatile uint16_t linkHead = 0;
static volatile uint16_t linkTail = 0;

static void configureSecurity() {
  // Bond + LE Secure Connections, no MITM. Just Works, no passkey.
  // The host starts pairing. MIDI bytes are not gated on encryption.
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setForceAuthentication(false);
}

static bool payloadFits(BLEAdvertisementData &data, const uint8_t *bytes, int len) {
  data.addData((char *)bytes, (size_t)len);
  return (int)data.getPayload().length() == len;
}

static bool radioStart() {
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  if (!advertising) {
    strncpy(failReason, "no advertising", sizeof(failReason) - 1);
    linkInfo.advertising = false;
    return false;
  }
  if (advertising->isAdvertising()) {
    advertising->stop();
  }
  advertising->setMinInterval(kAdvIntervalMin);
  advertising->setMaxInterval(kAdvIntervalMax);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x0C);

  BLEAdvertisementData primary;
  if (!payloadFits(primary, pkts.adv, pkts.advLen)) {
    advertising->addServiceUUID(kMidiServiceUuid);
    advertising->setScanResponse(false);
    if (!advertising->start()) {
      strncpy(failReason, "not advertising", sizeof(failReason) - 1);
      linkInfo.advertising = false;
      return false;
    }
    linkInfo.advertising = advertising->isAdvertising();
    if (linkInfo.advertising) {
      failReason[0] = 0;
    }
    return linkInfo.advertising;
  }
  if (!advertising->setAdvertisementData(primary)) {
    strncpy(failReason, "advert rejected", sizeof(failReason) - 1);
    linkInfo.advertising = false;
    return false;
  }
  BLEAdvertisementData scan;
  if (payloadFits(scan, pkts.scan, pkts.scanLen)) {
    advertising->setScanResponseData(scan);
  }
  advertising->setScanResponse(true);
  if (!advertising->start()) {
    strncpy(failReason, "not advertising", sizeof(failReason) - 1);
    linkInfo.advertising = false;
    return false;
  }
  linkInfo.advertising = advertising->isAdvertising();
  if (linkInfo.advertising) {
    failReason[0] = 0;
  } else {
    strncpy(failReason, "not advertising", sizeof(failReason) - 1);
  }
  return linkInfo.advertising;
}

static void pushLink(DiagLinkKind kind) {
  portENTER_CRITICAL(&mux);
  uint16_t next = (uint16_t)((linkHead + 1) % kLinkSlots);
  if (next == linkTail) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  linkSlots[linkHead].kind = kind;
  linkSlots[linkHead].info = linkInfo;
  linkHead = next;
  portEXIT_CRITICAL(&mux);
}

static void rememberInterval(uint16_t interval, uint16_t latency, uint16_t timeout) {
  linkInfo.interval = interval;
  linkInfo.latency = latency;
  linkInfo.timeout = timeout;
  linkInfo.hasInterval = interval != 0;
}

static void requestShortInterval(uint16_t handle) {
  if (!midiServer || handle == 0xFFFF) {
    return;
  }
  // 6..12 is 7.5..15 ms. Timeout is in 10 ms units.
  bool ok = midiServer->requestConnParams(handle, 6, 12, 0, 500);
  if (ok) {
    connParamsPending = false;
    return;
  }
  if (connParamTries < 8) {
    connParamsPending = true;
    connParamDue = millis() + 250;
  } else {
    connParamsPending = false;
  }
}

class DiagServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    (void)server;
    connected = true;
    linkInfo.connected = true;
    linkInfo.advertising = false;
    linkInfo.hasRssi = false;
    if (desc) {
      connHandle = desc->conn_handle;
      rememberInterval(desc->conn_itvl, desc->conn_latency, desc->supervision_timeout);
      // Same delay as MothDeck: asking for 7.5-15 ms inside this callback
      // races the pairing exchange. Ask a quarter second after the link is up.
      linkInfo.hasMtu = false;
      linkInfo.mtu = 0;
      if (midiServer) {
        uint16_t mtu = midiServer->getPeerMTU(connHandle);
        if (mtu > 0) {
          linkInfo.mtu = mtu;
          linkInfo.hasMtu = true;
        }
      }
      connParamTries = 0;
      connParamsPending = true;
      connParamDue = millis() + 250;
    }
    pushLink(DIAG_LINK_CONNECT);
  }

  void onDisconnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    (void)server;
    (void)desc;
    connected = false;
    linkInfo.connected = false;
    connParamsPending = false;
    connParamDue = 0;
    connHandle = 0xFFFF;
    BLESecurity::resetSecurity();
    restartAdvert = true;
    BLEDevice::startAdvertising();
    pushLink(DIAG_LINK_DISCONNECT);
  }

  void onMtuChanged(BLEServer *server, ble_gap_conn_desc *desc, uint16_t mtu) override {
    (void)server;
    if (mtu == 0) {
      return;
    }
    bool changed = !linkInfo.hasMtu || linkInfo.mtu != mtu;
    linkInfo.mtu = mtu;
    linkInfo.hasMtu = true;
    if (desc && desc->conn_itvl != 0) {
      bool intervalChanged = !linkInfo.hasInterval || linkInfo.interval != desc->conn_itvl;
      rememberInterval(desc->conn_itvl, desc->conn_latency, desc->supervision_timeout);
      changed = changed || intervalChanged;
    }
    if (changed && connected) {
      pushLink(DIAG_LINK_UPDATE);
    }
  }

  void onConnParamsUpdate(uint16_t conn_handle, uint16_t interval, uint16_t latency, uint16_t timeout, uint8_t status) override {
    (void)conn_handle;
    bool changed = interval != 0 && (!linkInfo.hasInterval || linkInfo.interval != interval || linkInfo.latency != latency || linkInfo.timeout != timeout);
    if (interval != 0) {
      rememberInterval(interval, latency, timeout);
    }
    if (changed && connected) {
      pushLink(DIAG_LINK_UPDATE);
    }
    // The first update is often the central's own interval, with status 0.
    // That is not the 7.5-15 ms request. Keep asking until the link is 15 ms
    // or faster, which is interval units of 12 or below.
    bool fast = status == 0 && interval > 0 && interval <= 12;
    if (fast || connParamTries >= 8) {
      connParamsPending = false;
      return;
    }
    if (!connParamsPending) {
      connParamsPending = true;
      connParamDue = millis() + 250;
    }
  }
};

class DiagCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (!characteristic) {
      return;
    }
    const uint8_t *data = characteristic->getData();
    size_t len = characteristic->getLength();
    if (!data || len == 0 || len > (size_t)kPktCap) {
      if (len > (size_t)kPktCap) {
        portENTER_CRITICAL(&mux);
        pktDrops++;
        portEXIT_CRITICAL(&mux);
      }
      return;
    }
    portENTER_CRITICAL(&mux);
    uint16_t next = (uint16_t)((pktHead + 1) % kPktSlots);
    if (next == pktTail) {
      pktDrops++;
      portEXIT_CRITICAL(&mux);
      return;
    }
    pktSlots[pktHead].us = (uint64_t)esp_timer_get_time();
    pktSlots[pktHead].len = (uint16_t)len;
    memcpy(pktSlots[pktHead].data, data, len);
    pktHead = next;
    portEXIT_CRITICAL(&mux);
  }
};

bool diagBleBegin() {
  memset(&linkInfo, 0, sizeof(linkInfo));
  buildBleMidiAdvert(kMothDiagName, kMothDiagName, &pkts);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (largest < kBleMinLargest) {
    strncpy(failReason, "no mem", sizeof(failReason) - 1);
    return false;
  }
  if (btMemReleased(BT_MODE_BLE)) {
    strncpy(failReason, "mem released", sizeof(failReason) - 1);
    return false;
  }
  if (!BLEDevice::init(pkts.gapName)) {
    strncpy(failReason, "init", sizeof(failReason) - 1);
    return false;
  }
  initOk = true;
  BLEDevice::setMTU(256);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  configureSecurity();
  BLEServer *server = BLEDevice::createServer();
  if (!server) {
    strncpy(failReason, "no server", sizeof(failReason) - 1);
    return false;
  }
  midiServer = server;
  server->setCallbacks(new DiagServerCallbacks());
  BLEService *service = server->createService(kMidiServiceUuid);
  if (!service) {
    strncpy(failReason, "no server", sizeof(failReason) - 1);
    return false;
  }
  midiChar = service->createCharacteristic(
    kMidiCharUuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  if (!midiChar) {
    strncpy(failReason, "no server", sizeof(failReason) - 1);
    return false;
  }
  midiChar->setCallbacks(new DiagCharCallbacks());
  service->start();
  serverOk = true;
  linkInfo.radioOk = true;
  bool active = radioStart();
  linkInfo.radioOk = initOk && serverOk;
  return active;
}

void diagBleMaintain(uint32_t nowMs) {
  if (restartAdvert && initOk && serverOk && !connected) {
    restartAdvert = false;
    radioStart();
  }
  if (initOk && connected && connParamsPending && connParamDue != 0 && (int32_t)(nowMs - connParamDue) >= 0) {
    connParamTries++;
    requestShortInterval(connHandle);
  }
  if (!initOk || !serverOk || connected) {
    return;
  }
  static uint32_t lastMaintain = 0;
  if (lastMaintain != 0 && (uint32_t)(nowMs - lastMaintain) < 1000) {
    return;
  }
  lastMaintain = nowMs;
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  bool active = advertising && advertising->isAdvertising();
  linkInfo.advertising = active;
  if (active) {
    return;
  }
  radioStart();
}

bool diagBlePopLink(DiagLinkEvent *out) {
  if (!out) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  if (linkTail == linkHead) {
    portEXIT_CRITICAL(&mux);
    return false;
  }
  *out = linkSlots[linkTail];
  linkTail = (uint16_t)((linkTail + 1) % kLinkSlots);
  portEXIT_CRITICAL(&mux);
  return true;
}

int diagBlePopPacket(uint8_t *buf, int cap, uint64_t *us) {
  if (!buf || cap <= 0) {
    return 0;
  }
  portENTER_CRITICAL(&mux);
  if (pktTail == pktHead) {
    portEXIT_CRITICAL(&mux);
    return 0;
  }
  uint16_t len = pktSlots[pktTail].len;
  if ((int)len > cap) {
    len = (uint16_t)cap;
  }
  memcpy(buf, pktSlots[pktTail].data, len);
  if (us) {
    *us = pktSlots[pktTail].us;
  }
  pktTail = (uint16_t)((pktTail + 1) % kPktSlots);
  portEXIT_CRITICAL(&mux);
  return (int)len;
}

uint32_t diagBleQueueDrops() {
  portENTER_CRITICAL(&mux);
  uint32_t drops = pktDrops;
  portEXIT_CRITICAL(&mux);
  return drops;
}

void diagBleRefreshRssi() {
  if (!connected || connHandle == 0xFFFF) {
    return;
  }
  int8_t rssi = 0;
  if (ble_gap_conn_rssi(connHandle, &rssi) == 0) {
    linkInfo.rssi = rssi;
    linkInfo.hasRssi = true;
  }
}

const DiagLinkInfo *diagBleInfo() {
  return &linkInfo;
}

const char *diagBleFailReason() {
  return failReason[0] ? failReason : "";
}
