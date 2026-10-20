#ifndef BeatDisplay_h
#define BeatDisplay_h

// Bar/beat position and the four digits for the HT16K33.
// snap.step is the next step the tracker will play (absolute, across
// patterns). The digit that should be on screen is the step that just
// sounded, one behind that, wrapping inside the current pattern.

struct BarBeat {
  int bar;
  int beat;
  int stepInBar;
  int local;
};

inline BarBeat barBeatFromNextStep(int nextStep, int patternLength, int pattern) {
  BarBeat bb;
  int span = patternLength > 0 ? patternLength : 16;
  if (span > 256) {
    span = 256;
  }
  int base = pattern * span;
  if (base < 0) {
    base = 0;
  }
  int sounded = nextStep - 1;
  if (sounded < base) {
    sounded = base + span - 1;
  }
  int local = sounded - base;
  if (local < 0) {
    local = 0;
  }
  if (local >= span) {
    local = span - 1;
  }
  bb.local = local;
  bb.stepInBar = local % 16;
  bb.bar = local / 16 + 1;
  bb.beat = bb.stepInBar / 4 + 1;
  return bb;
}

// True on the step that starts a quarter-note beat (local steps 0, 4, 8, 12
// of each bar).
inline bool isBeatStep(int nextStep, int patternLength, int pattern) {
  BarBeat bb = barBeatFromNextStep(nextStep, patternLength, pattern);
  return (bb.stepInBar % 4) == 0;
}

// digit 0..9, or 10 for a blank. dot is 1 when that digit lights its decimal.
struct SevenSeg {
  int digit[4];
  int dot[4];
};

// While playing: " b.t " with the decimal between bar and beat (bar 1 beat 3
// reads 1.3). Otherwise the BPM, right aligned in the four digits.
inline void formatBeatReadout(bool playing, int bar, int beat, int bpm, SevenSeg *out) {
  if (!out) {
    return;
  }
  for (int i = 0; i < 4; i++) {
    out->digit[i] = 10;
    out->dot[i] = 0;
  }
  if (playing) {
    if (bar < 1) {
      bar = 1;
    } else if (bar > 8) {
      bar = 8;
    }
    if (beat < 1) {
      beat = 1;
    } else if (beat > 4) {
      beat = 4;
    }
    out->digit[1] = bar;
    out->dot[1] = 1;
    out->digit[2] = beat;
    return;
  }
  if (bpm < 0) {
    bpm = 0;
  } else if (bpm > 999) {
    bpm = 999;
  }
  int hundreds = bpm / 100;
  int tens = (bpm / 10) % 10;
  int ones = bpm % 10;
  if (hundreds > 0) {
    out->digit[1] = hundreds;
  }
  if (hundreds > 0 || tens > 0) {
    out->digit[2] = tens;
  }
  out->digit[3] = ones;
}

#endif
