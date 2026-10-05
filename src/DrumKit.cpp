#include "DrumKit.h"
#include "KitFormat.h"
#include "SdCard.h"
#include "WavPcm.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

DrumKit drumKit;

static const int kKitFrames = 18000;
static const int kPadFrames = 6000;

static int16_t *kitBlock = nullptr;
static int kitOffset[kKitPads];
static int kitLength[kKitPads];
static int kitRate[kKitPads];
static volatile uint8_t kitReady = 0;

static void setErr(char *err, int errLen, const char *msg) {
  if (err && errLen > 0) {
    snprintf(err, errLen, "%s", msg ? msg : "");
  }
}

bool drumKitHit(int note, DrumHitView *out) {
  if (!out || !kitReady || note < 0 || note >= kKitPads) {
    return false;
  }
  if (!kitBlock || kitLength[note] < 2) {
    return false;
  }
  out->data = kitBlock + kitOffset[note];
  out->length = kitLength[note];
  out->rate = kitRate[note] > 0 ? kitRate[note] : 22050;
  return true;
}

static void unloadKit() {
  kitReady = 0;
  delay(30);
  deckFree(kitBlock);
  kitBlock = nullptr;
  memset(kitOffset, 0, sizeof(kitOffset));
  memset(kitLength, 0, sizeof(kitLength));
  memset(kitRate, 0, sizeof(kitRate));
}

const char *DrumKit::Name(int index) const {
  if (index <= 0 || index > sdCount) {
    return "Ambient House";
  }
  if (names[index - 1][0]) {
    return names[index - 1];
  }
  return folders[index - 1];
}

void DrumKit::Scan() {
  sdCount = 0;
  count = 1;
  memset(folders, 0, sizeof(folders));
  memset(names, 0, sizeof(names));
  if (!sdCard.Ensure()) {
    if (selected != 0) {
      unloadKit();
      selected = 0;
    }
    return;
  }
  sdCard.Mkdir("/moth");
  sdCard.Mkdir("/moth/drums");
  char found[kMaxKits][24];
  int n = sdCard.List("/moth/drums", found, kMaxKits, true);
  for (int i = 0; i < n && sdCount < kMaxKits; i++) {
    strncpy(folders[sdCount], found[i], 23);
    char path[96];
    snprintf(path, sizeof(path), "/moth/drums/%s/manifest.txt", folders[sdCount]);
    char text[1024];
    if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
      strncpy(names[sdCount], folders[sdCount], 31);
      sdCount++;
      continue;
    }
    KitManifest man;
    if (!parseKitManifest(text, (int)strlen(text), &man)) {
      strncpy(names[sdCount], folders[sdCount], 31);
      sdCount++;
      continue;
    }
    strncpy(names[sdCount], man.name, 31);
    sdCount++;
  }
  count = 1 + sdCount;
  if (selected < 0 || selected >= count) {
    unloadKit();
    selected = 0;
  }
}

static bool decodePad(const char *path, int16_t *dst, int dstFrames, int *got, int *rate, char *err, int errLen) {
  int maxBytes = kPadFrames * 4 + 256;
  uint8_t *raw = (uint8_t *)deckAlloc((size_t)maxBytes);
  if (!raw) {
    setErr(err, errLen, "Out of memory");
    return false;
  }
  int n = 0;
  if (!sdCard.ReadAll(path, raw, maxBytes, &n)) {
    deckFree(raw);
    setErr(err, errLen, "Pad missing or too big");
    return false;
  }
  if (n < 12 || memcmp(raw, "RIFF", 4) != 0) {
    deckFree(raw);
    setErr(err, errLen, "Pad is not WAV");
    return false;
  }
  WavInfo info;
  if (!parseWavHeader(raw, n, &info)) {
    setErr(err, errLen, info.error[0] ? info.error : "Bad WAV");
    deckFree(raw);
    return false;
  }
  int frames = info.frames;
  if (frames > dstFrames) {
    frames = dstFrames;
  }
  if (frames < 2) {
    deckFree(raw);
    setErr(err, errLen, "Pad too short");
    return false;
  }
  int written = decodeWavMono(raw, n, dst, frames, &info);
  deckFree(raw);
  if (written < 2) {
    setErr(err, errLen, "Pad decode failed");
    return false;
  }
  *got = written;
  *rate = info.rate > 0 ? info.rate : 22050;
  return true;
}

bool DrumKit::Select(int index, char *err, int errLen) {
  if (index < 0 || index >= count) {
    setErr(err, errLen, "No such kit");
    return false;
  }
  if (index == 0) {
    unloadKit();
    selected = 0;
    setErr(err, errLen, "");
    return true;
  }
  int sd = index - 1;
  char path[96];
  snprintf(path, sizeof(path), "/moth/drums/%s/manifest.txt", folders[sd]);
  char text[1024];
  if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
    setErr(err, errLen, "Kit manifest missing");
    return false;
  }
  KitManifest man;
  if (!parseKitManifest(text, (int)strlen(text), &man)) {
    setErr(err, errLen, man.error[0] ? man.error : "Bad kit");
    return false;
  }
  int16_t *block = (int16_t *)deckAlloc((size_t)kKitFrames * sizeof(int16_t));
  if (!block) {
    setErr(err, errLen, "Out of memory");
    return false;
  }
  int used = 0;
  int off[kKitPads];
  int len[kKitPads];
  int rate[kKitPads];
  memset(off, 0, sizeof(off));
  memset(len, 0, sizeof(len));
  memset(rate, 0, sizeof(rate));
  for (int p = 0; p < kKitPads; p++) {
    int room = kKitFrames - used;
    if (room > kPadFrames) {
      room = kPadFrames;
    }
    if (room < 2) {
      deckFree(block);
      setErr(err, errLen, "Kit too large");
      return false;
    }
    char file[128];
    snprintf(file, sizeof(file), "/moth/drums/%s/%s", folders[sd], man.pads[p]);
    int got = 0;
    int padRate = 22050;
    if (!decodePad(file, block + used, room, &got, &padRate, err, errLen)) {
      deckFree(block);
      return false;
    }
    off[p] = used;
    len[p] = got;
    rate[p] = padRate;
    used += got;
  }
  kitReady = 0;
  delay(30);
  deckFree(kitBlock);
  kitBlock = block;
  memcpy(kitOffset, off, sizeof(kitOffset));
  memcpy(kitLength, len, sizeof(kitLength));
  memcpy(kitRate, rate, sizeof(kitRate));
  selected = index;
  kitReady = 1;
  setErr(err, errLen, "");
  return true;
}
