#include "PcmHold.h"
#include "SdCard.h"
#include "WavPcm.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <stdio.h>

static const int kWin = 1024;

struct PcmWin {
  int16_t sample[kWin];
  int base;
  int count;
};

struct PcmSlot {
  PcmWin win[2];
  File file;
  char path[96];
  int frames;
  int rate;
  int dataOffset;
  int channels;
  int bits;
  volatile uint32_t seq;
  volatile int active;
  volatile int want;
  uint8_t open;
};

static PcmSlot slots[kPcmHolds];

static void setErr(char *err, int errLen, const char *msg) {
  if (err && errLen > 0) {
    snprintf(err, errLen, "%s", msg ? msg : "");
  }
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

static bool fillWin(PcmSlot &slot, PcmWin &win, int start) {
  if (start < 0) {
    start = 0;
  }
  if (slot.frames > 0 && start >= slot.frames) {
    start = 0;
  }
  int frameBytes = slot.channels * (slot.bits / 8);
  if (frameBytes < 1) {
    return false;
  }
  int count = kWin;
  if (slot.frames - start < count) {
    count = slot.frames - start;
  }
  if (count < 2) {
    return false;
  }
  if (!slot.file.seek(slot.dataOffset + (uint32_t)start * (uint32_t)frameBytes)) {
    return false;
  }
  const int kBatch = 256;
  uint8_t chunk[kBatch * 4];
  int wrote = 0;
  while (wrote < count) {
    int batch = count - wrote;
    if (batch > kBatch) {
      batch = kBatch;
    }
    int need = batch * frameBytes;
    int got = slot.file.read(chunk, need);
    if (got < need) {
      break;
    }
    decodeFrames(chunk, batch, slot.channels, slot.bits, win.sample + wrote);
    wrote += batch;
  }
  if (wrote < 2) {
    return false;
  }
  win.base = start;
  win.count = wrote;
  return true;
}

int pcmHoldOpen(const char *path, int *frames, int *rate, char *err, int errLen) {
  if (frames) {
    *frames = 0;
  }
  if (rate) {
    *rate = 0;
  }
  if (!path || !path[0] || !sdCard.Ensure()) {
    setErr(err, errLen, "No SD card");
    return 0;
  }
  int slotIndex = -1;
  for (int i = 0; i < kPcmHolds; i++) {
    if (slots[i].open && strcmp(slots[i].path, path) == 0) {
      if (frames) {
        *frames = slots[i].frames;
      }
      if (rate) {
        *rate = slots[i].rate;
      }
      setErr(err, errLen, "");
      return i + 1;
    }
  }
  for (int i = 0; i < kPcmHolds; i++) {
    if (!slots[i].open) {
      slotIndex = i;
      break;
    }
  }
  if (slotIndex < 0) {
    setErr(err, errLen, "Loop cache full");
    return 0;
  }
  PcmSlot &slot = slots[slotIndex];
  memset(&slot.win, 0, sizeof(slot.win));
  slot.seq = 0;
  slot.active = 0;
  slot.want = 0;
  slot.frames = 0;
  slot.open = 0;
  strncpy(slot.path, path, sizeof(slot.path) - 1);
  slot.file = SD.open(path, FILE_READ);
  if (!slot.file) {
    setErr(err, errLen, "Loop missing");
    return 0;
  }
  uint8_t hdr[768];
  int n = slot.file.read(hdr, sizeof(hdr));
  WavInfo info;
  if (!parseWavPrefix(hdr, n, &info)) {
    slot.file.close();
    setErr(err, errLen, info.error[0] ? info.error : "Bad WAV");
    return 0;
  }
  slot.frames = info.frames;
  slot.rate = info.rate;
  slot.dataOffset = info.dataOffset;
  slot.channels = info.channels;
  slot.bits = info.bits;
  if (!fillWin(slot, slot.win[0], 0)) {
    slot.file.close();
    setErr(err, errLen, "Loop read failed");
    return 0;
  }
  slot.active = 0;
  slot.seq = 2;
  slot.open = 1;
  if (frames) {
    *frames = slot.frames;
  }
  if (rate) {
    *rate = slot.rate;
  }
  setErr(err, errLen, "");
  return slotIndex + 1;
}

void pcmHoldClose(int id) {
  if (id < 1 || id > kPcmHolds) {
    return;
  }
  PcmSlot &slot = slots[id - 1];
  slot.open = 0;
  slot.seq++;
  if (slot.file) {
    slot.file.close();
  }
  slot.path[0] = 0;
  slot.frames = 0;
}

void pcmHoldWant(int id, int frame) {
  if (id < 1 || id > kPcmHolds) {
    return;
  }
  slots[id - 1].want = frame;
}

int16_t pcmHoldAt(int id, int frame) {
  if (id < 1 || id > kPcmHolds || frame < 0) {
    return 0;
  }
  PcmSlot &slot = slots[id - 1];
  if (!slot.open) {
    return 0;
  }
  slot.want = frame;
  uint32_t s1 = slot.seq;
  if (s1 & 1) {
    return 0;
  }
  int active = slot.active;
  if (active < 0 || active > 1) {
    return 0;
  }
  int base = slot.win[active].base;
  int count = slot.win[active].count;
  uint32_t s2 = slot.seq;
  if (s1 != s2 || count < 1) {
    return 0;
  }
  if (frame < base || frame >= base + count) {
    return 0;
  }
  return slot.win[active].sample[frame - base];
}

void pcmHoldService() {
  for (int i = 0; i < kPcmHolds; i++) {
    PcmSlot &slot = slots[i];
    if (!slot.open || !slot.file) {
      continue;
    }
    int active = slot.active;
    if (active < 0 || active > 1) {
      active = 0;
    }
    int want = slot.want;
    if (want < 0) {
      want = 0;
    }
    int base = slot.win[active].base;
    int count = slot.win[active].count;
    bool inside = want >= base && want + 512 < base + count;
    if (inside) {
      continue;
    }
    int next = active ^ 1;
    int start = want;
    if (start > 128) {
      start -= 128;
    } else {
      start = 0;
    }
    if (!fillWin(slot, slot.win[next], start)) {
      continue;
    }
    // Odd seq tells the reader to skip one sample while the window swaps.
    slot.seq = slot.seq + 1;
    slot.active = next;
    slot.seq = slot.seq + 1;
  }
}
