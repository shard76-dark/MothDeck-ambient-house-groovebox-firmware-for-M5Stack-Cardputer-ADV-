#include "diag_stats.h"

#include <string.h>

static const uint32_t kWindowMin = 100;
static const uint32_t kWindowMax = 60000;

static double stddev(double m2, uint32_t n) {
  if (n < 2 || m2 < 0) {
    return 0;
  }
  return m2 / (double)n;
}

static double sqrtApprox(double x) {
  if (x <= 0) {
    return 0;
  }
  double g = x;
  if (g > 1) {
    g = x / 2;
  }
  for (int i = 0; i < 12; i++) {
    g = (g + x / g) / 2;
  }
  return g;
}

DiagMeter::DiagMeter() {
  memset(this, 0, sizeof(*this));
  windowMs_ = 1000;
}

void DiagMeter::reset(uint64_t nowUs) {
  uint32_t keep = windowMs_;
  memset(this, 0, sizeof(*this));
  windowMs_ = keep ? keep : 1000;
  windowStartUs_ = nowUs;
}

void DiagMeter::setWindowMs(uint32_t ms) {
  if (ms < kWindowMin) {
    ms = kWindowMin;
  }
  if (ms > kWindowMax) {
    ms = kWindowMax;
  }
  windowMs_ = ms;
}

uint32_t DiagMeter::windowMs() const {
  return windowMs_;
}

void DiagMeter::addGap(uint32_t gapUs) {
  if (gapUs > maxGapUs_) {
    maxGapUs_ = gapUs;
  }
  gapCount_++;
  double x = (double)gapUs;
  double d = x - gapMean_;
  gapMean_ += d / (double)gapCount_;
  gapM2_ += d * (x - gapMean_);
}

void DiagMeter::addSessionGap(double sample, double *mean, double *m2, uint32_t *n) {
  (*n)++;
  double d = sample - *mean;
  *mean += d / (double)(*n);
  *m2 += d * (sample - *mean);
}

static bool bitTest(const uint8_t bits[16][16], int ch, int note) {
  if (ch < 1 || ch > 16 || note < 0 || note > 127) {
    return false;
  }
  return (bits[ch - 1][note >> 3] & (uint8_t)(1u << (note & 7))) != 0;
}

static void bitPut(uint8_t bits[16][16], int ch, int note, bool on) {
  if (ch < 1 || ch > 16 || note < 0 || note > 127) {
    return;
  }
  uint8_t mask = (uint8_t)(1u << (note & 7));
  if (on) {
    bits[ch - 1][note >> 3] |= mask;
  } else {
    bits[ch - 1][note >> 3] &= (uint8_t)~mask;
  }
}

int DiagMeter::heldCount() const {
  int n = 0;
  for (int c = 0; c < 16; c++) {
    for (int b = 0; b < 16; b++) {
      uint8_t v = bits_[c][b];
      while (v) {
        n += v & 1;
        v >>= 1;
      }
    }
  }
  return n;
}

void DiagMeter::clearHeldChannel(int ch) {
  if (ch < 1 || ch > 16) {
    return;
  }
  memset(bits_[ch - 1], 0, sizeof(bits_[ch - 1]));
  for (int i = 0; i < 128; i++) {
    if (held_[i].used && held_[i].ch == (uint8_t)ch) {
      held_[i].used = false;
    }
  }
}

void DiagMeter::noteOn(int ch, int note, int vel, uint64_t us) {
  if (vel <= 0) {
    noteOff(ch, note);
    return;
  }
  winNoteOn_++;
  noteOn_++;
  bool was = bitTest(bits_, ch, note);
  bitPut(bits_, ch, note, true);
  for (int i = 0; i < 128; i++) {
    if (held_[i].used && held_[i].ch == (uint8_t)ch && held_[i].note == (uint8_t)note) {
      held_[i].vel = (uint8_t)vel;
      held_[i].onUs = us;
      return;
    }
  }
  if (was) {
    return;
  }
  for (int i = 0; i < 128; i++) {
    if (!held_[i].used) {
      held_[i].used = true;
      held_[i].ch = (uint8_t)ch;
      held_[i].note = (uint8_t)note;
      held_[i].vel = (uint8_t)vel;
      held_[i].onUs = us;
      return;
    }
  }
}

