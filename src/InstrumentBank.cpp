#include "InstrumentBank.h"
#include "SdCard.h"
#include "PluginFormat.h"
#include "SynthRender.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

InstrumentBank instrumentBank;

static const int kLiveSlots = 4;
static const int kCycle = 168;

struct LiveSlot {
  int16_t *data;
  int length;
  int rate;
  int root;
  int loopStart;
  int loopEnd;
  uint8_t oneshot;
  volatile uint8_t ready;
  uint8_t id;
  char folder[24];
};

static LiveSlot liveSlots[kLiveSlots];

static void setErr(char *err, int errLen, const char *msg) {
  if (!err || errLen <= 0) {
    return;
  }
  snprintf(err, errLen, "%s", msg ? msg : "Error");
}

bool instrumentView(int id, ExtSampleView *out) {
  if (!out || id < 12 || id > 63) {
    return false;
  }
  for (int i = 0; i < kLiveSlots; i++) {
    if (!liveSlots[i].ready || liveSlots[i].id != (uint8_t)id) {
      continue;
    }
    out->data = liveSlots[i].data;
    out->length = liveSlots[i].length;
    out->rate = liveSlots[i].rate;
    out->rootMidi = liveSlots[i].root;
    out->loopStart = liveSlots[i].loopStart;
    out->loopEnd = liveSlots[i].loopEnd;
    out->oneshot = liveSlots[i].oneshot;
    if (!liveSlots[i].ready || !out->data || out->length <= 0) {
      return false;
    }
    return true;
  }
  return false;
}

const char *InstrumentBank::BuiltinName(int id) {
  static const char *names[] = {
      "Drums", "SFX", "Sine", "Square", "Saw", "Tri", "Organ", "Pluck", "Bell", "Flute", "Bass", "Pad"};
  if (id == kLoopsVoice) {
    return "Loops";
  }
  if (id < 0 || id > 11) {
    return "";
  }
  return names[id];
}

static int findLive(const char *folder) {
  for (int i = 0; i < kLiveSlots; i++) {
    if (liveSlots[i].ready && strcmp(liveSlots[i].folder, folder) == 0) {
      return i;
    }
  }
  return -1;
}

static int allocLive(const char *folder) {
  int existing = findLive(folder);
  if (existing >= 0) {
    return existing;
  }
  for (int i = 0; i < kLiveSlots; i++) {
    if (!liveSlots[i].data && !liveSlots[i].ready) {
      return i;
    }
  }
  liveSlots[0].ready = 0;
  delay(30);
  deckFree(liveSlots[0].data);
  memset(&liveSlots[0], 0, sizeof(liveSlots[0]));
  return 0;
}

static uint8_t idForFolder(const char *folder) {
  for (int i = 0; i < kLiveSlots; i++) {
    if (liveSlots[i].folder[0] && strcmp(liveSlots[i].folder, folder) == 0 && liveSlots[i].id >= 12) {
      return liveSlots[i].id;
    }
  }
  bool used[64];
  memset(used, 0, sizeof(used));
  for (int i = 0; i < kLiveSlots; i++) {
    if (liveSlots[i].id >= 12 && liveSlots[i].id <= 63) {
      used[liveSlots[i].id] = true;
    }
  }
  used[kLoopsVoice] = true;
  for (int id = 12; id <= 62; id++) {
    if (!used[id]) {
      return (uint8_t)id;
    }
  }
  return 12;
}

static void scaleGain(int16_t *data, int n, int gain) {
  if (gain >= 100 || !data) {
    return;
  }
  if (gain < 0) {
    gain = 0;
  }
  for (int i = 0; i < n; i++) {
    data[i] = (int16_t)((int32_t)data[i] * gain / 100);
  }
}

static void oom(char *err, int errLen, int needBytes) {
  int freeKb = (int)(deckFreeInternal() / 1024);
  int needKb = (needBytes + 1023) / 1024;
  if (err && errLen > 0) {
    snprintf(err, errLen, "Need %dk, %dk free", needKb, freeKb);
  }
}

