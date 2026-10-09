#include "LoopLibrary.h"
#include "SdCard.h"
#include "PcmHold.h"
#include "PluginFormat.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

static const int kInstLoopMax = 8;
static LoopHit instLoops[kInstLoopMax];
static volatile int instLoopCount = 0;

bool loopInstrumentHit(int note, LoopHit *out) {
  int n = instLoopCount;
  if (!out || n < 1 || n > kInstLoopMax) {
    return false;
  }
  if (note < 0) {
    note = 0;
  }
  const LoopHit &hit = instLoops[note % n];
  if (hit.frames < 2 || (hit.hold <= 0 && !hit.pcm)) {
    return false;
  }
  *out = hit;
  return true;
}

LoopLibrary loopLibrary;

static const int kLoopCache = 8;

struct LoopCache {
  char key[56];
  int hold;
  int frames;
  int rate;
  int bpm;
  uint8_t steps[kPatternStepsMax];
  int stepCount;
  uint8_t isPattern;
};

static LoopCache cache[kLoopCache];

static void setErr(char *err, int errLen, const char *msg) {
  if (err && errLen > 0) {
    snprintf(err, errLen, "%s", msg ? msg : "");
  }
}

static int cacheFind(const char *key) {
  for (int i = 0; i < kLoopCache; i++) {
    if (cache[i].key[0] && strcmp(cache[i].key, key) == 0) {
      return i;
    }
  }
  return -1;
}

static int cacheSlot(const char *key) {
  int found = cacheFind(key);
  if (found >= 0) {
    return found;
  }
  for (int i = 0; i < kLoopCache; i++) {
    if (!cache[i].key[0]) {
      return i;
    }
  }
  return -1;
}

void LoopLibrary::Scan() {
  count = 0;
  memset(libs, 0, sizeof(libs));
  if (!sdCard.Ensure()) {
    return;
  }
  sdCard.Mkdir("/moth");
  sdCard.Mkdir("/moth/loops");
  char names[kMaxLibs][24];
  int n = sdCard.List("/moth/loops", names, kMaxLibs, true);
  for (int i = 0; i < n && count < kMaxLibs; i++) {
    LoopLibInfo &lib = libs[count];
    strncpy(lib.folder, names[i], sizeof(lib.folder) - 1);
    char path[96];
    snprintf(path, sizeof(path), "/moth/loops/%s/manifest.txt", lib.folder);
    char text[1536];
    if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
      strncpy(lib.error, "No manifest", sizeof(lib.error) - 1);
      strncpy(lib.name, lib.folder, sizeof(lib.name) - 1);
      count++;
      continue;
    }
    LoopManifest man;
    if (!parseLoopManifest(text, (int)strlen(text), &man)) {
      strncpy(lib.error, man.error, sizeof(lib.error) - 1);
      strncpy(lib.name, lib.folder, sizeof(lib.name) - 1);
      count++;
      continue;
    }
    strncpy(lib.name, man.name, sizeof(lib.name) - 1);
    lib.bpm = man.bpm;
    lib.bars = man.bars;
    strncpy(lib.tags, man.tags, sizeof(lib.tags) - 1);
    lib.entryCount = man.entryCount;
    if (lib.entryCount > kLoopEntryMax) {
      lib.entryCount = kLoopEntryMax;
    }
    for (int e = 0; e < lib.entryCount; e++) {
      lib.entries[e] = man.entries[e];
    }
    count++;
  }
}