void DiagMeter::noteOff(int ch, int note) {
  winNoteOff_++;
  noteOff_++;
  if (!bitTest(bits_, ch, note)) {
    extraOffs_++;
    return;
  }
  bitPut(bits_, ch, note, false);
  for (int i = 0; i < 128; i++) {
    if (held_[i].used && held_[i].ch == (uint8_t)ch && held_[i].note == (uint8_t)note) {
      held_[i].used = false;
      return;
    }
  }
}

void DiagMeter::allOff(int ch) {
  clearHeldChannel(ch);
}

void DiagMeter::absorbClocks(uint32_t raw, uint32_t dup, uint32_t burst, uint32_t playSteps, uint32_t tempoSteps, uint32_t spanMs, uint32_t arrivalSteps, uint64_t arrivalSpanUs) {
  winClocks_ += raw;
  clocks_ += raw;
  winDup_ += dup;
  sessDup_ += dup;
  winBurst_ += burst;
  sessBurst_ += burst;
  winPlaySteps_ += playSteps;
  sessPlaySteps_ += playSteps;
  winTempoSteps_ += tempoSteps;
  sessTempoSteps_ += tempoSteps;
  winMidiSpanMs_ += spanMs;
  sessMidiSpanMs_ += spanMs;
  winArrivalSteps_ += arrivalSteps;
  sessArrivalSteps_ += arrivalSteps;
  winArrivalSpanUs_ += arrivalSpanUs;
  sessArrivalSpanUs_ += arrivalSpanUs;
}

void DiagMeter::absorbPackets(uint32_t n) {
  winPackets_ += n;
}

void DiagMeter::clock(uint64_t us, uint16_t bleTs) {
  if (fold_.have) {
    uint16_t delta = bleClockDelta(fold_.lastTs, bleTs);
    if (delta > 0 && delta < 2000) {
      addSessionGap((double)delta, &sessMidiMean_, &sessMidiM2_, &sessMidiN_);
    }
  }
  ClockFold before = fold_;
  clockFoldFeed(&fold_, bleTs, 1, us);
  uint32_t raw = fold_.raw - before.raw;
  uint32_t dup = fold_.dup - before.dup;
  uint32_t burst = fold_.burst - before.burst;
  uint32_t play = fold_.playSteps - before.playSteps;
  uint32_t tempo = fold_.tempoSteps - before.tempoSteps;
  uint32_t span = fold_.spanMs - before.spanMs;
  uint32_t arrN = fold_.arrivalSteps - before.arrivalSteps;
  uint64_t arrUs = fold_.arrivalSpanUs - before.arrivalSpanUs;
  absorbClocks(raw, dup, burst, play, tempo, span, arrN, arrUs);
  if (us > 0) {
    if (sessHaveClock_ && us >= sessClockLastUs_) {
      uint64_t deltaUs = us - sessClockLastUs_;
      addSessionGap((double)deltaUs, &sessArrivalMean_, &sessArrivalM2_, &sessArrivalN_);
    }
    sessClockLastUs_ = us;
    if (!sessHaveClock_) {
      sessClockFirstUs_ = us;
      sessHaveClock_ = true;
    }
  }
}

void DiagMeter::packet(uint64_t us) {
  if (havePacket_ && us >= lastPacketUs_) {
    uint64_t gap = us - lastPacketUs_;
    if (gap > 0xFFFFFFFFULL) {
      gap = 0xFFFFFFFFULL;
    }
    addGap((uint32_t)gap);
  }
  havePacket_ = true;
  lastPacketUs_ = us;
  winPackets_++;
}

void DiagMeter::parseError(int n) {
  if (n <= 0) {
    return;
  }
  winErrors_ += (uint32_t)n;
  errors_ += (uint32_t)n;
}

void DiagMeter::noteParserMismatch() {
  winMismatch_++;
  mismatch_++;
}

