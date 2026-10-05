#ifndef KitFormat_h
#define KitFormat_h
#include <stdint.h>

// Text manifest for /moth/drums/<folder>/manifest.txt
// Twelve pad= lines, in keyboard order. See docs/FORMATS.md.

static const int kKitPads = 12;

struct KitManifest {
  char name[32];
  int padCount;
  char pads[kKitPads][32];
  char error[48];
};

bool parseKitManifest(const char *text, int len, KitManifest *out);

#endif
