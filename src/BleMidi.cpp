#include <Arduino.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "BleMidi.h"

static const char *kMidiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *kMidiCharUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
static const int kQueueLen = 16;

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
    BLEDevice::startAdvertising();
  }
};

BleMidi::BleMidi() {
  started = false;
}

bool BleMidi::Begin(const char *name) {
  if (started) {
    return true;
  }
  if (!name) {
    name = "MothSynth";
  }
  memset(&running, 0, sizeof(running));
  BLEDevice::init(name);
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

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kMidiServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  started = true;
  Serial.printf("BLE MIDI advertising as %s\n", name);
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
