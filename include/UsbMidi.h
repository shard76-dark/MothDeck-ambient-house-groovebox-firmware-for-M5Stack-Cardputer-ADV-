#ifndef UsbMidi_h
#define UsbMidi_h
#include "MidiProtocol.h"

class BleMidi;

// USB MIDI device on the Cardputer's USB port, next to the serial console.
// The port enumerates as a composite CDC + MIDI device. BLE keeps its own
// radio; both transports land in the same input queue.
//
// These functions are safe to call from the loop task only. The audio task
// reads the shared queue and does not touch the USB stack.

void usbMidiPoll(BleMidi &ble);
void usbMidiSend(const MidiEvent &event);
bool usbMidiConnected();
// 1 when a host enumerates the port, -1 when it leaves, 0 otherwise.
int usbMidiConsumeEdge();
// "USB MIDI" while a host has enumerated the port, otherwise "USB off".
const char *usbMidiLine();

#endif
