#include "FatPlan.h"
#include "ImageTag.h"
#include "MothCrc.h"
#include "SdPattern.h"

#include <vector>

#include <stdio.h>
#include <string.h>

static int gFailed = 0;

static void expect(bool ok, const char *name) {
  if (!ok) {
    fprintf(stderr, "fail %s\n", name);
    gFailed++;
  }
}

int main() {
  const uint8_t text[] = "123456789";
  uint32_t crc = mothCrc32Update(0, text, sizeof(text) - 1);
  expect(crc == 0xCBF43926u, "crc32");
  uint32_t split = mothCrc32Update(0, text, 4);
  split = mothCrc32Update(split, text + 4, 5);
  expect(split == crc, "crc32 split");

  uint8_t sector[512];
  uint8_t other[512];
  sdPatternFill(sector, 100, 7);
  expect(sdPatternMatch(sector, 100, 7), "pattern match");
  sdPatternFill(other, 101, 7);
  expect(!sdPatternMatch(other, 100, 7), "pattern wrap");

  SdProbe probes[3] = {
    {0, true, true},
    {512, true, true},
    {1024, false, true},
  };
  expect(sdVerifiedFromProbes(probes, 3) == 1024, "probe limit");
  probes[1].sentinel = false;
  expect(sdVerifiedFromProbes(probes, 3) == 512, "sentinel stop");

  FatPlan plan;
  uint64_t verified = 1024ull * 1024ull * 1024ull;
  expect(fatPlanFor(verified, kFatMarginBytes, 0, &plan), "fat plan");
  expect(plan.ok && plan.volumeBytes <= verified - kFatMarginBytes, "fat ceiling");
  expect(plan.clusters >= 65525u, "fat32 clusters");
  expect((plan.partStart + plan.partSectors) * 512ull <= plan.volumeBytes, "partition end");
  expect(fatPlanFor(verified, kFatMarginBytes, verified, &plan), "oversize request clamps");
  expect(plan.volumeBytes <= verified - kFatMarginBytes, "clamp");
  expect(!fatPlanFor(0, kFatMarginBytes, 0, &plan), "unchecked card");

  uint8_t boot[512];
  fatPlanFor(verified, kFatMarginBytes, 0, &plan);
  fat32BootSector(&plan, boot);
  expect(boot[510] == 0x55 && boot[511] == 0xAA, "boot signature");
  expect(boot[13] == plan.sectorsPerCluster, "cluster size");

  std::vector<uint8_t> deck(300000, 0xFF);
  const char deckTag[] = "MOTHIMG:1.2.11";
  memcpy(deck.data() + 292788, deckTag, sizeof(deckTag));
  char found[24];
  expect(mothImageVersionScan(deck.data(), deck.size(), found, sizeof(found)), "slot past 256KB");
  expect(strcmp(found, "1.2.11") == 0, "slot B version");

  std::vector<uint8_t> diag(90000, 0x00);
  const char diagTag[] = "MOTHIMG:0.1.4";
  memcpy(diag.data() + 81412, diagTag, sizeof(diagTag));
  expect(mothImageVersionScan(diag.data(), diag.size(), found, sizeof(found)), "slot inside 256KB");
  expect(strcmp(found, "0.1.4") == 0, "slot A version");

  std::vector<uint8_t> edge(700, 0xFF);
  memcpy(edge.data() + 500, deckTag, sizeof(deckTag));
  expect(mothImageVersionScan(edge.data(), edge.size(), found, sizeof(found)), "tag across a window");
  expect(strcmp(found, "1.2.11") == 0, "split window version");

  uint8_t bare[32];
  memset(bare, 0, sizeof(bare));
  memcpy(bare, "MOTHIMG:", 8);
  memcpy(bare + 16, diagTag, sizeof(diagTag));
  expect(mothImageVersion(bare, sizeof(bare), found, sizeof(found)), "skip empty tag");
  expect(strcmp(found, "0.1.4") == 0, "later tag wins");

  if (gFailed) {
    fprintf(stderr, "%d failed\n", gFailed);
    return 1;
  }
  printf("mothboot host ok\n");
  return 0;
}
