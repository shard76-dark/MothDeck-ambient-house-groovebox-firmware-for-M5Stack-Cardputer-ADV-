#include "PatchBlocks.h"

// Bit p set means pitch class p is in the scale, relative to the root.
// major 0 2 4 5 7 9 11, minor 0 2 3 5 7 8 10, harmonic minor 0 2 3 5 7 8 11,
// mixolydian 0 2 4 5 7 9 10, phrygian 0 1 3 5 7 8 10.
static const uint16_t kScaleBits[7] = {
  0,
  2741,
  1453,
  2477,
  1717,
  1451,
  4095
};

int scaleLock(int note, int root, int mode) {
  if (note < 0) {
    note = 0;
  } else if (note > 127) {
    note = 127;
  }
  if (mode < 1 || mode > 6) {
    return note;
  }
  if (root < 0) {
    root = 0;
  }
  root %= 12;
  int pc = note % 12;
  int rel = (pc - root + 12) % 12;
  uint16_t bits = kScaleBits[mode];
  if (bits & (uint16_t)(1u << rel)) {
    return note;
  }
  int best = 0;
  for (int d = 1; d <= 6; d++) {
    int down = (rel - d + 12) % 12;
    if (bits & (uint16_t)(1u << down)) {
      best = -d;
      break;
    }
    int up = (rel + d) % 12;
    if (bits & (uint16_t)(1u << up)) {
      best = d;
      break;
    }
  }
  int out = note + best;
  if (out < 0) {
    out = 0;
  } else if (out > 127) {
    out = 127;
  }
  return out;
}

int shiftSemi(int freq, int coarse) {
  // 2^(n/12) in Q16 for n = -12..12. One octave is a shift of the integer.
  static const uint32_t kQ16[25] = {
    32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715,
    131072
  };
  if (freq < 1) {
    freq = 1;
  }
  if (coarse > 24) {
    coarse = 24;
  } else if (coarse < -24) {
    coarse = -24;
  }
  while (coarse > 12) {
    freq *= 2;
    coarse -= 12;
  }
  while (coarse < -12) {
    freq /= 2;
    coarse += 12;
  }
  if (freq < 1) {
    freq = 1;
  }
  int out = (int)(((int64_t)freq * (int64_t)kQ16[coarse + 12]) / 65536);
  if (out < 1) {
    out = 1;
  } else if (out > 20000) {
    out = 20000;
  }
  return out;
}

int glideAt(int from, int to, int pos, int len) {
  if (len <= 0 || pos >= len) {
    return to;
  }
  if (pos <= 0) {
    return from;
  }
  return from + (int)(((int64_t)(to - from) * pos) / len);
}

int heldAdd(uint8_t *notes, int *count, int note) {
  if (!notes || !count) {
    return 0;
  }
  if (note < 0) {
    note = 0;
  } else if (note > 127) {
    note = 127;
  }
  if (*count < 0) {
    *count = 0;
  }
  for (int i = 0; i < *count; i++) {
    if (notes[i] == (uint8_t)note) {
      return 0;
    }
  }
  if (*count >= 8) {
    return 0;
  }
  notes[*count] = (uint8_t)note;
  *count += 1;
  return *count == 1 ? 1 : 2;
}

int heldRemove(uint8_t *notes, int *count, int *index, int note) {
  if (!notes || !count || !index || *count <= 0) {
    if (count) {
      *count = 0;
    }
    return 0;
  }
  int found = -1;
  for (int i = 0; i < *count; i++) {
    if (notes[i] == (uint8_t)note) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    return *count;
  }
  for (int i = found; i < *count - 1; i++) {
    notes[i] = notes[i + 1];
  }
  *count -= 1;
  if (*count <= 0) {
    *index = 0;
    return 0;
  }
  if (*index > found) {
    *index -= 1;
  }
  if (*index >= *count) {
    *index = 0;
  }
  if (*index < 0) {
    *index = 0;
  }
  return *count;
}
