#include "DrumKit.h"
#include "DevLog.h"
#include "KitFormat.h"
#include "SdCard.h"
#include "WavPcm.h"
#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>

DrumKit drumKit;

// Whole kit stays small enough for internal RAM after BLE and the screen.
// A kit that fits is copied in. A 44100 Hz kit is larger, so the same 16 KB
// holds a short attack per pad plus a sliding window per track. The refill
// runs on the other core and does not take the loop-window pool.
static const int kKitFrames = 8000;
static const int kPadFrames = 1800;
static const int kAttack = 256;
static const int kStreamTracks = 4;
static const int kWin = (kKitFrames - kKitPads * kAttack) / (kStreamTracks * 2);
static_assert(kWin >= 256, "kit stream window");

static int16_t *kitBlock = nullptr;
static int kitOffset[kKitPads];
static int kitLength[kKitPads];
static int kitRate[kKitPads];
static volatile uint8_t kitReady = 0;
static volatile uint8_t streamMode = 0;

struct PadMeta {
  char path[96];
  int rate;
  int frames;
  int dataOffset;
  int channels;
  int bits;
  int attack;
};

struct StreamTrack {
  File file;
  int filePad;
  int base[2];
  int count[2];
  volatile int active;
  volatile int playPad;
  volatile uint32_t seq;
  volatile int wantFrame;
  volatile int wantPad;
};

static PadMeta padMeta[kKitPads];
static int16_t *attackPtr[kKitPads];
static int16_t *winPtr[kStreamTracks][2];
static StreamTrack streamTracks[kStreamTracks];
static volatile int streamRun = 0;
static TaskHandle_t streamTask = nullptr;

static void setErr(char *err, int errLen, const char *msg) {
  if (err && errLen > 0) {
    snprintf(err, errLen, "%s", msg ? msg : "");
  }
}

bool drumKitHit(int note, DrumHitView *out) {
  if (!out || !kitReady || note < 0 || note >= kKitPads) {
    return false;
  }
  if (kitLength[note] < 2) {
    return false;
  }
  out->length = kitLength[note];
  out->rate = kitRate[note] > 0 ? kitRate[note] : 22050;
  out->streamed = streamMode ? 1 : 0;
  if (streamMode) {
    out->data = nullptr;
    return true;
  }
  if (!kitBlock) {
    return false;
  }
  out->data = kitBlock + kitOffset[note];
  return true;
}

static int decodeFrames(const uint8_t *p, int n, int channels, int bits, int16_t *dst) {
  for (int i = 0; i < n; i++) {
    int32_t acc = 0;
    for (int c = 0; c < channels; c++) {
      int32_t s;
      if (bits == 8) {
        s = ((int32_t)(*p++) - 128) << 8;
      } else {
        s = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        p += 2;
      }
      acc += s;
    }
    if (channels == 2) {
      acc /= 2;
    }
    if (acc > 32767) {
      acc = 32767;
    } else if (acc < -32768) {
      acc = -32768;
    }
    dst[i] = (int16_t)acc;
  }
  return n;
}

static bool readPadHeader(PadMeta *meta) {
  SdBusGuard bus;
  File file = SD.open(meta->path, FILE_READ);
  if (!file) {
    return false;
  }
  uint8_t hdr[768];
  int n = file.read(hdr, sizeof(hdr));
  file.close();
  WavInfo info;
  if (!parseWavPrefix(hdr, n, &info) || info.frames < 2) {
    return false;
  }
  if (info.bits != 8 && info.bits != 16) {
    return false;
  }
  meta->rate = info.rate > 0 ? info.rate : 22050;
  meta->frames = info.frames;
  meta->dataOffset = info.dataOffset;
  meta->channels = info.channels;
  meta->bits = info.bits;
  meta->attack = info.frames < kAttack ? info.frames : kAttack;
  return true;
}

static bool fillAttack(PadMeta *meta, int16_t *dst) {
  SdBusGuard bus;
  File file = SD.open(meta->path, FILE_READ);
  if (!file) {
    return false;
  }
  int frameBytes = meta->channels * (meta->bits / 8);
  if (frameBytes < 1 || !file.seek(meta->dataOffset)) {
    file.close();
    return false;
  }
  uint8_t chunk[256 * 4];
  int need = meta->attack * frameBytes;
  if (need > (int)sizeof(chunk)) {
    file.close();
    return false;
  }
  int got = file.read(chunk, need);
  file.close();
  if (got < need) {
    return false;
  }
  decodeFrames(chunk, meta->attack, meta->channels, meta->bits, dst);
  return true;
}

