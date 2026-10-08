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
static void startAdvert(const BleAdvertPackets &pkts) {
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->setMinInterval(kAdvIntervalMin);
  advertising->setMaxInterval(kAdvIntervalMax);

  BLEAdvertisementData primary;
  if (!payloadFits(primary, pkts.adv, pkts.advLen)) {
    // The length check failed before any custom payload was committed, so
    // the library builder can still run. Scan response is off, which makes
    // that builder shorten the name into the primary packet instead of
    // dropping it.
    Serial.println("BLE advert payload truncated; shortening the name");
    advertising->addServiceUUID(kMidiServiceUuid);
    advertising->setScanResponse(false);
    if (!advertising->start()) {
      Serial.println("BLE advertising failed to start");
    }
    return;
  }
  if (!advertising->setAdvertisementData(primary)) {
    Serial.println("BLE advert config rejected");
  }

  BLEAdvertisementData scan;
  if (!payloadFits(scan, pkts.scan, pkts.scanLen) || !advertising->setScanResponseData(scan)) {
    Serial.println("BLE scan response rejected");
  }
  advertising->setScanResponse(true);
  if (!advertising->start()) {
    Serial.println("BLE advertising failed to start");
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
  BLEDevice::setMTU(256);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
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
