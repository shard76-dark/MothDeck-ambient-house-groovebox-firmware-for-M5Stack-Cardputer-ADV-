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
#include "DevLog.h"

static const char *kMidiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *kMidiCharUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
// One slot stays empty so head == tail means empty. The producer is the
// NimBLE host task and the consumer is the audio task.
static const int kQueueLen = 128;

// Legacy connectable advertising interval, units of 0.625 ms. 0x20..0x40 is
// 20..40 ms, the range Apple scans for and hardware hosts still catch.
static const uint16_t kAdvIntervalMin = 0x20;
static const uint16_t kAdvIntervalMax = 0x40;

// NimBLE's controller and host pools come out of internal SRAM
// (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL). The Stamp-S3A has no PSRAM.
// A largest free block under this size is reported as "no mem": the
// controller wants one contiguous internal block, and init returns false
// rather than advertising.
// Below this, nimble_port_init is not called. A failed controller alloc can
// still consume the largest internal block, and playback is already using
// its own. 36KB is the floor at which the prebuilt NimBLE image is worth
// attempting; the Arduino libs ship with the pool sizes baked in
// (3 connections, internal RAM only) and those CONFIG_BT_NIMBLE_* values
// cannot be trimmed without rebuilding libbt.a.
static const uint32_t kBleMinLargest = 36 * 1024;

static BLECharacteristic *midiChar = nullptr;
static BLEServer *midiServer = nullptr;
static portMUX_TYPE midiMux = portMUX_INITIALIZER_UNLOCKED;
static MidiEvent midiQueue[kQueueLen];
static uint32_t qHead = 0;
static uint32_t qTail = 0;
static uint32_t statPackets = 0;
static uint32_t statMessages = 0;
static uint32_t statClocks = 0;
static uint32_t statNotes = 0;
static uint32_t statOverflow = 0;
static volatile bool connected = false;
static volatile int connectEdge = 0;
static uint16_t connHandle = 0xFFFF;
static bool connParamsPending = false;
static uint32_t connParamDue = 0;
static uint8_t connParamTries = 0;
static volatile uint16_t linkInterval = 0;
static MidiRunningState running;
static MidiParseResult parsed;

static BleAdvertPackets pkts;
static bool initOk = false;
static bool serverOk = false;
static char failReason[24] = "not started";
static uint32_t heapFreeAtInit = 0;
static uint32_t heapLargestAtInit = 0;
static uint32_t lastMaintainMs = 0;
static uint32_t lastStatMs = 0;
static uint32_t lastAdvQueryMs = 0;
static bool lastAdvActive = false;
static volatile bool userEnabled = false;
static volatile bool restartAdvert = false;
static bool pendingUnload = false;
static char statusBuf[44];
static char diagBuf[44];
static char counterBuf[44];
static char linkBuf[20];

static bool pushEvent(const MidiEvent &event) {
  uint32_t head = __atomic_load_n(&qHead, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&qTail, __ATOMIC_ACQUIRE);
  uint32_t next = (head + 1) % kQueueLen;
  if (next == tail) {
    __atomic_fetch_add(&statOverflow, 1, __ATOMIC_RELAXED);
    return false;
  }
  midiQueue[head] = event;
  __atomic_store_n(&qHead, next, __ATOMIC_RELEASE);
  return true;
}

static void countParsed(const MidiEvent &event) {
  __atomic_fetch_add(&statMessages, 1, __ATOMIC_RELAXED);
  if (event.type == MIDI_MSG_CLOCK) {
    __atomic_fetch_add(&statClocks, 1, __ATOMIC_RELAXED);
  } else if (event.type == MIDI_MSG_NOTE_ON || event.type == MIDI_MSG_NOTE_OFF) {
    __atomic_fetch_add(&statNotes, 1, __ATOMIC_RELAXED);
  }
}

class MidiCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (!userEnabled || !characteristic) {
      return;
    }
    const uint8_t *data = characteristic->getData();
    size_t len = characteristic->getLength();
    if (!data || len == 0) {
      return;
    }
    __atomic_fetch_add(&statPackets, 1, __ATOMIC_RELAXED);
    parseBleMidiPacket(data, (int)len, &running, &parsed);
    if (parsed.overflow > 0) {
      __atomic_fetch_add(&statOverflow, (uint32_t)parsed.overflow, __ATOMIC_RELAXED);
    }
    for (int i = 0; i < parsed.count; i++) {
      countParsed(parsed.events[i]);
      pushEvent(parsed.events[i]);
    }
  }
};