static bool fillWindow(int track, int which, int pad, int start) {
  StreamTrack &tr = streamTracks[track];
  PadMeta &meta = padMeta[pad];
  if (start < 0) {
    start = 0;
  }
  if (start >= meta.frames) {
    start = 0;
  }
  if (tr.filePad != pad) {
    if (tr.file) {
      tr.file.close();
    }
    tr.file = SD.open(meta.path, FILE_READ);
    tr.filePad = tr.file ? pad : -1;
    if (!tr.file) {
      return false;
    }
  }
  int frameBytes = meta.channels * (meta.bits / 8);
  if (frameBytes < 1 || !tr.file.seek(meta.dataOffset + (uint32_t)start * (uint32_t)frameBytes)) {
    return false;
  }
  int count = kWin;
  if (meta.frames - start < count) {
    count = meta.frames - start;
  }
  if (count < 2 || !winPtr[track][which]) {
    return false;
  }
  const int kBatch = 128;
  uint8_t chunk[kBatch * 4];
  int wrote = 0;
  while (wrote < count && streamRun) {
    int batch = count - wrote;
    if (batch > kBatch) {
      batch = kBatch;
    }
    int need = batch * frameBytes;
    int got = tr.file.read(chunk, need);
    if (got < need) {
      break;
    }
    decodeFrames(chunk, batch, meta.channels, meta.bits, winPtr[track][which] + wrote);
    wrote += batch;
  }
  if (wrote < 2) {
    return false;
  }
  tr.seq = tr.seq + 1;
  tr.base[which] = start;
  tr.count[which] = wrote;
  tr.playPad = pad;
  tr.active = which;
  tr.seq = tr.seq + 1;
  return true;
}