bool LoopLibrary::LoadEntry(int libIndex, int entryIndex, LoopArm *arm, uint8_t *steps, int *stepCount, bool *pattern, char *err, int errLen) {
  if (pattern) {
    *pattern = false;
  }
  if (stepCount) {
    *stepCount = 0;
  }
  if (libIndex < 0 || libIndex >= count || !arm) {
    setErr(err, errLen, "No library");
    return false;
  }
  const LoopLibInfo &lib = libs[libIndex];
  if (entryIndex < 0 || entryIndex >= lib.entryCount) {
    setErr(err, errLen, "No loop");
    return false;
  }
  const LoopEntry &entry = lib.entries[entryIndex];
  char key[56];
  snprintf(key, sizeof(key), "%s/%s", lib.folder, entry.file);
  int slot = cacheFind(key);
  if (slot < 0) {
    char path[96];
    snprintf(path, sizeof(path), "/moth/loops/%s/%s", lib.folder, entry.file);
    slot = cacheSlot(key);
    if (slot < 0) {
      setErr(err, errLen, "Loops: not ready");
      return false;
    }
    if (cache[slot].hold > 0) {
      pcmHoldClose(cache[slot].hold);
    }
    memset(&cache[slot], 0, sizeof(cache[slot]));
    strncpy(cache[slot].key, key, sizeof(cache[slot].key) - 1);
    cache[slot].bpm = lib.bpm;
    if (entry.kind == LOOP_PATTERN) {
      char text[1024];
      if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
        cache[slot].key[0] = 0;
        setErr(err, errLen, "Pattern missing");
        return false;
      }
      PatternFile pf;
      if (!parsePatternFile(text, (int)strlen(text), &pf)) {
        cache[slot].key[0] = 0;
        setErr(err, errLen, pf.error[0] ? pf.error : "Bad pattern");
        return false;
      }
      cache[slot].isPattern = 1;
      cache[slot].stepCount = pf.stepCount;
      memcpy(cache[slot].steps, pf.steps, pf.stepCount);
    } else {
      int frames = 0;
      int rate = 0;
      int hold = pcmHoldOpen(path, &frames, &rate, err, errLen);
      if (hold <= 0) {
        cache[slot].key[0] = 0;
        return false;
      }
      cache[slot].hold = hold;
      cache[slot].frames = frames;
      cache[slot].rate = rate;
    }
  }
  memset(arm, 0, sizeof(*arm));
  strncpy(arm->library, lib.folder, sizeof(arm->library) - 1);
  strncpy(arm->name, entry.file, sizeof(arm->name) - 1);
  arm->bpm = cache[slot].bpm > 0 ? cache[slot].bpm : lib.bpm;
  arm->rate = cache[slot].rate;
  arm->pcm = nullptr;
  arm->hold = cache[slot].hold;
  arm->frames = cache[slot].frames;
  if (cache[slot].isPattern) {
    if (pattern) {
      *pattern = true;
    }
    if (steps && stepCount) {
      int n = cache[slot].stepCount;
      if (n > kPatternStepsMax) {
        n = kPatternStepsMax;
      }
      memcpy(steps, cache[slot].steps, n);
      *stepCount = n;
    }
    return true;
  }
  if (arm->frames < 2 || (arm->hold <= 0 && !arm->pcm)) {
    setErr(err, errLen, "Empty loop");
    return false;
  }
  return true;
}

bool LoopLibrary::PrepareSong(SongData *song, char *err, int errLen) {
  memset(loaded, 0, sizeof(loaded));
  memset(loadedOk, 0, sizeof(loadedOk));
  if (!song) {
    return false;
  }
  setErr(err, errLen, "");
  bool ok = true;
  if (!sdCard.Ensure()) {
    bool any = false;
    for (int t = 0; t < kSongTracks; t++) {
      if (song->loops[t].enabled) {
        any = true;
        song->loops[t].enabled = 0;
      }
    }
    if (any) {
      setErr(err, errLen, "Loops need the SD card");
      return false;
    }
    return true;
  }
  if (count == 0) {
    Scan();
  }
  for (int t = 0; t < kSongTracks; t++) {
    if (!song->loops[t].enabled) {
      continue;
    }
    int libIndex = -1;
    int entryIndex = -1;
    for (int i = 0; i < count; i++) {
      if (strcmp(libs[i].folder, song->loops[t].library) == 0) {
        libIndex = i;
        for (int e = 0; e < libs[i].entryCount; e++) {
          if (strcmp(libs[i].entries[e].file, song->loops[t].name) == 0) {
            entryIndex = e;
            break;
          }
        }
        break;
      }
    }
    if (libIndex < 0 || entryIndex < 0) {
      song->loops[t].enabled = 0;
      setErr(err, errLen, "Loop missing");
      ok = false;
      continue;
    }
    uint8_t steps[kPatternStepsMax];
    int stepCount = 0;
    bool pattern = false;
    char local[48];
    if (!LoadEntry(libIndex, entryIndex, &loaded[t], steps, &stepCount, &pattern, local, (int)sizeof(local))) {
      song->loops[t].enabled = 0;
      setErr(err, errLen, local[0] ? local : "Loop failed");
      ok = false;
      continue;
    }
    if (pattern) {
      song->loops[t].enabled = 0;
      setErr(err, errLen, "Pattern loop is not audio");
      ok = false;
      continue;
    }
    loaded[t].quantize = song->loops[t].quantize;
    loadedOk[t] = 1;
  }
  return ok;
}

