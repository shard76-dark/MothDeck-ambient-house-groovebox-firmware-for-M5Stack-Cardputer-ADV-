#ifndef SdPattern_h
#define SdPattern_h

#include <stdint.h>
#include <stdbool.h>

// One 512-byte sector whose bytes are a function of the sector number.
// A wrapped read of a different sector does not match.

static const int kSdSector = 512;

void sdPatternFill(uint8_t *dst, uint32_t sector, uint32_t salt);
bool sdPatternMatch(const uint8_t *src, uint32_t sector, uint32_t salt);

struct SdProbe {
  uint64_t offset;
  bool match;
  bool sentinel;
};

// Bytes covered by the leading run of probes that matched and left the
// sentinel intact. The first failed probe ends the run. offset is the
// start of that sector; a match covers offset + 512.
uint64_t sdVerifiedFromProbes(const SdProbe *probes, int count);

#endif