static void streamTaskFn(void *) {
  while (streamRun) {
    for (int t = 0; t < kStreamTracks && streamRun; t++) {
      StreamTrack &tr = streamTracks[t];
      int pad = tr.wantPad;
      int want = tr.wantFrame;
      if (pad < 0 || pad >= kKitPads) {
        continue;
      }
      int active = tr.active;
      if (active < 0 || active > 1) {
        active = 0;
      }
      int ahead = kWin / 2;
      bool covered = tr.playPad == pad && want >= tr.base[active] && want + ahead < tr.base[active] + tr.count[active];
      if (covered) {
        continue;
      }
      int start = want > 64 ? want - 64 : 0;
      sdBusLock();
      fillWindow(t, active ^ 1, pad, start);
      sdBusUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  streamTask = nullptr;
  vTaskDelete(nullptr);
}

int drumStreamAt(int track, int pad, int frame) {
  if (!streamMode || !kitReady || track < 0 || track >= kStreamTracks || pad < 0 || pad >= kKitPads) {
    return 0;
  }
  PadMeta &meta = padMeta[pad];
  if (meta.frames < 2) {
    return 0;
  }
  if (frame < 0) {
    frame = 0;
  } else if (frame >= meta.frames) {
    frame = meta.frames - 1;
  }
  if (frame < meta.attack && attackPtr[pad]) {
    StreamTrack &tr = streamTracks[track];
    tr.wantPad = pad;
    tr.wantFrame = frame;
    return attackPtr[pad][frame];
  }
  StreamTrack &tr = streamTracks[track];
  tr.wantPad = pad;
  tr.wantFrame = frame;
  uint32_t s1 = tr.seq;
  if (s1 & 1) {
    return 0;
  }
  int active = tr.active;
  int playPad = tr.playPad;
  int base = 0;
  int count = 0;
  int16_t *sample = nullptr;
  if (active >= 0 && active <= 1) {
    base = tr.base[active];
    count = tr.count[active];
    sample = winPtr[track][active];
  }
  uint32_t s2 = tr.seq;
  if (s1 != s2 || playPad != pad || !sample || frame < base || frame >= base + count) {
    return 0;
  }
  return sample[frame - base];
}

static void unloadKit() {
  streamRun = 0;
  kitReady = 0;
  streamMode = 0;
  delay(30);
  for (int i = 0; i < 40 && streamTask; i++) {
    delay(2);
  }
  if (streamTask) {
    vTaskDelete(streamTask);
    streamTask = nullptr;
  }
  sdBusLock();
  for (int t = 0; t < kStreamTracks; t++) {
    if (streamTracks[t].file) {
      streamTracks[t].file.close();
    }
    streamTracks[t].filePad = -1;
    streamTracks[t].playPad = -1;
    streamTracks[t].wantPad = -1;
    streamTracks[t].active = 0;
    streamTracks[t].count[0] = 0;
    streamTracks[t].count[1] = 0;
  }
  sdBusUnlock();
  deckFree(kitBlock);
  kitBlock = nullptr;
  memset(kitOffset, 0, sizeof(kitOffset));
  memset(kitLength, 0, sizeof(kitLength));
  memset(kitRate, 0, sizeof(kitRate));
  memset(attackPtr, 0, sizeof(attackPtr));
  memset(winPtr, 0, sizeof(winPtr));
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
  if (!sdCard.LoadWavMono(path, dst, dstFrames, got, rate, err, errLen)) {
    return false;
  }
  if (!got || *got < 2) {
    setErr(err, errLen, "Pad too short");
    return false;
  }
  return true;
}

static void oom(char *err, int errLen, int needBytes) {
  DEV_LOGF("KIT: skipped need=%d free=%u\n", needBytes, (unsigned)deckFreeInternal());
  (void)needBytes;
  setErr(err, errLen, "Kit unchanged");
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
  PadMeta probed[kKitPads];
  memset(probed, 0, sizeof(probed));
  int total = 0;
  for (int p = 0; p < kKitPads; p++) {
    snprintf(probed[p].path, sizeof(probed[p].path), "/moth/drums/%s/%s", folders[sd], man.pads[p]);
    if (!readPadHeader(&probed[p])) {
      setErr(err, errLen, "Pad missing");
      return false;
    }
    total += probed[p].frames;
  }
  if (total > kKitFrames && psramFound()) {
    int16_t *block = (int16_t *)deckAlloc((size_t)total * sizeof(int16_t));
    if (block) {
      int used = 0;
      int off[kKitPads];
      int len[kKitPads];
      int rate[kKitPads];
      bool ok = true;
      for (int p = 0; p < kKitPads; p++) {
        int got = 0;
        int padRate = 22050;
        if (!decodePad(probed[p].path, block + used, probed[p].frames, &got, &padRate, err, errLen)) {
          ok = false;
          break;
        }
        off[p] = used;
        len[p] = got;
        rate[p] = padRate;
        used += got;
      }
      if (ok) {
        unloadKit();
        kitBlock = block;
        memcpy(kitOffset, off, sizeof(kitOffset));
        memcpy(kitLength, len, sizeof(kitLength));
        memcpy(kitRate, rate, sizeof(kitRate));
        streamMode = 0;
        selected = index;
        kitReady = 1;
        setErr(err, errLen, "");
        return true;
      }
      deckFree(block);
    }
  }
  if (total > kKitFrames) {
    int16_t *block = (int16_t *)deckAlloc((size_t)kKitFrames * sizeof(int16_t));
    if (!block) {
      oom(err, errLen, kKitFrames * (int)sizeof(int16_t));
      return false;
    }
    memset(block, 0, (size_t)kKitFrames * sizeof(int16_t));
    int16_t *attacks[kKitPads];
    int16_t *wins[kStreamTracks][2];
    int16_t *cursor = block;
    for (int p = 0; p < kKitPads; p++) {
      attacks[p] = cursor;
      cursor += kAttack;
    }
    for (int t = 0; t < kStreamTracks; t++) {
      wins[t][0] = cursor;
      cursor += kWin;
      wins[t][1] = cursor;
      cursor += kWin;
    }
    for (int p = 0; p < kKitPads; p++) {
      if (!fillAttack(&probed[p], attacks[p])) {
        deckFree(block);
        setErr(err, errLen, "Pad missing");
        return false;
      }
    }
    unloadKit();
    kitBlock = block;
    memcpy(padMeta, probed, sizeof(padMeta));
    memcpy(attackPtr, attacks, sizeof(attackPtr));
    memcpy(winPtr, wins, sizeof(winPtr));
    for (int t = 0; t < kStreamTracks; t++) {
      streamTracks[t].filePad = -1;
      streamTracks[t].playPad = -1;
      streamTracks[t].wantPad = -1;
      streamTracks[t].wantFrame = 0;
      streamTracks[t].active = 0;
      streamTracks[t].seq = 0;
      streamTracks[t].base[0] = 0;
      streamTracks[t].base[1] = 0;
      streamTracks[t].count[0] = 0;
      streamTracks[t].count[1] = 0;
    }
    for (int p = 0; p < kKitPads; p++) {
      kitLength[p] = probed[p].frames;
      kitRate[p] = probed[p].rate;
    }
    streamMode = 1;
    streamRun = 1;
    BaseType_t made = xTaskCreatePinnedToCore(streamTaskFn, "moth-kit", 6144, nullptr, 1, &streamTask, 0);
    if (made != pdPASS) {
      streamRun = 0;
      streamTask = nullptr;
      streamMode = 0;
      deckFree(kitBlock);
      kitBlock = nullptr;
      selected = 0;
      setErr(err, errLen, "Kit unchanged");
      return false;
    }
    selected = index;
    kitReady = 1;
    setErr(err, errLen, "");
    return true;
  }
  int16_t *block = (int16_t *)deckAlloc((size_t)kKitFrames * sizeof(int16_t));
  if (!block) {
    oom(err, errLen, kKitFrames * (int)sizeof(int16_t));
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
      setErr(err, errLen, "Kit unchanged");
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
  unloadKit();
  kitBlock = block;
  memcpy(kitOffset, off, sizeof(kitOffset));
  memcpy(kitLength, len, sizeof(kitLength));
  memcpy(kitRate, rate, sizeof(kitRate));
  streamMode = 0;
  selected = index;
  kitReady = 1;
  setErr(err, errLen, "");
  return true;
}
