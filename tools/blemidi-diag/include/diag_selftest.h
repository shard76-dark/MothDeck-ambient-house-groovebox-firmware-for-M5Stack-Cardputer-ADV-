#ifndef diag_selftest_h
#define diag_selftest_h

#include "MidiProtocol.h"
#include "diag_parse.h"

// True when every event MothDeck's parser keeps is the same message, in the
// same order, as the diagnostic decode. Messages the parser drops (SysEx,
// program change, aftertouch, and controllers it does not use) are skipped.
bool diagMatchesParser(const DiagPacket *pkt, const MidiParseResult *parsed);

// Built-in packets: running status, several messages in one packet, a
// 13-bit timestamp wrap, SysEx split across two packets, and clocks mixed
// into a note. emit receives one JSON object per case, then a summary line.
// Returns the number of failed cases.
int diagSelfTest(void (*emit)(const char *line, void *ctx), void *ctx);

#endif