static void requestShortInterval(BLEServer *server, uint16_t handle) {
  if (!server || handle == 0xFFFF) {
    return;
  }
  // Interval units are 1.25 ms. 6..12 is 7.5..15 ms. Timeout is in 10 ms
  // units, and it has to be longer than the interval.
  bool ok = server->requestConnParams(handle, 6, 12, 0, 500);
  Serial.printf("BLE: conn interval request 7.5-15ms handle=%u ok=%d try=%u\n", handle, ok ? 1 : 0, connParamTries);
  if (ok) {
    connParamsPending = false;
    return;
  }
  if (connParamTries < 4) {
    connParamsPending = true;
    connParamDue = millis() + 250;
  } else {
    connParamsPending = false;
    Serial.println("BLE: conn interval request failed");
  }
}

class MidiServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    if (!userEnabled) {
      return;
    }
    connected = true;
    connectEdge = 1;
  }

  void onConnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    if (!userEnabled) {
      if (desc && server) {
        server->disconnect(desc->conn_handle);
      }
      return;
    }
    connected = true;
    connectEdge = 1;
    if (desc) {
      connHandle = desc->conn_handle;
      // 1.1.0 asked for 7.5-15 ms inside this callback. That races the
      // pairing exchange. Wait until the link has been up for a quarter
      // second, then ask. The callback below prints the interval the
      // central actually accepted.
      linkInterval = 0;
      connParamTries = 0;
      connParamsPending = true;
      connParamDue = millis() + 250;
    }
  }

  void onDisconnect(BLEServer *server) override {
    (void)server;
    connected = false;
    connectEdge = -1;
    connParamsPending = false;
    connParamDue = 0;
    connHandle = 0xFFFF;
    linkInterval = 0;
    running.sysex = 0;
    BLESecurity::resetSecurity();
    if (userEnabled) {
      // Same call 1.1.0 made from this callback. Rebuilding the payload
      // here would run inside the host event. Maintain reloads the name
      // and the MIDI UUID on the next pass.
      restartAdvert = true;
      BLEDevice::startAdvertising();
    }
  }

  void onConnParamsUpdate(uint16_t conn_handle, uint16_t interval, uint16_t latency, uint16_t timeout, uint8_t status) override {
    linkInterval = interval;
    Serial.printf(
      "BLE: conn interval %u (%.1f ms) latency=%u timeout=%u status=%u\n",
      interval,
      (double)interval * 1.25,
      latency,
      timeout,
      status);
    if (status == 0) {
      connParamsPending = false;
      return;
    }
    if (connParamTries < 4) {
      connParamsPending = true;
      connParamDue = millis() + 250;
    }
    (void)conn_handle;
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
  // Used only if the custom payload is rejected and the library builder
  // runs. The scan response already carries 7.5-15 ms for the normal path.
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x0C);

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

static void stopRadio() {
  userEnabled = false;
  if (initOk) {
    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    if (advertising) {
      advertising->stop();
    }
  }
  lastAdvActive = false;
  lastAdvQueryMs = millis();
  if (midiServer && connected && connHandle != 0xFFFF) {
    midiServer->disconnect(connHandle);
  }
}

BleMidi::BleMidi() {
  started = false;
}

