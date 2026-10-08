#include <Arduino.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLESecurity.h>
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

static BLECharacteristic *midiChar = nullptr;
static portMUX_TYPE midiMux = portMUX_INITIALIZER_UNLOCKED;
static MidiEvent midiQueue[kQueueLen];
static int qHead = 0;
static int qTail = 0;
static volatile bool connected = false;
static volatile int connectEdge = 0;
static MidiRunningState running;

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

// The library starts advertising when the primary payload is committed.
// That payload already has the name and the MIDI UUID. The hook then pauses
// once, writes the scan response (full name, appearance, connection
// interval), and lets the library start advertising again. A later
// disconnect restart leaves both payloads in place.
enum AdvPhase : uint8_t {
  ADV_IDLE = 0,
  ADV_NEED_SCAN = 1,
  ADV_STOPPING = 2,
  ADV_SCAN_CFG = 3,
  ADV_DONE = 4
};

static volatile AdvPhase advPhase = ADV_IDLE;
static SemaphoreHandle_t advReady = nullptr;
static uint8_t scanRsp[kBleAdvMax];
static uint8_t scanRspLen = 0;

static void giveAdvReady() {
  if (advReady) {
    xSemaphoreGive(advReady);
  }
}

static void onGap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    if (!param || param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      return;
    }
    if (advPhase == ADV_NEED_SCAN) {
      advPhase = ADV_STOPPING;
      if (!BLEDevice::getAdvertising()->stop()) {
        advPhase = ADV_DONE;
        giveAdvReady();
      }
      return;
    }
    if (advPhase == ADV_DONE) {
      giveAdvReady();
    }
    return;
  }

  if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT && advPhase == ADV_STOPPING) {
    advPhase = ADV_SCAN_CFG;
    BLEAdvertisementData scan;
    scan.addData((char *)scanRsp, scanRspLen);
    if ((int)scan.getPayload().length() != (int)scanRspLen || !BLEDevice::getAdvertising()->setScanResponseData(scan)) {
      advPhase = ADV_DONE;
      BLEDevice::startAdvertising();
    }
    return;
  }

  if (event == ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT && advPhase == ADV_SCAN_CFG) {
    if (!param || param->scan_rsp_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      advPhase = ADV_DONE;
      BLEDevice::startAdvertising();
      return;
    }
    // The library handler already queued the restart.
    advPhase = ADV_DONE;
  }
}

static void logBytes(const char *label, const uint8_t *data, int len) {
  Serial.printf("%s (%d):", label, len);
  for (int i = 0; i < len; i++) {
    Serial.printf(" %02X", data[i]);
  }
  Serial.println();
}

static void configureSecurity() {
  // Bond + LE Secure Connections, no MITM. With no keyboard and no display
  // this is Just Works: the MPC, iOS, and Android can pair without a PIN.
  // Encryption is not required to move MIDI bytes. A host that sends a pair
  // request is bonded; a host that never pairs still connects.
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

static void startAdvert(const BleAdvertPackets &pkts) {
  if (!advReady) {
    advReady = xSemaphoreCreateBinary();
  }
  memcpy(scanRsp, pkts.scan, (size_t)pkts.scanLen);
  scanRspLen = (uint8_t)pkts.scanLen;
  advPhase = ADV_NEED_SCAN;
  xSemaphoreTake(advReady, 0);

  BLEDevice::setCustomGapHandler(onGap);
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->setScanResponse(false);
  advertising->setAdvertisementType(ADV_TYPE_IND);
  advertising->setMinInterval(kAdvIntervalMin);
  advertising->setMaxInterval(kAdvIntervalMax);
  advertising->setAdvertisementChannelMap(ADV_CHNL_ALL);

  BLEAdvertisementData primary;
  primary.addData((char *)pkts.adv, (size_t)pkts.advLen);
  bool payloadOk = (int)primary.getPayload().length() == pkts.advLen;
  bool queued = payloadOk && advertising->setAdvertisementData(primary);
  if (!queued) {
    Serial.println("BLE advert payload rejected; using service UUID only");
    advPhase = ADV_DONE;
    advertising->addServiceUUID(kMidiServiceUuid);
    advertising->setScanResponse(false);
    advertising->start();
    return;
  }
  if (xSemaphoreTake(advReady, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("BLE advert setup timed out");
    advertising->start();
  }
}

BleMidi::BleMidi() {
  started = false;
}

bool BleMidi::Begin(const char *name) {
  if (started) {
    return true;
  }
  BleAdvertPackets pkts;
  buildBleMidiAdvert(name, MOTHDECK_BLE_NAME_DEFAULT, &pkts);
  memset(&running, 0, sizeof(running));
  BLEDevice::init(pkts.gapName);
  BLEDevice::setMTU(185);
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  configureSecurity();
  BLEServer *server = BLEDevice::createServer();
  if (!server) {
    return false;
  }
  server->setCallbacks(new MidiServerCallbacks());
  BLEService *service = server->createService(kMidiServiceUuid);
  if (!service) {
    return false;
  }
  midiChar = service->createCharacteristic(
    kMidiCharUuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  if (!midiChar) {
    return false;
  }
  midiChar->setCallbacks(new MidiCharCallbacks());
  service->start();

  logBytes("BLE adv", pkts.adv, pkts.advLen);
  logBytes("BLE scan", pkts.scan, pkts.scanLen);
  startAdvert(pkts);
  started = true;
  Serial.printf("BLE MIDI advertising as %s (air %s)\n", pkts.gapName, pkts.advName);
  return true;
}

bool BleMidi::Restart(const char *name) {
  advPhase = ADV_IDLE;
  if (started) {
    BLEDevice::deinit(true);
    started = false;
    midiChar = nullptr;
    connected = false;
  }
  return Begin(name);
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
