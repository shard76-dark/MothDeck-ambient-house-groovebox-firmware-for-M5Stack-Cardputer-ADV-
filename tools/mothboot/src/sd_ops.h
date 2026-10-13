#ifndef sd_ops_h
#define sd_ops_h

#include <stdint.h>
#include <stdbool.h>

typedef void (*SdProgressFn)(int percent, const char *phase, uint64_t bytes, uint64_t reported);

bool sdCheckFull(SdProgressFn progress, uint64_t *verifiedOut, uint64_t *reportedOut, uint32_t *bpsOut);
bool sdFormatCard(uint64_t requestedBytes, SdProgressFn progress);
bool sdMountFiles();
void sdUnmountFiles();
uint64_t sdVolumeBytes();
uint64_t sdFreeBytes();

#endif
