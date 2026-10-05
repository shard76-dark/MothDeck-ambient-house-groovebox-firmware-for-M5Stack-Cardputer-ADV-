#ifndef MidiProtocol_h
#define MidiProtocol_h
#include <stdint.h>
#include <stddef.h>

// BLE MIDI (Apple spec) frames every packet with a 13-bit timestamp:
//   byte 0 header  = 1xxxxxxx  timestamp MSB (top 6 bits)
//   byte 1         = 1yyyyyyy  timestamp LSB (low 7 bits)
// followed by ordinary MIDI bytes. This module enables Note, controller
// MSB (CC 0-31) and controller LSB (CC 32-63). Pitch bend is delivered
// as an LSB byte then an MSB byte on controller number kMidiPitchCc.

enum MidiMsgType : uint8_t {
  MIDI_MSG_NONE = 0,
  MIDI_MSG_NOTE_ON = 1,
  MIDI_MSG_NOTE_OFF = 2,
  MIDI_MSG_MSB = 3,
  MIDI_MSG_LSB = 4
};

static const uint8_t kMidiPitchCc = 128;
static const int kMidiMaxEvents = 8;

struct MidiEvent {
  MidiMsgType type;
  uint8_t channel;
  uint8_t number;
  uint8_t value;
  uint16_t value14;
};

struct MidiParseResult {
  MidiEvent events[kMidiMaxEvents];
  int count;
};

struct BleMidiTimestamp {
  uint8_t headerMsb;
  uint8_t timestampLsb;
};

struct MidiRunningState {
  uint8_t msb[32];
  uint8_t lsb[32];
  uint8_t msbKnown[32];
  uint8_t lsbKnown[32];
};

BleMidiTimestamp bleMidiTimestamp(uint16_t millis13);

int buildBleMidiPacket(uint8_t *buf, int bufLen, uint16_t millis13, MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value);

int parseBleMidiPacket(const uint8_t *data, int len, MidiRunningState *state, MidiParseResult *out);

#endif
