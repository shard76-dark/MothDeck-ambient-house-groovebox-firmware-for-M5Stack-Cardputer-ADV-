#ifndef diag_parse_h
#define diag_parse_h

#include <stdint.h>
#include <stdbool.h>

// One BLE MIDI notification, decoded the way the BLE MIDI spec frames it:
// a 13-bit millisecond timestamp, running status inside the packet only,
// system real-time allowed between data bytes, and SysEx allowed to continue
// into the next packet. Message types MothDeck's parser keeps use the same
// classification (Note On velocity 0 is Note Off). Messages it ignores are
// still listed, so a capture shows what the sender actually wrote.

// Same ceiling as MothDeck's parser (kMidiMaxEvents), so a long packet is
// not reported as a diagnostic overflow while the firmware still accepts it.
static const int kDiagMaxMsg = 160;
static const int kDiagSysexCap = 240;

struct DiagState {
  uint8_t sysex;
};

struct DiagMsg {
  char type[16];
  // 0 for system messages. Channel voice uses 1..16.
  int channel;
  uint8_t data[2];
  int dataLen;
  int sysexOff;
  int sysexLen;
  bool sysexEnded;
  bool running;
  bool vel0;
  uint16_t bleTs;
};

static const int kDiagTsLoMax = 24;

struct DiagPacket {
  // Timestamp after the header and the first timestamp byte.
  uint16_t bleTs;
  uint8_t hdr;
  uint8_t tsHi;
  uint8_t tsLo[kDiagTsLoMax];
  int tsLoN;
  int errors;
  bool rejected;
  int count;
  DiagMsg msg[kDiagMaxMsg];
  uint8_t sysex[kDiagSysexCap];
  int sysexLen;
};

void diagStateClear(DiagState *state);
void diagParse(const uint8_t *data, int len, DiagState *state, DiagPacket *out);

#endif