bool BleMidi::Begin(const char *name, bool advertise) {
  if (started && initOk && serverOk) {
    if (!advertise) {
      stopRadio();
      return true;
    }
    userEnabled = true;
    pendingUnload = false;
    bool active = radioStart(false);
    if (active) {
      failReason[0] = 0;
    } else {
      snprintf(failReason, sizeof(failReason), "not advertising");
    }
    return active;
  }
  userEnabled = advertise;
  buildBleMidiAdvert(name, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  memset(&running, 0, sizeof(running));
  snapHeap(&heapFreeAtInit, &heapLargestAtInit);
  esp_bt_controller_status_t ctrl = esp_bt_controller_get_status();
  DEV_LOGF(
    "BLE: before init free=%u largest=%u ctrl=%s psram=%d\n",
    (unsigned)heapFreeAtInit,
    (unsigned)heapLargestAtInit,
    ctrlName(ctrl),
    psramFound() ? 1 : 0);

  if (heapLargestAtInit < kBleMinLargest) {
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "no mem");
    initOk = false;
    Serial.printf(
      "BLE: init failed (no mem) free=%u largest=%u ctrl=%s\n",
      (unsigned)heapFreeAtInit,
      (unsigned)heapLargestAtInit,
      ctrlName(ctrl));
    Serial.println("BLE: skipped init, largest block below 36k");
    return false;
  }

  if (btMemReleased(BT_MODE_BLE)) {
    userEnabled = false;
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
    userEnabled = false;
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
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "no server");
    serverOk = false;
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  midiServer = server;
  server->setCallbacks(new MidiServerCallbacks());
  BLEService *service = server->createService(kMidiServiceUuid);
  if (!service) {
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "no server");
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  midiChar = service->createCharacteristic(
    kMidiCharUuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  if (!midiChar) {
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "no server");
    Serial.println("BLE: init failed (no server)");
    return false;
  }
  midiChar->setCallbacks(new MidiCharCallbacks());
  DEV_LOG("BLE: write and write-without-response enabled");
  service->start();
  serverOk = true;
  failReason[0] = 0;
  started = true;
  if (!advertise) {
    userEnabled = false;
    Serial.printf("BLE MIDI ready as %s, advertising off\n", pkts.gapName);
    return true;
  }
  userEnabled = true;
  bool active = radioStart(true);
  if (active) {
    Serial.printf("BLE MIDI advertising as %s (air %s)\n", pkts.gapName, pkts.advName);
  } else {
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  return active;
}

void BleMidi::SetUserEnabled(bool on) {
  if (!initOk || !serverOk) {
    userEnabled = false;
    return;
  }
  if (!on) {
    stopRadio();
    Serial.println("BLE: radio off");
    return;
  }
  userEnabled = true;
  bool active = radioStart(false);
  if (active) {
    failReason[0] = 0;
    Serial.println("BLE: radio on");
  } else {
    userEnabled = false;
    snprintf(failReason, sizeof(failReason), "not advertising");
    Serial.println("BLE: radio on, advertising failed");
  }
}

bool BleMidi::UserEnabled() const {
  return userEnabled;
}

bool BleMidi::Resident() const {
  return initOk && serverOk;
}

void BleMidi::MarkSkipped() {
  userEnabled = false;
  pendingUnload = false;
  initOk = false;
  serverOk = false;
  started = false;
  snprintf(failReason, sizeof(failReason), "unloaded");
}

void BleMidi::MarkIdle() {
  userEnabled = false;
  pendingUnload = false;
  initOk = false;
  serverOk = false;
  started = false;
  snprintf(failReason, sizeof(failReason), "idle");
}

const char *BleMidi::LinkLine() {
  if (!connected) {
    return "link --";
  }
  uint16_t interval = linkInterval;
  if (interval == 0) {
    return "link ...";
  }
  unsigned x10 = (unsigned)interval * 25 / 2;
  snprintf(linkBuf, sizeof(linkBuf), "link %u.%ums", x10 / 10, x10 % 10);
  return linkBuf;
}

void BleMidi::SetPendingUnload(bool pending) {
  pendingUnload = pending && initOk && serverOk;
}

const char *BleMidi::SwitchLine() {
  if (!initOk || !serverOk) {
    if (strcmp(failReason, "no mem") == 0 || strcmp(failReason, "mem released") == 0) {
      return "no mem";
    }
    if (strcmp(failReason, "unloaded") == 0) {
      return "unloaded";
    }
    return "Off";
  }
  if (!userEnabled) {
    return "Off";
  }
  if (connected) {
    return "On conn";
  }
  if (Advertising()) {
    return "On adv";
  }
  return "On";
}

bool BleMidi::Restart(const char *name) {
  // deinit() gives the controller memory back, and taking it again after
  // the sprite and the audio DMA exist can fail. Change the payload in
  // place when the stack is already up.
  if (!initOk || !serverOk) {
    started = false;
    return Begin(name, userEnabled);
  }
  buildBleMidiAdvert(name, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  int rc = ble_svc_gap_device_name_set(pkts.gapName);
  DEV_LOGF("BLE: rename rc=%d name=%s air=%s\n", rc, pkts.gapName, pkts.advName);
  if (!userEnabled) {
    return true;
  }
  bool active = radioStart(MOTHOS_DEV_LOG != 0);
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
  if (!initOk || !serverOk || !userEnabled) {
    DEV_LOGF(
      "BLE: after %s free=%u largest=%u active=0 (init never came up)\n",
      tag,
      (unsigned)freeB,
      (unsigned)largest);
    return;
  }
  DEV_LOGF("BLE: after %s free=%u largest=%u recommit\n", tag, (unsigned)freeB, (unsigned)largest);
  bool active = radioStart(false);
  if (active) {
    failReason[0] = 0;
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  DEV_LOGF("BLE: after %s active=%d\n", tag, active ? 1 : 0);
}

void BleMidi::Maintain() {
  uint32_t now = millis();
  if (initOk && (uint32_t)(now - lastStatMs) >= 1000) {
    lastStatMs = now;
    uint32_t packets = __atomic_load_n(&statPackets, __ATOMIC_RELAXED);
    if (connected || packets != 0) {
      DEV_LOGF(
        "MIDI: pk=%lu msg=%lu clk=%lu note=%lu ovf=%lu\n",
        (unsigned long)packets,
        (unsigned long)__atomic_load_n(&statMessages, __ATOMIC_RELAXED),
        (unsigned long)__atomic_load_n(&statClocks, __ATOMIC_RELAXED),
        (unsigned long)__atomic_load_n(&statNotes, __ATOMIC_RELAXED),
        (unsigned long)__atomic_load_n(&statOverflow, __ATOMIC_RELAXED));
    }
  }
  if (restartAdvert && userEnabled && initOk && serverOk && !connected) {
    restartAdvert = false;
    bool active = radioStart(false);
    if (active) {
      failReason[0] = 0;
    } else {
      snprintf(failReason, sizeof(failReason), "not advertising");
    }
  }
  if (initOk && connected && userEnabled && connParamsPending && connParamDue != 0 && (int32_t)(now - connParamDue) >= 0) {
    connParamTries++;
    requestShortInterval(midiServer, connHandle);
  }
  if (!userEnabled || !initOk || !serverOk || connected) {
    return;
  }
  if ((uint32_t)(now - lastMaintainMs) < 1000) {
    return;
  }
  lastMaintainMs = now;
  if (Advertising()) {
    return;
  }
  DEV_LOG("BLE: advertising dropped; restarting");
  bool active = radioStart(false);
  if (active) {
    failReason[0] = 0;
  } else {
    snprintf(failReason, sizeof(failReason), "not advertising");
  }
  if (!active) {
    Serial.println("BLE: advertising failed to restart");
  }
  DEV_LOGF("BLE: restart active=%d\n", active ? 1 : 0);
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
  if (!initOk && strcmp(failReason, "unloaded") == 0) {
    return "BLE unloaded";
  }
  if (!initOk && strcmp(failReason, "idle") == 0) {
    return "BLE off";
  }
  if (initOk && serverOk && !userEnabled) {
    return pendingUnload ? "BLE reboot to unload" : "BLE off";
  }
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
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snprintf(
    diagBuf,
    sizeof(diagBuf),
    "heap %uk blk %uk adv %s",
    (unsigned)(freeB / 1024),
    (unsigned)(largest / 1024),
    Advertising() ? "on" : "off");
  return diagBuf;
}

bool BleMidi::Connected() const {
  return connected;
}

void BleMidi::CopyCounters(MidiCounters *out) const {
  if (!out) {
    return;
  }
  out->packets = __atomic_load_n(&statPackets, __ATOMIC_RELAXED);
  out->messages = __atomic_load_n(&statMessages, __ATOMIC_RELAXED);
  out->clocks = __atomic_load_n(&statClocks, __ATOMIC_RELAXED);
  out->notes = __atomic_load_n(&statNotes, __ATOMIC_RELAXED);
  out->overflows = __atomic_load_n(&statOverflow, __ATOMIC_RELAXED);
}

const char *BleMidi::CounterLine() {
  MidiCounters c;
  CopyCounters(&c);
  snprintf(
    counterBuf,
    sizeof(counterBuf),
    "p%lu m%lu c%lu n%lu o%lu",
    (unsigned long)c.packets,
    (unsigned long)c.messages,
    (unsigned long)c.clocks,
    (unsigned long)c.notes,
    (unsigned long)c.overflows);
  return counterBuf;
}

int BleMidi::Poll(MidiEvent *out, int maxOut) {
  if (!out || maxOut <= 0) {
    return 0;
  }
  if (!userEnabled) {
    uint32_t head = __atomic_load_n(&qHead, __ATOMIC_ACQUIRE);
    __atomic_store_n(&qTail, head, __ATOMIC_RELEASE);
    return 0;
  }
  uint32_t tail = __atomic_load_n(&qTail, __ATOMIC_RELAXED);
  uint32_t head = __atomic_load_n(&qHead, __ATOMIC_ACQUIRE);
  int count = 0;
  while (tail != head && count < maxOut) {
    out[count++] = midiQueue[tail];
    tail = (tail + 1) % kQueueLen;
  }
  __atomic_store_n(&qTail, tail, __ATOMIC_RELEASE);
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
  if (!userEnabled || !started || !connected || !midiChar) {
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
