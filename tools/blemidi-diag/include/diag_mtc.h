#ifndef diag_mtc_h
#define diag_mtc_h

#include <stdint.h>
#include <stdbool.h>

// MIDI Time Code. Quarter frames are F1 0nnndddd. A full frame is the
// universal SysEx F0 7F <id> 01 01 hr mn sc fr F7, without the F0/F7.

struct DiagMtcAsm {
  uint8_t nib[8];
  uint8_t got;
  uint64_t lastQfUs;
  uint64_t lastFullUs;
  uint32_t qfWindow;
  uint64_t gapSum;
  uint32_t gapN;
  uint32_t gapMax;
  uint32_t fullWindow;
  int hours;
  int minutes;
  int seconds;
  int frames;
  int rateCode;
  bool hasPos;
};

struct DiagMtcEvent {
  bool quarter;
  bool complete;
  bool full;
  int piece;
  int nibble;
  bool hasGap;
  uint32_t gapUs;
  int hours;
  int minutes;
  int seconds;
  int frames;
  int rateCode;
  bool hasPos;
};

void diagMtcReset(DiagMtcAsm *asmState);
bool diagMtcQuarter(DiagMtcAsm *asmState, uint8_t byte, uint64_t us, DiagMtcEvent *out);
bool diagMtcFull(const uint8_t *payload, int len, uint64_t us, DiagMtcAsm *asmState, DiagMtcEvent *out);
const char *diagMtcRateName(int rateCode);

#endif
