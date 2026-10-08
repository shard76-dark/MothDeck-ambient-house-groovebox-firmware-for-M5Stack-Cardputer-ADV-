#include <Arduino.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLESecurity.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <services/gap/ble_svc_gap.h>
#include "esp32-hal-bt.h"
#include "BleAdvert.h"
#include "BleMidi.h"
#include "BoardConfig.h"

static const char *kMidiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *kMidiCharUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
static const int kQueueLen = 16;

// Legacy connectable advertising interval, units of 0.625 ms. 0x20..0x40 is
// 20..40 ms, the range Apple scans for and hardware hosts still catch.
static const uint16_t kAdvIntervalMin = 0x20;
static const uint16_t kAdvIntervalMax = 0x40;

// NimBLE's controller and host pools come out of internal SRAM
// (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL). The Stamp-S3A has no PSRAM.
// A largest free block under this size is reported as "no mem": the
// controller wants one contiguous internal block, and init returns false
// rather than advertising.
static const uint32_t kBleMinLargest = 64 * 1024;

static BLECharacteristic *midiChar = nullptr;
static portMUX_TYPE midiMux = portMUX_INITIALIZER_UNLOCKED;
static MidiEvent midiQueue[kQueueLen];
static int qHead = 0;
static int qTail = 0;
static volatile bool connected = false;
static volatile int connectEdge = 0;
static MidiRunningState running;

static BleAdvertPackets pkts;
static bool initOk = false;
static bool serverOk = false;
static char failReason[24] = "not started";
static uint32_t heapFreeAtInit = 0;
static uint32_t heapLargestAtInit = 0;
static uint32_t lastMaintainMs = 0;
static uint32_t lastAdvQueryMs = 0;
static bool lastAdvActive = false;
static char statusBuf[44];
static char diagBuf[44];

static bool pushEvent(const MidiEvent &event) {
  portENTER_CRITICAL(&midiMux);
  int next = (qHead + 1) % kQueueLen;
  if (next == qTail) {
    portEXIT_CRITICAL(&midiMux);
    return false;
  }
  midiQueue[qHead] = event;
  qHead = next;
  portEXIT_CRITICAL(&midiMux);
  return true;
}

class MidiCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (!characteristic) {
      return;
    }
    const uint8_t *data = characteristic->getData();
    size_t len = characteristic->getLength();
    if (!data || len == 0) {
      return;
    }
    MidiParseResult parsed;
    parseBleMidiPacket(data, (int)len, &running, &parsed);
    for (int i = 0; i < parsed.count; i++) {
      pushEvent(parsed.events[i]);
    }
  }
};

class MidiServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    connected = true;
    connectEdge = 1;
  }

  void onDisconnect(BLEServer *server) override {
    (void)server;
    connected = false;
    connectEdge = -1;
    BLESecurity::resetSecurity();
    BLEDevice::startAdvertising();
  }
};

static void logBytes(const char *label, const uint8_t *data, int len) {
  Serial.printf("%s (%d):", label, len);
  for (int i = 0; i < len; i++) {
    Serial.printf(" %02X", data[i]);
  }
  Serial.println();
}

static bool payloadFits(BLEAdvertisementData &data, const uint8_t *bytes, int len) {
  data.addData((char *)bytes, (size_t)len);
  return (int)data.getPayload().length() == len;
}

