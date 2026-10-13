#ifndef SdLimit_h
#define SdLimit_h

#include <stdint.h>
#include <stdbool.h>

// Bytes MothBoot stored after a full card check. 0 means not checked yet.
uint64_t mothSdVerifiedBytes();
void mothSdStoreVerified(uint64_t bytes);

// True when no check is stored, or the mounted volume is inside that size.
bool mothSdVolumeOk(uint64_t volumeBytes);

#endif
