#ifndef FatPlan_h
#define FatPlan_h

#include <stdint.h>
#include <stdbool.h>

// FAT32 geometry for a card whose usable bytes were measured.
// The partition ends at or before verifiedBytes - marginBytes.
// 16 MiB is the margin MothBoot uses when the caller passes 0.

static const uint64_t kFatMarginBytes = 16ull * 1024ull * 1024ull;
static const uint32_t kFatPartStart = 2048;

struct FatPlan {
  uint32_t partStart;
  uint32_t partSectors;
  uint32_t reserved;
  uint32_t sectorsPerCluster;
  uint32_t sectorsPerFat;
  uint32_t fats;
  uint32_t rootCluster;
  uint32_t clusters;
  uint32_t dataStart;
  uint64_t volumeBytes;
  bool ok;
};

// requestedBytes 0 selects verified - margin. A larger request is clamped
// to that ceiling. ok is false when the card has not been checked, or when
// the clamped size cannot hold a FAT32 volume.
bool fatPlanFor(uint64_t verifiedBytes, uint64_t marginBytes, uint64_t requestedBytes, FatPlan *out);

void fat32BootSector(const FatPlan *plan, uint8_t sector[512]);
void fat32FsInfo(uint8_t sector[512]);
void fat32FatHead(uint8_t sector[512]);

#endif