bool DiagMeter::takeWindow(uint64_t nowUs, DiagWindow *out) {
  if (!out) {
    return false;
  }
  if (nowUs < windowStartUs_) {
    windowStartUs_ = nowUs;
    return false;
  }
  uint64_t elapsedUs = nowUs - windowStartUs_;
  if (elapsedUs < (uint64_t)windowMs_ * 1000ULL) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->windowMs = windowMs_;
  out->elapsedMs = (uint32_t)(elapsedUs / 1000ULL);
  if (out->elapsedMs == 0) {
    out->elapsedMs = 1;
  }
  out->clocks = winClocks_;
  out->clocksTotal = clocks_;
  out->noteOn = winNoteOn_;
  out->noteOff = winNoteOff_;
  out->noteOnTotal = noteOn_;
  out->noteOffTotal = noteOff_;
  out->hanging = (uint32_t)heldCount();
  out->extraOffs = extraOffs_;
  out->parseErrors = winErrors_;
  out->parseErrorsTotal = errors_;
  out->parserMismatch = winMismatch_;
  out->parserMismatchTotal = mismatch_;
  out->packets = winPackets_;
  out->pps = (double)winPackets_ * 1000.0 / (double)out->elapsedMs;
  out->maxGapUs = maxGapUs_;
  double jitter = sqrtApprox(stddev(gapM2_, gapCount_));
  if (jitter < 0) {
    jitter = 0;
  } else if (jitter > 100000000.0) {
    jitter = 100000000.0;
  }
  out->jitterUs = (uint32_t)(jitter + 0.5);

  out->dup = winDup_;
  out->dupTotal = sessDup_;
  out->burst = winBurst_;
  out->playSteps = winPlaySteps_;
  if (winTempoSteps_ >= 1 && winMidiSpanMs_ > 0) {
    out->hasBpmMidi = true;
    out->bpmMidi = 60000.0 * (double)winTempoSteps_ / (24.0 * (double)winMidiSpanMs_);
  }
  if (winArrivalSteps_ >= 1 && winArrivalSpanUs_ > 0) {
    out->hasBpmArrival = true;
    out->bpmArrival = 60000000.0 * (double)winArrivalSteps_ / (24.0 * (double)winArrivalSpanUs_);
  }

  winPackets_ = 0;
  winClocks_ = 0;
  winNoteOn_ = 0;
  winNoteOff_ = 0;
  winErrors_ = 0;
  winMismatch_ = 0;
  gapCount_ = 0;
  maxGapUs_ = 0;
  gapMean_ = 0;
  gapM2_ = 0;
  winMidiSpanMs_ = 0;
  winDup_ = 0;
  winBurst_ = 0;
  winPlaySteps_ = 0;
  winTempoSteps_ = 0;
  winArrivalSteps_ = 0;
  winArrivalSpanUs_ = 0;
  winClockFirstUs_ = 0;
  winClockLastUs_ = 0;
  windowStartUs_ = nowUs;
  return true;
}

uint32_t DiagMeter::noteOnTotal() const {
  return noteOn_;
}

uint32_t DiagMeter::noteOffTotal() const {
  return noteOff_;
}

uint32_t DiagMeter::clockTotal() const {
  return clocks_;
}

uint32_t DiagMeter::hanging() const {
  return (uint32_t)heldCount();
}

uint32_t DiagMeter::extraOffs() const {
  return extraOffs_;
}

uint32_t DiagMeter::parseErrors() const {
  return errors_;
}

uint32_t DiagMeter::parserMismatch() const {
  return mismatch_;
}

int DiagMeter::copyHeld(DiagHeld *dst, int max) const {
  if (!dst || max <= 0) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < 128 && n < max; i++) {
    if (!held_[i].used) {
      continue;
    }
    dst[n].ch = held_[i].ch;
    dst[n].note = held_[i].note;
    dst[n].vel = held_[i].vel;
    dst[n].onUs = held_[i].onUs;
    n++;
  }
  return n;
}

void DiagMeter::bpmSummary(DiagBpm *out) const {
  memset(out, 0, sizeof(*out));
  out->clocks = clocks_;
  out->midiSpanMs = sessMidiSpanMs_;
  out->arrivalSpanUs = sessArrivalSpanUs_;
  if (sessTempoSteps_ >= 1 && sessMidiSpanMs_ > 0) {
    out->hasBpmMidi = true;
    out->bpmMidi = 60000.0 * (double)sessTempoSteps_ / (24.0 * (double)sessMidiSpanMs_);
  }
  if (sessArrivalSteps_ >= 1 && sessArrivalSpanUs_ > 0) {
    out->hasBpmArrival = true;
    out->bpmArrival = 60000000.0 * (double)sessArrivalSteps_ / (24.0 * (double)sessArrivalSpanUs_);
  }
  if (sessArrivalN_ >= 2) {
    out->hasArrivalJitter = true;
    out->arrivalJitterUs = sqrtApprox(stddev(sessArrivalM2_, sessArrivalN_));
  }
  if (sessMidiN_ >= 2) {
    out->hasMidiJitter = true;
    out->midiJitterMs = sqrtApprox(stddev(sessMidiM2_, sessMidiN_));
  }
}