static bool loadAudioFile(const char *path, const InstrumentManifest &man, int16_t **outData, int *outLen, int *outRate, char *err, int errLen) {
  int capFrames = psramFound() ? 48000 : 4096;
  int16_t *pcm = nullptr;
  int frames = 0;
  int rate = man.sampleRate > 0 ? man.sampleRate : 22050;
  bool isWav = false;
  {
    uint8_t probe[12];
    int n = 0;
    if (sdCard.ReadPrefix(path, probe, 12, &n) && n >= 12 && memcmp(probe, "RIFF", 4) == 0) {
      isWav = true;
    }
  }
  if (isWav) {
    pcm = (int16_t *)deckAlloc((size_t)capFrames * sizeof(int16_t));
    if (!pcm) {
      oom(err, errLen, capFrames * (int)sizeof(int16_t));
      return false;
    }
    int got = 0;
    if (!sdCard.LoadWavMono(path, pcm, capFrames, &got, &rate, err, errLen) || got < 1) {
      deckFree(pcm);
      if (err && errLen > 0 && !err[0]) {
        setErr(err, errLen, "WAV decode failed");
      }
      return false;
    }
    frames = got;
  } else {
    int maxBytes = capFrames * 2;
    uint8_t *raw = (uint8_t *)deckAlloc((size_t)maxBytes);
    if (!raw) {
      oom(err, errLen, maxBytes);
      return false;
    }
    int n = 0;
    if (!sdCard.ReadAll(path, raw, maxBytes, &n) || (n % 2) != 0 || n < 4) {
      deckFree(raw);
      setErr(err, errLen, "Raw sample too small");
      return false;
    }
    frames = n / 2;
    pcm = (int16_t *)raw;
  }
  if (man.loopEnd > frames) {
    deckFree(pcm);
    setErr(err, errLen, "Loop end past sample");
    return false;
  }
  scaleGain(pcm, frames, man.gain);
  *outData = pcm;
  *outLen = frames;
  *outRate = rate;
  return true;
}

int InstrumentBank::Load(int index, char *err, int errLen) {
  if (index < 0 || index >= count) {
    setErr(err, errLen, "No such instrument");
    return -1;
  }
  return LoadFolder(items[index].folder, err, errLen);
}

int InstrumentBank::LoadFolder(const char *folder, char *err, int errLen) {
  if (!manifestNameSafe(folder)) {
    setErr(err, errLen, "Bad instrument folder");
    return -1;
  }
  int have = findLive(folder);
  if (have >= 0) {
    return liveSlots[have].id;
  }
  if (!sdCard.Ensure()) {
    setErr(err, errLen, "No SD card");
    return -1;
  }
  char path[96];
  snprintf(path, sizeof(path), "/moth/instruments/%s/manifest.txt", folder);
  char text[1024];
  if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
    setErr(err, errLen, "No manifest");
    return -1;
  }
  InstrumentManifest man;
  if (!parseInstrumentManifest(text, (int)strlen(text), &man)) {
    setErr(err, errLen, man.error[0] ? man.error : "Bad manifest");
    return -1;
  }
  int16_t *pcm = nullptr;
  int frames = 0;
  int rate = kSampleRate;
  if (man.kind == INST_SUBTRACTIVE || man.kind == INST_FM) {
    pcm = (int16_t *)deckAlloc(kCycle * sizeof(int16_t));
    if (!pcm) {
      oom(err, errLen, kCycle * (int)sizeof(int16_t));
      return -1;
    }
    if (man.kind == INST_FM) {
      renderFm(pcm, kCycle, man.fmRatio, man.fmIndex);
    } else {
      renderSubtractive(pcm, kCycle, man.wave, man.cutoff, man.resonance);
    }
    scaleGain(pcm, kCycle, man.gain);
    frames = kCycle;
    rate = kSampleRate;
    man.oneshot = 0;
    man.loopStart = 0;
    man.loopEnd = kCycle;
    man.rootMidi = 60;
  } else {
    snprintf(path, sizeof(path), "/moth/instruments/%s/%s", folder, man.sampleFile);
    if (!loadAudioFile(path, man, &pcm, &frames, &rate, err, errLen)) {
      return -1;
    }
    if (man.kind == INST_WAVETABLE && man.loopEnd == 0) {
      man.loopStart = 0;
      man.loopEnd = frames;
      man.oneshot = 0;
    }
  }
  int slot = allocLive(folder);
  if (slot < 0) {
    deckFree(pcm);
    setErr(err, errLen, "Instrument cache full");
    return -1;
  }
  if (liveSlots[slot].data && liveSlots[slot].data != pcm) {
    liveSlots[slot].ready = 0;
    delay(30);
    deckFree(liveSlots[slot].data);
  }
  uint8_t id = idForFolder(folder);
  liveSlots[slot].data = pcm;
  liveSlots[slot].length = frames;
  liveSlots[slot].rate = rate;
  liveSlots[slot].root = man.rootMidi;
  liveSlots[slot].loopStart = man.loopStart;
  liveSlots[slot].loopEnd = man.loopEnd;
  liveSlots[slot].oneshot = man.oneshot;
  liveSlots[slot].id = id;
  memset(liveSlots[slot].folder, 0, sizeof(liveSlots[slot].folder));
  strncpy(liveSlots[slot].folder, folder, sizeof(liveSlots[slot].folder) - 1);
  liveSlots[slot].ready = 1;
  for (int i = 0; i < count; i++) {
    if (strcmp(items[i].folder, folder) == 0) {
      items[i].id = id;
      items[i].loaded = 1;
      items[i].kind = (uint8_t)man.kind;
      items[i].error[0] = 0;
    }
  }
  setErr(err, errLen, "");
  return id;
}

