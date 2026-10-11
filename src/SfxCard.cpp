#include "SfxCard.h"
#include "DevLog.h"
#include "SdCard.h"
#include "Voice.h"
#include "WavPcm.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// Same order as tools/voices.py SFX_NAMES and the keyboard.
static const char *const kNames[12] = {
    "riser", "downlifter", "zap", "sweep", "impact", "noise",
    "blip", "siren", "reverse", "drop", "bubble", "whoosh",
};

struct SfxSlot {
  int16_t *data;
  int length;
};

static SfxSlot slots[12];

bool sfxCardHit(int note, const int16_t **data, int *length) {
  if (!data || !length || note < 0 || note > 11) {
    return false;
  }
  if (!slots[note].data || slots[note].length < 2) {
    return false;
  }
  *data = slots[note].data;
  *length = slots[note].length;
  return true;
}

static void loadOne(int note) {
  if (note < 0 || note > 11 || slots[note].data) {
    return;
  }
  char path[64];
  snprintf(path, sizeof(path), "/moth/sfx/%s.wav", kNames[note]);
  uint8_t hdr[768];
  int n = 0;
  if (!sdCard.ReadPrefix(path, hdr, (int)sizeof(hdr), &n)) {
    return;
  }
  WavInfo info;
  if (!parseWavPrefix(hdr, n, &info) || info.frames < 2) {
    return;
  }
  if (info.frames > 48000) {
    info.frames = 48000;
  }
  int16_t *block = (int16_t *)deckAlloc((size_t)info.frames * sizeof(int16_t));
  if (!block) {
    DEV_LOGF("SFX: skip %s free=%u\n", kNames[note], (unsigned)deckFreeInternal());
    return;
  }
  int got = 0;
  int rate = 0;
  char err[40];
  if (!sdCard.LoadWavMono(path, block, info.frames, &got, &rate, err, (int)sizeof(err)) || got < 2) {
    deckFree(block);
    return;
  }
  slots[note].data = block;
  slots[note].length = got;
}

void sfxCardScan() {
  if (!sdCard.Ensure()) {
    return;
  }
  sdCard.Mkdir("/moth");
  sdCard.Mkdir("/moth/sfx");
  for (int i = 0; i < 12; i++) {
    if (sfxFlashLength(i) >= 2) {
      continue;
    }
    loadOne(i);
  }
}