static void snapHeap(uint32_t *freeBytes, uint32_t *largest) {
  *freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  *largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static const char *ctrlName(esp_bt_controller_status_t status) {
  switch (status) {
    case ESP_BT_CONTROLLER_STATUS_IDLE: return "idle";
    case ESP_BT_CONTROLLER_STATUS_INITED: return "inited";
    case ESP_BT_CONTROLLER_STATUS_ENABLED: return "enabled";
    default: return "other";
  }
}

static void configureSecurity() {
  // Bond + LE Secure Connections, no MITM. With no keyboard and no display
  // this is Just Works: the MPC, iOS, and Android can pair without a PIN.
  // The host starts pairing. MIDI bytes are not gated on encryption, so a
  // host that never pairs still connects. NimBLE would otherwise begin
  // security during service discovery.
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setForceAuthentication(false);
}

// This firmware's cardputer-adv build uses NimBLE. ble_gap_adv_set_data is
// synchronous, so both payloads are committed before start(). The library's
// own builder moves a name that does not fit into the scan response and
// leaves the primary packet unnamed. Hardware hosts that do not read the
// scan response then never list the device.
// Returns true only when the controller reports advertising active.
// Stops first: NimBLE's start() returns immediately when already active
// and does not reload a custom payload. A host reset clears the bytes the
// controller is sending and then restarts with that empty payload.
static bool radioStart(bool logPackets) {
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  if (!advertising) {
    Serial.println("BLE: no advertising object");
    return false;
  }
  if (advertising->isAdvertising()) {
    advertising->stop();
  }
  advertising->setMinInterval(kAdvIntervalMin);
  advertising->setMaxInterval(kAdvIntervalMax);

  if (logPackets) {
    logBytes("BLE adv", pkts.adv, pkts.advLen);
    logBytes("BLE scan", pkts.scan, pkts.scanLen);
  }

  BLEAdvertisementData primary;
  if (!payloadFits(primary, pkts.adv, pkts.advLen)) {
    // The length check failed before any custom payload was committed, so
    // the library builder can still run. Scan response is off, which makes
    // that builder shorten the name into the primary packet instead of
    // dropping it.
    Serial.println("BLE: advert payload truncated; shortening the name");
    advertising->addServiceUUID(kMidiServiceUuid);
    advertising->setScanResponse(false);
    if (!advertising->start()) {
      Serial.println("BLE: advertising failed to start");
      return false;
    }
    bool active = advertising->isAdvertising();
    lastAdvActive = active;
    lastAdvQueryMs = millis();
    Serial.printf("BLE: advertising active=%d\n", active ? 1 : 0);
    return active;
  }
  if (!advertising->setAdvertisementData(primary)) {
    // The library sets its custom-data flag even when this returns false,
    // so the built-in builder will not run afterwards. Do not call start
    // on an empty payload.
    Serial.println("BLE: advert config rejected");
    return false;
  }

  BLEAdvertisementData scan;
  if (!payloadFits(scan, pkts.scan, pkts.scanLen) || !advertising->setScanResponseData(scan)) {
    Serial.println("BLE: scan response rejected");
  }
  advertising->setScanResponse(true);
  if (!advertising->start()) {
    Serial.println("BLE: advertising failed to start");
    return false;
  }
  bool active = advertising->isAdvertising();
  lastAdvActive = active;
  lastAdvQueryMs = millis();
  Serial.printf("BLE: advertising active=%d\n", active ? 1 : 0);
  return active;
}

BleMidi::BleMidi() {
  started = false;
}

bool BleMidi::Begin(const char *name) {
  if (started && initOk) {
    return true;
  }
  buildBleMidiAdvert(name, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  memset(&running, 0, sizeof(running));
  snapHeap(&heapFreeAtInit, &heapLargestAtInit);
  esp_bt_controller_status_t ctrl = esp_bt_controller_get_status();
  Serial.printf(
    "BLE: before init free=%u largest=%u ctrl=%s psram=%d\n",
    (unsigned)heapFreeAtInit,
    (unsigned)heapLargestAtInit,
    ctrlName(ctrl),
    psramFound() ? 1 : 0);

  if (btMemReleased(BT_MODE_BLE)) {
    snprintf(failReason, sizeof(failReason), "mem released");
    initOk = false;
    Serial.printf(
      "BLE: init failed (mem released) free=%u largest=%u ctrl=%s\n",
      (unsigned)heapFreeAtInit,
      (unsigned)heapLargestAtInit,
      ctrlName(ctrl));
    return false;
  }

  bool inited = BLEDevice::init(pkts.gapName);
  uint32_t freeAfter = 0;
  uint32_t largestAfter = 0;
  snapHeap(&freeAfter, &largestAfter);
  ctrl = esp_bt_controller_get_status();
  if (!inited) {
    if (heapLargestAtInit < kBleMinLargest) {
      snprintf(failReason, sizeof(failReason), "no mem");
    } else {
      snprintf(failReason, sizeof(failReason), "other");
    }
    initOk = false;
    Serial.printf(
      "BLE: init failed (%s) free=%u largest=%u ctrl=%s\n",
      failReason,
      (unsigned)heapFreeAtInit,
      (unsigned)heapLargestAtInit,
      ctrlName(ctrl));
    Serial.printf("BLE: after init attempt free=%u largest=%u\n", (unsigned)freeAfter, (unsigned)largestAfter);
    return false;
  }
  initOk = true;
  Serial.printf(
    "BLE: init ok free=%u largest=%u ctrl=%s\n",
    (unsigned)freeAfter,
    (unsigned)largestAfter,
    ctrlName(ctrl));

  BLEDevice::setMTU(256);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  configureSecurity();
  BLEServer *server = BLEDevice::createServer();
  if (!server) {
    snprintf(failReason, sizeof(failReason), "no server");
    serverOk = false;
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  server->setCallbacks(new MidiServerCallbacks());
  BLEService *service = server->createService(kMidiServiceUuid);
  if (!service) {
    snprintf(failReason, sizeof(failReason), "no server");
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  midiChar = service->createCharacteristic(
    kMidiCharUuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  if (!midiChar) {
    snprintf(failReason, sizeof(failReason), "no server");
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  midiChar->setCallbacks(new MidiCharCallbacks());
  service->start();
  serverOk = true;
  failReason[0] = 0;

  bool active = radioStart(true);
  started = true;
  if (active) {
    Serial.printf("BLE MIDI advertising as %s (air %s)\n", pkts.gapName, pkts.advName);
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  return active;
}

bool BleMidi::Restart(const char *name) {
  // deinit() gives the controller memory back, and taking it again after
  // the sprite and the audio DMA exist can fail. Change the payload in
  // place when the stack is already up.
  if (!initOk || !serverOk) {
    started = false;
    return Begin(name);
  }
  buildBleMidiAdvert(name, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  int rc = ble_svc_gap_device_name_set(pkts.gapName);
  Serial.printf("BLE: rename rc=%d name=%s air=%s\n", rc, pkts.gapName, pkts.advName);
  bool active = radioStart(true);
  if (active) {
    failReason[0] = 0;
    Serial.printf("BLE MIDI advertising as %s (air %s)\n", pkts.gapName, pkts.advName);
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  return active;
}

void BleMidi::RecommitAfter(const char *where) {
  uint32_t freeB = 0;
  uint32_t largest = 0;
  snapHeap(&freeB, &largest);
  const char *tag = where && where[0] ? where : "later";
  if (!initOk || !serverOk) {
    Serial.printf(
      "BLE: after %s free=%u largest=%u active=0 (init never came up)\n",
      tag,
      (unsigned)freeB,
      (unsigned)largest);
    return;
  }
  Serial.printf("BLE: after %s free=%u largest=%u recommit\n", tag, (unsigned)freeB, (unsigned)largest);
  bool active = radioStart(false);
  if (active) {
    failReason[0] = 0;
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  Serial.printf("BLE: after %s active=%d\n", tag, active ? 1 : 0);
}

void BleMidi::Maintain() {
  if (!initOk || !serverOk || connected) {
    return;
  }
  uint32_t now = millis();
  if ((uint32_t)(now - lastMaintainMs) < 1000) {
    return;
  }
  lastMaintainMs = now;
  if (Advertising()) {
    return;
  }
  Serial.println("BLE: advertising dropped; restarting");
  bool active = radioStart(false);
  if (active) {
    failReason[0] = 0;
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  Serial.printf("BLE: restart active=%d\n", active ? 1 : 0);
}

bool BleMidi::Advertising() {
  if (!initOk) {
    return false;
  }
  uint32_t now = millis();
  if (lastAdvQueryMs != 0 && (uint32_t)(now - lastAdvQueryMs) < 200) {
    return lastAdvActive;
  }
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  lastAdvActive = advertising && advertising->isAdvertising();
  lastAdvQueryMs = now;
  return lastAdvActive;
}

const char *BleMidi::StatusLine() {
  if (connected) {
    return "MIDI connected";
  }
  if (Advertising()) {
    return "MIDI advertising";
  }
  const char *why = failReason[0] ? failReason : "not advertising";
  if (!initOk || strcmp(why, "no server") == 0) {
    if (strcmp(why, "no mem") == 0 || strcmp(why, "mem released") == 0 || strcmp(why, "other") == 0 || strcmp(why, "no server") == 0) {
      snprintf(statusBuf, sizeof(statusBuf), "BLE off: init failed (%s)", why);
    } else {
      snprintf(statusBuf, sizeof(statusBuf), "BLE off: init failed");
    }
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "BLE off: %s", why);
  }
  return statusBuf;
}

const char *BleMidi::DiagLine() {
  snprintf(
    diagBuf,
    sizeof(diagBuf),
    "heap %uk blk %uk adv %s",
    (unsigned)(heapFreeAtInit / 1024),
    (unsigned)(heapLargestAtInit / 1024),
    Advertising() ? "on" : "off");
  return diagBuf;
}

bool BleMidi::Connected() const {
  return connected;
}

int BleMidi::Poll(MidiEvent *out, int maxOut) {
  if (!out || maxOut <= 0) {
    return 0;
  }
  int count = 0;
  portENTER_CRITICAL(&midiMux);
  while (qTail != qHead && count < maxOut) {
    out[count++] = midiQueue[qTail];
    qTail = (qTail + 1) % kQueueLen;
  }
  portEXIT_CRITICAL(&midiMux);
  return count;
}

int BleMidi::ConsumeConnectEdge() {
  portENTER_CRITICAL(&midiMux);
  int edge = connectEdge;
  connectEdge = 0;
  portEXIT_CRITICAL(&midiMux);
  return edge;
}

void BleMidi::Send(const MidiEvent &event) {
  if (!started || !connected || !midiChar) {
    return;
  }
  uint8_t packet[5];
  int n = buildBleMidiPacket(packet, (int)sizeof(packet), (uint16_t)millis(), event.type, event.channel, event.number, event.value);
  if (n <= 0) {
    return;
  }
  midiChar->setValue(packet, n);
  midiChar->notify();
}