void InstrumentBank::Scan() {
  count = 0;
  memset(items, 0, sizeof(items));
  if (!sdCard.Ensure()) {
    return;
  }
  sdCard.Mkdir("/moth");
  sdCard.Mkdir("/moth/instruments");
  char names[kMaxPlugins][24];
  int n = sdCard.List("/moth/instruments", names, kMaxPlugins, true);
  for (int i = 0; i < n && count < kMaxPlugins; i++) {
    PluginInfo &info = items[count];
    strncpy(info.folder, names[i], sizeof(info.folder) - 1);
    char path[96];
    snprintf(path, sizeof(path), "/moth/instruments/%s/manifest.txt", info.folder);
    char text[1024];
    if (!sdCard.ReadText(path, text, (int)sizeof(text))) {
      strncpy(info.error, "No manifest", sizeof(info.error) - 1);
      strncpy(info.name, info.folder, sizeof(info.name) - 1);
      count++;
      continue;
    }
    InstrumentManifest man;
    if (!parseInstrumentManifest(text, (int)strlen(text), &man)) {
      strncpy(info.error, man.error, sizeof(info.error) - 1);
      strncpy(info.name, info.folder, sizeof(info.name) - 1);
      count++;
      continue;
    }
    strncpy(info.name, man.name, sizeof(info.name) - 1);
    info.kind = (uint8_t)man.kind;
    int live = findLive(info.folder);
    if (live >= 0) {
      info.loaded = 1;
      info.id = liveSlots[live].id;
    }
    count++;
  }
}

static void remapId(SongData *song, uint8_t from, uint8_t to) {
  if (song->currentVoice == from) {
    song->currentVoice = to;
  }
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      if (song->instruments[t][s] == from) {
        song->instruments[t][s] = to;
      }
    }
  }
}

void InstrumentBank::FillSongRefs(SongData *song) const {
  if (!song) {
    return;
  }
  uint8_t ids[kSongPluginSlots];
  int n = 0;
  uint8_t candidates[1 + kSongTracks * kSongSteps];
  int candCount = 0;
  candidates[candCount++] = song->currentVoice;
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      candidates[candCount++] = song->instruments[t][s];
    }
  }
  for (int c = 0; c < candCount && n < kSongPluginSlots; c++) {
    uint8_t id = candidates[c];
    if (id < 12) {
      continue;
    }
    bool seen = false;
    for (int i = 0; i < n; i++) {
      if (ids[i] == id) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      ids[n++] = id;
    }
  }
  memset(song->plugins, 0, sizeof(song->plugins));
  song->pluginCount = (uint8_t)n;
  for (int i = 0; i < n; i++) {
    song->plugins[i].id = ids[i];
    for (int s = 0; s < kLiveSlots; s++) {
      if (liveSlots[s].ready && liveSlots[s].id == ids[i]) {
        strncpy(song->plugins[i].name, liveSlots[s].folder, kPluginNameLen - 1);
        break;
      }
    }
  }
}

bool InstrumentBank::PrepareSong(SongData *song, char *err, int errLen) {
  if (!song) {
    return false;
  }
  setErr(err, errLen, "");
  bool ok = true;
  for (int i = 0; i < song->pluginCount && i < kSongPluginSlots; i++) {
    uint8_t oldId = song->plugins[i].id;
    const char *folder = song->plugins[i].name;
    if (!folder[0]) {
      if (oldId >= 12) {
        remapId(song, oldId, 0);
      }
      setErr(err, errLen, "Missing plugin name");
      ok = false;
      continue;
    }
    char local[48];
    int id = LoadFolder(folder, local, (int)sizeof(local));
    if (id < 0) {
      if (oldId >= 12) {
        remapId(song, oldId, 0);
      }
      setErr(err, errLen, local[0] ? local : "Plugin missing");
      ok = false;
      continue;
    }
    if (oldId != (uint8_t)id) {
      remapId(song, oldId, (uint8_t)id);
    }
    song->plugins[i].id = (uint8_t)id;
  }
  return ok;
}
