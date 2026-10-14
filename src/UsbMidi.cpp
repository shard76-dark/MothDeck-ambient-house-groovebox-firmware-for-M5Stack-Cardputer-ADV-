#include "UsbMidi.h"
#include "BleMidi.h"

#if defined(ARDUINO)
#include <Arduino.h>
#if !ARDUINO_USB_MODE
#include "USB.h"
#include "USBMIDI.h"

// Constructed before app_main starts TinyUSB, so the MIDI interface is in
// the descriptor next to the CDC serial port.
static USBMIDI usbPort("Mothdeck");

struct UsbMidiIdentity {
  UsbMidiIdentity() {
    USB.manufacturerName("Mothdeck");
    USB.productName("Mothdeck");
    USB.firmwareVersion(0x012B);
  }
};

static UsbMidiIdentity usbMidiIdentity;

static volatile int usbEdge = 0;
static int usbWas = -1;

static bool hostMounted() {
  return (bool)USB;
}

void usbMidiPoll(BleMidi &ble) {
  bool mounted = hostMounted();
  if (usbWas < 0) {
    usbWas = mounted ? 1 : 0;
  } else if ((mounted ? 1 : 0) != usbWas) {
    usbWas = mounted ? 1 : 0;
    __atomic_store_n(&usbEdge, mounted ? 1 : -1, __ATOMIC_RELEASE);
  }
  if (!mounted) {
    return;
  }
  // One read shares one arrival time. The next read's time is the span
  // those clocks occupied, which is how a BLE packet of several 0xF8s is
  // measured too.
  uint16_t now = (uint16_t)(millis() & 0x1FFF);
  uint8_t raw[64];
  int n = 0;
  midiEventPacket_t packet;
  while (n + 4 <= (int)sizeof(raw) && usbPort.readPacket(&packet)) {
    raw[n++] = packet.header;
    raw[n++] = packet.byte1;
    raw[n++] = packet.byte2;
    raw[n++] = packet.byte3;
  }
  if (n < 4) {
    return;
  }
  MidiParseResult parsed;
  parseUsbMidiStream(raw, n, now, &parsed);
  ble.Ingest(parsed.events, parsed.count);
}

void usbMidiSend(const MidiEvent &event) {
  if (!hostMounted()) {
    return;
  }
  uint8_t channel = (uint8_t)((event.channel & 0x0F) + 1);
  if (event.type == MIDI_MSG_NOTE_ON) {
    usbPort.noteOn(event.number, event.value, channel);
  } else if (event.type == MIDI_MSG_NOTE_OFF) {
    usbPort.noteOff(event.number, event.value, channel);
  } else if (event.type == MIDI_MSG_MSB && event.number < 128) {
    if (event.number == kMidiPitchCc) {
      usbPort.pitchBend(event.value14, channel);
    } else {
      usbPort.controlChange(event.number, event.value, channel);
    }
  } else if (event.type == MIDI_MSG_LSB && event.number < 32) {
    usbPort.controlChange((uint8_t)(event.number + 32), event.value, channel);
  } else if (event.type == MIDI_MSG_CLOCK) {
    int ticks = event.value == 0 ? 1 : (int)event.value;
    if (ticks > 8) {
      ticks = 8;
    }
    for (int i = 0; i < ticks; i++) {
      usbPort.write(0xF8);
    }
  } else if (event.type == MIDI_MSG_START) {
    usbPort.write(0xFA);
  } else if (event.type == MIDI_MSG_CONTINUE) {
    usbPort.write(0xFB);
  } else if (event.type == MIDI_MSG_STOP) {
    usbPort.write(0xFC);
  }
}

bool usbMidiConnected() {
  // The audio task reads this. The USB stack is only touched from loop.
  return usbWas == 1;
}

int usbMidiConsumeEdge() {
  return __atomic_exchange_n(&usbEdge, 0, __ATOMIC_ACQ_REL);
}

const char *usbMidiLine() {
  if (usbMidiConnected()) {
    return "USB MIDI";
  }
  return "USB off";
}

#else
void usbMidiPoll(BleMidi &ble) { (void)ble; }
void usbMidiSend(const MidiEvent &event) { (void)event; }
bool usbMidiConnected() { return false; }
int usbMidiConsumeEdge() { return 0; }
const char *usbMidiLine() { return "USB off"; }
#endif
#else
void usbMidiPoll(BleMidi &ble) { (void)ble; }
void usbMidiSend(const MidiEvent &event) { (void)event; }
bool usbMidiConnected() { return false; }
int usbMidiConsumeEdge() { return 0; }
const char *usbMidiLine() { return "USB off"; }
#endif
