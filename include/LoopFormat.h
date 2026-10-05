#ifndef LoopFormat_h
#define LoopFormat_h
#include <stdint.h>

// Text manifest for /moth/loops/<library>/manifest.txt
// and a pattern file mothdeck-pattern. See docs/FORMATS.md.

static const int kLoopEntryMax = 16;
static const int kPatternStepsMax = 64;

enum LoopEntryKind : uint8_t {
  LOOP_AUDIO = 0,
  LOOP_PATTERN = 1
};

struct LoopEntry {
  LoopEntryKind kind;
  char file[32];
};

struct LoopManifest {
  char name[32];
  int bpm;
  int bars;
  char tags[48];
  int entryCount;
  LoopEntry entries[kLoopEntryMax];
  char error[48];
};

struct PatternFile {
  int bars;
  int stepCount;
  uint8_t steps[kPatternStepsMax];
  char error[48];
};

bool parseLoopManifest(const char *text, int len, LoopManifest *out);
bool parsePatternFile(const char *text, int len, PatternFile *out);

#endif