static bool memoryLimit(const char *msg) {
  return msg && strstr(msg, "Loops: not ready") != nullptr;
}

void LoopLibrary::DropAudio() {
  for (int i = 0; i < kLoopCache; i++) {
    if (cache[i].hold > 0) {
      pcmHoldClose(cache[i].hold);
    }
    memset(&cache[i], 0, sizeof(cache[i]));
  }
  memset(instLoops, 0, sizeof(instLoops));
  instLoopCount = 0;
  ready = 0;
  memset(loaded, 0, sizeof(loaded));
  memset(loadedOk, 0, sizeof(loadedOk));
  limitMsg[0] = 0;
}

int LoopLibrary::PreloadInstrument(char *err, int errLen) {
  LoopHit next[kInstLoopMax];
  memset(next, 0, sizeof(next));
  limitMsg[0] = 0;
  ready = 0;
  audio = 0;
  int n = 0;
  setErr(err, errLen, "");
  if (!sdCard.Ensure()) {
    instLoopCount = 0;
    setErr(err, errLen, "Loops need the SD card");
    snprintf(limitMsg, sizeof(limitMsg), "%s", "Loops need the SD card");
    return 0;
  }
  if (count == 0) {
    Scan();
  }
  for (int i = 0; i < count; i++) {
    for (int e = 0; e < libs[i].entryCount; e++) {
      if (libs[i].entries[e].kind == LOOP_AUDIO) {
        audio++;
      }
    }
  }
  bool stopped = false;
  for (int i = 0; i < count && n < kInstLoopMax && !stopped; i++) {
    for (int e = 0; e < libs[i].entryCount && n < kInstLoopMax && !stopped; e++) {
      if (libs[i].entries[e].kind != LOOP_AUDIO) {
        continue;
      }
      uint8_t steps[kPatternStepsMax];
      int stepCount = 0;
      bool pattern = false;
      LoopArm arm;
      char local[48];
      local[0] = 0;
      if (!LoadEntry(i, e, &arm, steps, &stepCount, &pattern, local, (int)sizeof(local))) {
        if (memoryLimit(local)) {
          stopped = true;
          snprintf(limitMsg, sizeof(limitMsg), "Loops: %d of %d ready", n, audio);
          setErr(err, errLen, limitMsg);
          break;
        }
        if (local[0]) {
          setErr(err, errLen, local);
        }
        continue;
      }
      if (pattern || arm.frames < 2 || (arm.hold <= 0 && !arm.pcm)) {
        continue;
      }
      next[n].pcm = arm.pcm;
      next[n].hold = arm.hold;
      next[n].frames = arm.frames;
      next[n].rate = arm.rate;
      next[n].bpm = arm.bpm;
      strncpy(next[n].library, arm.library, sizeof(next[n].library) - 1);
      strncpy(next[n].name, arm.name, sizeof(next[n].name) - 1);
      n++;
    }
  }
  memcpy(instLoops, next, sizeof(instLoops));
  instLoopCount = n;
  ready = n;
  if (n == 0 && err && errLen > 0 && !err[0]) {
    setErr(err, errLen, "No loop samples");
    snprintf(limitMsg, sizeof(limitMsg), "%s", "No loop samples");
  }
  return n;
}

bool LoopLibrary::TrackArm(int track, LoopArm *arm) const {
  if (!arm || track < 0 || track >= kSongTracks || !loadedOk[track]) {
    return false;
  }
  *arm = loaded[track];
  return arm->pcm != nullptr || arm->hold > 0;
}
