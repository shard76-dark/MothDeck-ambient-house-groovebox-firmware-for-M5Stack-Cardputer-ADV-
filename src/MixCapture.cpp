#include "MixCapture.h"
#include "DspHot.h"

static int16_t take[kCaptureFrames];
static int filled = 0;
static int armed = 0;
static int playing = 0;
static uint32_t phase = 0;
static uint32_t inc = 0;

void mixCaptureArm() {
  filled = 0;
  armed = 1;
  playing = 0;
}

void mixCaptureFeed(int sample) {
  if (!armed) {
    return;
  }
  if (filled >= kCaptureFrames) {
    armed = 0;
    return;
  }
  take[filled++] = (int16_t)dspSat16(sample);
  if (filled >= kCaptureFrames) {
    armed = 0;
  }
}

bool mixCaptureReady() {
  return filled >= 2 && !armed;
}

int mixCaptureLength() {
  return filled;
}

void mixCapturePlay(int stepNum, int stepDen) {
  if (filled < 2 || armed) {
    return;
  }
  if (stepNum < 1) {
    stepNum = 1;
  }
  if (stepDen < 1) {
    stepDen = 1;
  }
  inc = (uint32_t)(((int64_t)stepNum << 16) / stepDen);
  if (inc < 1) {
    inc = 1;
  }
  phase = 0;
  playing = 1;
}

static int at(int frame) {
  if (filled < 1) {
    return 0;
  }
  if (frame < 0) {
    frame = 0;
  }
  if (frame >= filled) {
    frame = filled - 1;
  }
  return take[frame];
}

int mixCaptureMix() {
  if (!playing || filled < 2) {
    return 0;
  }
  int pos = (int)(phase >> 16);
  if (pos >= filled - 1) {
    playing = 0;
    return at(filled - 1);
  }
  int frac = (int)(phase & 0xFFFF);
  int sample = dspHermite(at(pos - 1), at(pos), at(pos + 1), at(pos + 2), frac, 65536);
  phase += inc;
  return sample;
}
