#ifndef MidiProtocol_h
#define MidiProtocol_h
#include <stdint.h>
#include <stddef.h>

// BLE MIDI (Apple / MMA spec) frames every packet with a 13-bit timestamp:
//   byte 0 header  = 1xxxxxxx  timestamp high (top 6 bits)
// then one or more messages, each preceded by its own timestamp-low byte
// (bit 7 set). One packet often holds many notes and several 0xF8 clocks.
// Running status stays inside the packet and is cancelled at the end.
// System real-time may sit between messages or between the data bytes of
// a message. SysEx may continue in the next packet.
// Note, controller MSB (CC 0-31), controller LSB (CC 32-63), pitch bend,
// clock, and transport are delivered. Pitch bend is an LSB byte then an
// MSB byte on controller number kMidiPitchCc. Note On velocity 0 is Note Off.

enum MidiMsgType : uint8_t {
  MIDI_MSG_NONE = 0,
  MIDI_MSG_NOTE_ON = 1,
  MIDI_MSG_NOTE_OFF = 2,
  MIDI_MSG_MSB = 3,
  MIDI_MSG_LSB = 4,
  MIDI_MSG_CLOCK = 5,
  MIDI_MSG_START = 6,
  MIDI_MSG_CONTINUE = 7,
  MIDI_MSG_STOP = 8,
  MIDI_MSG_SONG_POS = 9
};

static const uint8_t kMidiPitchCc = 128;
// One notification can carry a chord plus a run of clocks. The parser
// keeps scanning after this fills so a long packet cannot desync the next.
static const int kMidiMaxEvents = 48;

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
  int overflow;
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
  // A SysEx that did not end in this packet continues in the next one.
  uint8_t sysex;
};

BleMidiTimestamp bleMidiTimestamp(uint16_t millis13);

int buildBleMidiPacket(uint8_t *buf, int bufLen, uint16_t millis13, MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value);

int parseBleMidiPacket(const uint8_t *data, int len, MidiRunningState *state, MidiParseResult *out);

#endif
