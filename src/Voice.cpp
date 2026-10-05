#include "Voice.h"
#include "DspHot.h"
#include "MidiMap.h"
#include "DefaultSamples.h"
#include "InstrumentBank.h"
#include "DrumKit.h"
#include "BoardConfig.h"
#include <math.h>
#include <string.h>

// Keyboard order, not the old kick/snare/hat groups. See docs/FORMATS.md.
// C kick, C# rim, D snare, D# clap, E hat, F open hat, F# perc, G tom,
// G# shaker, A ride, A# snap, B crash. Unsigned 8-bit mono, 22050 Hz.
static const uint8_t *const kDrumWaves[12] = {
  kick1, snare1, special1, hihat1, kick2, snare2, special2, hihat2, kick3, snare3, special3, hihat3
};
static const int kDrumLens[12] = {
  kick1Length, snare1Length, special1Length, hihat1Length,
  kick2Length, snare2Length, special2Length, hihat2Length,
  kick3Length, snare3Length, special3Length, hihat3Length
};
static const int kDrumRates[12] = {
  22050, 22050, 22050, 22050, 22050, 22050, 22050, 22050, 22050, 22050, 22050, 22050
};
static const int kSfxRate = 22050;

__attribute__((weak)) bool drumKitHit(int note, DrumHitView *out) {
  (void)note;
  (void)out;
  return false;
}

static const uint8_t *const kSfxWaves[12] = {
  sfx1, sfx2, sfx3, sfx4, sfx5, sfx6, sfx7, sfx8, sfx9, sfx10, sfx11, sfx12
};
static const int kSfxLens[12] = {
  sfx1Length, sfx2Length, sfx3Length, sfx4Length, sfx5Length, sfx6Length,
  sfx7Length, sfx8Length, sfx9Length, sfx10Length, sfx11Length, sfx12Length
};

static const uint8_t kEnvelopes[4][101] = {
  {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,98,96,94,92,90,88,86,84,82,80,78,76,74,72,70,68,66,64,62,60,58,56,54,52,50,48,46,44,42,40,38,36,34,32,30,28,26,24,22,20,18,16,14,12,10,0,0,0,0,0},
  {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,4,2,1,1,0},
  {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100},
  {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100}
};

static const uint8_t kWhooshSin[101] = {
  10,9,9,9,9,9,9,9,9,9,9,8,8,8,8,7,7,7,7,6,6,6,5,5,5,5,4,4,4,3,3,3,2,2,2,2,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,4,4,4,4,5,5,5,6,6,6,7,7,7,7,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9
};

static const int16_t kNoteFreq[48] = {
  250,293,340,390,444,501,562,626,694,765,840,918,1000,1085,1173,1265,
  1361,1460,1562,1668,1777,1890,2006,2126,2250,2376,2506,2640,2777,2918,3062,3210,
  3361,3515,3673,3835,4000,4168,4340,4515,4694,4876,5062,5251,5444,5640,5840,6043
};

Voice::Voice() {
  samplerMode = false;
  overdrive = false;
  soloMute = false;
  mute = false;
  volume = 2;
  octave = 1;
  recOctave = -1;
  bps = 2.0f;
  sampleIndex = (int32_t)kick1Length * 1000;
  envelopeIndex = 0;
  sampleHistoryIndex = 0;
  phaserOffset = 0;
  phaserDir = 0;
  whooshOffset = 0;
  chordStep = 0;
  pitchDur = 0;
  voiceNum = 0;
  note = 0;
  bend14 = 8192;
  toneNoteOn(&tone, 2);
  extBaseStep = 1000;
  isDelay = false;
  envelopeNum = 0;
  filtLp = 0;
  filtLp2 = 0;
  delayLp = 0;
  revDamp = 0;
  crushHold = 0;
  crushCount = 0;
  chorusPhase = 0;
  tremPhase = 0;
  shotSerial = 0;
  memset(shots, 0, sizeof(shots));
  SetEnvelopeLength(1);
  ResetEffects();
}

void Voice::ReleaseShots() {
  memset(shots, 0, sizeof(shots));
}

static int pcmStep(int rate, bool native, int octave, int recOctave) {
  int step;
  if (native) {
    int r = rate > 0 ? rate : kSampleRate;
    step = (int)(((int64_t)1000 * r) / kSampleRate);
  } else {
    int oct = (recOctave > -1) ? (recOctave + 1) : (octave + 1);
    step = oct * 500;
    if (rate > 0 && rate != kSampleRate) {
      step = (int)(((int64_t)step * rate) / kSampleRate);
    }
  }
  if (step < 1) {
    step = 1;
  }
  return step;
}

void Voice::StartShot(const void *data, int length, int step, bool eightBit, uint8_t kind, bool fromKit, int instrument) {
  if (!data || length < 2 || step < 1) {
    return;
  }
  int slot = -1;
  for (int i = 0; i < kShotCount; i++) {
    if (!shots[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < kShotCount; i++) {
      int64_t left = (int64_t)shots[i].index * (shots[slot].length > 0 ? shots[slot].length : 1);
      int64_t right = (int64_t)shots[slot].index * (shots[i].length > 0 ? shots[i].length : 1);
      if (left > right) {
        slot = i;
      }
    }
  }
  Shot &s = shots[slot];
  memset(&s, 0, sizeof(s));
  s.data = data;
  s.length = length;
  s.step = step;
  s.eightBit = eightBit ? 1 : 0;
  s.reverse = envelopeNum > 1 ? 1 : 0;
  s.fromKit = fromKit ? 1 : 0;
  s.kind = kind;
  s.note = (uint8_t)note;
  s.instrument = (int8_t)instrument;
  s.index = 0;
  s.active = 1;
  s.serial = ++shotSerial;
}

void Voice::ArmPcm(int instrument) {
  if (samplerMode) {
    return;
  }
  if (instrument == 0) {
    DrumHitView hit;
    if ((unsigned)note <= 11 && drumKitHit(note, &hit) && hit.data && hit.length > 1) {
      int rate = hit.rate > 0 ? hit.rate : kSampleRate;
      StartShot(hit.data, hit.length, pcmStep(rate, true, octave, recOctave), false, 1, true, 0);
      return;
    }
    if ((unsigned)note > 11) {
      return;
    }
    int rate = kDrumRates[note] > 0 ? kDrumRates[note] : kSampleRate;
    StartShot(kDrumWaves[note], kDrumLens[note], pcmStep(rate, true, octave, recOctave), true, 1, false, 0);
    return;
  }
  if (instrument == 1) {
    if ((unsigned)note > 11) {
      return;
    }
    StartShot(kSfxWaves[note], kSfxLens[note], pcmStep(kSfxRate, false, octave, recOctave), true, 2, false, 1);
    return;
  }
  if (instrument >= 12) {
    ExtSampleView view;
    if (!instrumentView(instrument, &view) || !view.data || view.length < 2 || !view.oneshot) {
      return;
    }
    int step = extBaseStep > 0 ? extBaseStep : 1000;
    StartShot(view.data, view.length, step, false, 3, false, instrument);
  }
}

static bool shotNewer(uint16_t a, uint16_t b) {
  return a != b && (uint16_t)(a - b) < 0x8000;
}

int Voice::MixShots() {
  int newest = -1;
  for (int i = 0; i < kShotCount; i++) {
    if (!shots[i].active) {
      continue;
    }
    if (newest < 0 || shotNewer(shots[i].serial, shots[newest].serial)) {
      newest = i;
    }
  }
  bool ownPitch = !samplerMode && (voiceNum <= 1 || voiceNum >= 12);
  int acc = 0;
  for (int i = 0; i < kShotCount; i++) {
    Shot &shot = shots[i];
    if (!shot.active || !shot.data || shot.length < 2) {
      shot.active = 0;
      continue;
    }
    if (shot.kind == 1 && shot.fromKit) {
      DrumHitView hit;
      if (!drumKitHit(shot.note, &hit) || hit.data != shot.data) {
        shot.active = 0;
        continue;
      }
    } else if (shot.kind == 3) {
      ExtSampleView view;
      if (!instrumentView(shot.instrument, &view) || view.data != shot.data) {
        shot.active = 0;
        continue;
      }
    }
    if (shot.index >= shot.length * 1000) {
      shot.active = 0;
      continue;
    }
    int pos = shot.index;
    int span = shot.length * 1000;
    if (shot.reverse) {
      pos = span - 1 - pos;
    }
    if (pos < 0) {
      pos = 0;
    } else if (pos >= span) {
      pos = span - 1;
    }
    int idx = pos / 1000;
    int frac = pos % 1000;
    int idx2 = idx + 1;
    if (idx2 >= shot.length) {
      idx2 = shot.length - 1;
    }
    int s0;
    int s1;
    if (shot.eightBit) {
      const uint8_t *p = static_cast<const uint8_t *>(shot.data);
      s0 = ((int)p[idx] - 128) << 8;
      s1 = ((int)p[idx2] - 128) << 8;
    } else {
      const int16_t *p = static_cast<const int16_t *>(shot.data);
      s0 = p[idx];
      s1 = p[idx2];
    }
    // frac is 0..999. The product fits MULL; divide keeps the read linear
    // between frames instead of a staircase. frac 0 is the raw frame.
    int sample = s0 + dspMul(s1 - s0, frac) / 1000;
    int step = shot.step > 0 ? shot.step : 1;
    if (bend14 != 8192) {
      step = scaleByBend(step, bend14);
    }
    shot.index += step;
    if (ownPitch && i == newest && pitchMult > 0 && pitchDur > 0) {
      pitchDur -= pitchMult;
      if (pitchMult == 1) {
        shot.index -= pitchDur / 5;
      } else if (pitchMult == 2) {
        shot.index += pitchDur / 5;
      }
    }
    if (shot.index < 0) {
      shot.index = 0;
    }
    sample = dspMul(sample, volume) / 3;
    acc += sample;
  }
  return dspClamp(acc, -28000, 28000);
}

int Voice::RenderSource() {
  int sample = 0;
  if (voiceNum >= 12) {
    ExtSampleView view;
    bool oneshot = instrumentView(voiceNum, &view) && view.oneshot != 0;
    if (!oneshot) {
      sample = ReadExt();
    }
  } else if (voiceNum > 1 || samplerMode) {
    sample = ReadWaveform();
  }
  sample += MixShots();
  return dspClamp(sample, -28000, 28000);
}

int Voice::OutputWith(int extra) {
  int sample = RenderSource() + extra;
  if (soloMute || mute) {
    return 0;
  }
  return Shape(sample);
}

int Voice::UpdateVoice() {
  return OutputWith(0);
}

int Voice::Shape(int sample) {
  if (phaserMult > 0) {
    if (phaserDir >= 0) {
      phaserOffset++;
      if (phaserOffset > 2000 * phaserMult) {
        phaserDir = -1;
      }
    } else {
      phaserOffset--;
      if (phaserOffset < 0) {
        phaserDir = 1;
      }
    }
    sample = (sample + GetHistorySample(phaserOffset)) / 2;
  }

  if (delayMult > 0 && fx.delayDiv == 0) {
    sample += GetHistorySample(delaySamples) / 5;
  }

  if (whooshMult > 0) {
    whooshOffset += (int)(whooshMult * bps / 2);
    if (whooshOffset > 15000) {
      whooshOffset = 0;
    }
    int whoosh = kWhooshSin[whooshOffset / 150] + 1;
    for (int i = 0; i < whoosh; i++) {
      sample += GetHistorySample(i + 1);
    }
    sample /= whoosh + 1;
  }

  if (!fx.filter && !fx.drive && !fx.crush && !fx.chorus && !fx.delayDiv && !fx.tremolo && !fx.reverb) {
    if (lowPassMult > 0) {
      int taps = 4 * lowPassMult;
      for (int i = 1; i < taps; i++) {
        sample += GetHistorySample(i);
      }
      sample /= taps;
    }

    UpdateHistory(sample);

    if (reverbMult > 0) {
      int rSample = 0;
      for (int i = 2; i < 7; i++) {
        rSample += GetHistorySample(i * 450 * reverbMult);
      }
      sample = rSample / 2;
    }

    if (overdrive) {
      sample = sample * 10 / 7;
      if (sample > 5000) {
        sample = 5000;
      } else if (sample < -5000) {
        sample = -5000;
      }
    }
    return sample;
  }

  return ApplyInserts(sample);
}

int Voice::FxDelayBack() const {
  static const int kDiv[4] = {8, 8, 4, 2};
  int div = kDiv[fx.delayDiv > 3 ? 0 : fx.delayDiv];
  float rate = bps;
  if (rate < 0.4f) {
    rate = 0.4f;
  } else if (rate > 6.0f) {
    rate = 6.0f;
  }
  int perBeat = (int)(44100.0f / rate);
  int n = perBeat / div;
  // Full-rate line. 1/32 and 1/16 fit at the stock tempos. 1/8 clamps
  // to the buffer instead of aliasing a longer tap.
  int limit = kHistoryLen - 8;
  if (n > limit) {
    n = limit;
  } else if (n < 2) {
    n = 2;
  }
  return n;
}

static int lfoSine(int phase) {
  int q = (phase >> 14) & 3;
  int a = phase & 16383;
  if (q & 1) {
    a = 16383 - a;
  }
  int s = dspMulQ(a, 32767 - dspMulQ(a, a, 14), 14);
  if (s > 32767) {
    s = 32767;
  }
  if (q >= 2) {
    s = -s;
  }
  return s;
}

static int clamp30k(int sample) {
  return dspClamp(sample, -30000, 30000);
}

int Voice::ApplyInserts(int sample) {
  if (fx.drive > 0) {
    // Smooth saturation. A hard knee at a fixed threshold steps the
    // waveform and sounds like crunch even when bitcrush is off.
    int gain = 256 + (int)fx.drive * 4;
    int x = dspMulQ(sample, gain, 8);
    int knee = 14000 - (int)fx.drive * 70;
    if (knee < 4000) {
      knee = 4000;
    }
    int ax = dspAbs(x);
    int y = (int)(((int64_t)ax * knee) / (knee + ax / 2));
    if (y > 24000) {
      y = 24000;
    }
    sample = x < 0 ? -y : y;
  }

  if (fx.filter == 1 || fx.filter == 2) {
    // Q10 state so a dark cutoff still moves in fractions of an LSB
    // instead of sticking and then jumping.
    int coef = 500 + (int)fx.cutoff * 180;
    if (coef > 22000) {
      coef = 22000;
    }
    int x = sample;
    if (fx.filter == 1 && fx.res > 0) {
      int low = filtLp2 >> 10;
      int band = x - low;
      x += dspMul(band, fx.res) / 600;
      x = clamp30k(x);
    }
    int target = x << 10;
    // Q10. The state times the coefficient does not fit MULL.
    filtLp += (int)(((int64_t)(target - filtLp) * coef) >> 15);
    int poleLimit = 30000 << 10;
    filtLp = dspClamp(filtLp, -poleLimit, poleLimit);
    if (fx.filter == 1) {
      int mid = filtLp >> 10;
      int target2 = mid << 10;
      filtLp2 += (int)(((int64_t)(target2 - filtLp2) * coef) >> 15);
      filtLp2 = dspClamp(filtLp2, -poleLimit, poleLimit);
      sample = filtLp2 >> 10;
    } else {
      sample = x - (filtLp >> 10);
    }
  } else if (lowPassMult > 0) {
    int taps = 4 * lowPassMult;
    for (int i = 1; i < taps; i++) {
      sample += GetHistorySample(i);
    }
    sample /= taps;
  }

  if (fx.crush > 0 && fx.crush <= 4) {
    // Named lo-fi. Everything else in this chain stays full rate.
    if (crushCount <= 0) {
      int shift = (int)fx.crush * 2;
      crushHold = (sample >> shift) << shift;
      crushCount = 1 << fx.crush;
    }
    sample = crushHold;
    crushCount--;
  }

  if (fx.chorus > 0) {
    // ~0.7 Hz. The old step (~130 Hz) modulated the delay like a buzz.
    chorusPhase += 1;
    if (chorusPhase >= 65536) {
      chorusPhase -= 65536;
    }
    int mod = lfoSine(chorusPhase);
    int delayQ8 = (880 << 8) + dspMulQ(mod, 160, 7);
    int back = delayQ8 >> 8;
    int frac = delayQ8 & 255;
    int wet = HistoryAt(back, frac);
    int mix = (int)fx.chorus;
    if (mix > 60) {
      mix = 60;
    }
    sample = sample * (100 - mix) / 100 + wet * mix / 100;
  }

  int echo = 0;
  if (fx.delayDiv > 0 && fx.delayDiv <= 3) {
    int raw = GetHistorySample(FxDelayBack());
    // Darken the repeat so feedback does not pile up into a bright fizz.
    delayLp = dspClamp(delayLp, -24000, 24000);
    echo = dspPole(&delayLp, raw, 9000);
  }

  if (fx.reverb > 0) {
    static const int kTaps[6] = {947, 1601, 2251, 3119, 4327, 5987};
    int wet = 0;
    for (int i = 0; i < 6; i++) {
      wet += GetHistorySample(kTaps[i]);
    }
    wet /= 6;
    revDamp = dspClamp(revDamp, -24000, 24000);
    dspPole(&revDamp, wet, 5000);
  }

  // The line stores the dry plus damped feedback, not the wet mix.
  int stored = sample;
  if (echo != 0 && fx.delayFb > 0) {
    stored += dspMul(echo, fx.delayFb) / 100;
  }
  if (fx.reverb > 0) {
    stored += dspMul(revDamp, fx.reverb) / 400;
  }
  stored = clamp30k(stored);
  UpdateHistory(stored);

  if (fx.delayDiv > 0 && fx.delayDiv <= 3) {
    int mix = fx.delayMix;
    if (mix > 100) {
      mix = 100;
    }
    sample = sample * (100 - mix) / 100 + echo * mix / 100;
  }

  if (fx.tremolo > 0) {
    // About 4 Hz, sine rather than a fast triangle.
    tremPhase += 6;
    if (tremPhase >= 65536) {
      tremPhase -= 65536;
    }
    int uni = (lfoSine(tremPhase) + 32768) >> 1;
    int dip = dspMul(32767 - uni, fx.tremolo) / 32767;
    int gain = 100 - dip * 3 / 4;
    if (gain < 20) {
      gain = 20;
    }
    sample = sample * gain / 100;
  }

  if (fx.reverb > 0) {
    sample += dspMul(revDamp, fx.reverb) / 100;
  } else if (reverbMult > 0) {
    int rSample = 0;
    for (int i = 2; i < 7; i++) {
      rSample += GetHistorySample(i * 450 * reverbMult);
    }
    sample = rSample / 2;
  }

  if (fx.drive == 0 && overdrive) {
    sample = sample * 10 / 7;
    if (sample > 5000) {
      sample = 5000;
    } else if (sample < -5000) {
      sample = -5000;
    }
  }
  return clamp30k(sample);
}

int Voice::ReadWaveform() {
  int vSel = voiceNum;
  int baseFreqLocal = baseFreq;

  if (samplerMode) {
    vSel = note;
    int oct = (recOctave > -1) ? (recOctave + 1) : (octave + 1);
    baseFreqLocal = oct * 500;
    if (vSel < 2) {
      return 0;
    }
  }

  if (vSel < 2 || vSel > 11) {
    return 0;
  }

  if (pitchMult > 0 && pitchDur > 0) {
    pitchDur -= pitchMult;
    if (pitchMult == 1) {
      baseFreqLocal -= pitchDur / 5;
    } else if (pitchMult == 2) {
      baseFreqLocal += pitchDur / 5;
    }
  }

  if (chordMult > 0) {
    chordStep++;
    const int step = 1500;
    if (chordMult == 1) {
      if (chordStep < step) {
      } else if (chordStep < step * 2) {
        baseFreqLocal = baseFreq_ch1;
      } else if (chordStep < step * 3) {
        baseFreqLocal = baseFreq_ch2;
      } else {
        chordStep = 0;
      }
    } else if (chordMult == 2) {
      if (chordStep < step) {
      } else if (chordStep < step * 2) {
        baseFreqLocal = baseFreq_ch3;
      } else if (chordStep < step * 3) {
        baseFreqLocal = baseFreq_ch4;
      } else {
        chordStep = 0;
      }
    }
  }

  if (bend14 != 8192) {
    baseFreqLocal = scaleByBend(baseFreqLocal, bend14);
  }
  int sample = toneSample(&tone, vSel, baseFreqLocal);

  // Pad, organ, flute, and bass keep a long body. The default fade is
  // short enough that those voices used to die like a piano key.
  int envCap = 50000;
  int envDiv = 500;
  if (vSel == 11) {
    envCap = 220000;
    envDiv = 2200;
  } else if (vSel == 6 || vSel == 9 || vSel == 10) {
    envCap = 140000;
    envDiv = 1400;
  }
  envelopeIndex += envelopeLength;
  if (envelopeIndex > envCap) {
    envelopeIndex = (envelopeNum == 3) ? 1 : envCap;
  }
  int eidx = envelopeIndex / envDiv;
  if (eidx > 100) {
    eidx = 100;
  } else if (eidx < 0) {
    eidx = 0;
  }
  int envNum = envelopeNum;
  if (envNum < 0 || envNum > 3) {
    envNum = 0;
  }
  sample = (sample * volume * kEnvelopes[envNum][eidx]) / 300;
  if (isDelay) {
    sample /= 3;
  }
  return sample;
}

int Voice::ReadPcmShot(const void *data, int length, int rate, bool native, bool eightBit) {
  if (!data || length < 2) {
    return 0;
  }
  sampleLen = length;
  int sample = 0;
  if (sampleIndex < sampleLen * 1000) {
    int idx = sampleIndex / 1000;
    if (envelopeNum > 1) {
      idx = sampleLen - idx - 1;
    }
    if (idx < 0) {
      idx = 0;
    } else if (idx >= sampleLen) {
      idx = sampleLen - 1;
    }
    if (eightBit) {
      sample = ((int)static_cast<const uint8_t *>(data)[idx] - 128) << 8;
    } else {
      sample = static_cast<const int16_t *>(data)[idx];
    }
    int step;
    if (native) {
      // Kit pieces stay at the recorded pitch. Octave still selects which
      // pad (the key), but it does not transpose the sample.
      int r = rate > 0 ? rate : kSampleRate;
      step = (int)(((int64_t)1000 * r) / kSampleRate);
    } else {
      int oct = (recOctave > -1) ? (recOctave + 1) : (octave + 1);
      step = oct * 500;
      if (rate > 0 && rate != kSampleRate) {
        step = (int)(((int64_t)step * rate) / kSampleRate);
      }
    }
    if (step < 1) {
      step = 1;
    }
    if (bend14 != 8192) {
      step = scaleByBend(step, bend14);
    }
    sampleIndex += step;
    if (pitchMult > 0 && pitchDur > 0) {
      pitchDur -= pitchMult;
      if (pitchMult == 1) {
        sampleIndex -= pitchDur / 5;
      } else if (pitchMult == 2) {
        sampleIndex += pitchDur / 5;
      }
    }
  }
  sample = sample * volume / 3;
  if (isDelay) {
    sample /= 3;
  }
  return sample;
}

int Voice::ReadOneShot(const uint8_t *const *tables, const int *lengths, const int *rates) {
  if ((unsigned)note > 11 || !tables || !lengths) {
    return 0;
  }
  int rate = (rates && rates[note] > 0) ? rates[note] : kSampleRate;
  return ReadPcmShot(tables[note], lengths[note], rate, false, true);
}

int Voice::ReadDrumWaveform() {
  DrumHitView hit;
  if (drumKitHit(note, &hit)) {
    return ReadPcmShot(hit.data, hit.length, hit.rate, true, false);
  }
  if ((unsigned)note > 11) {
    return 0;
  }
  int rate = kDrumRates[note] > 0 ? kDrumRates[note] : kSampleRate;
  return ReadPcmShot(kDrumWaves[note], kDrumLens[note], rate, true, true);
}

int Voice::ReadSfxWaveform() {
  static const int kRates[12] = {
    kSfxRate, kSfxRate, kSfxRate, kSfxRate, kSfxRate, kSfxRate,
    kSfxRate, kSfxRate, kSfxRate, kSfxRate, kSfxRate, kSfxRate
  };
  return ReadOneShot(kSfxWaves, kSfxLens, kRates);
}

int Voice::GetBaseFreq(int val, int ioctave) {
  if (val > 11) {
    val -= 12;
    ioctave++;
  }
  if (val < 0) {
    val += 12;
    ioctave--;
  }
  int idx = val + (ioctave * 12);
  if (idx < 0) {
    idx = 0;
  } else if (idx > 47) {
    idx = 47;
  }
  return kNoteFreq[idx];
}

void Voice::SetNote(int val, bool delay, int optOctave, int optInstrument) {
  sampleIndex = 0;
  pitchDur = 7000;
  envelopeIndex = 0;
  note = val;
  voiceNum = optInstrument;
  recOctave = optOctave;
  int oct = (optOctave == -1) ? octave : optOctave;
  baseFreq = GetBaseFreq(val, oct);
  baseFreq_ch1 = GetBaseFreq(val - 4, oct);
  baseFreq_ch2 = GetBaseFreq(val + 3, oct);
  baseFreq_ch3 = GetBaseFreq(val - 5, oct);
  baseFreq_ch4 = GetBaseFreq(val + 7, oct);
  isDelay = delay;
  extBaseStep = 1000;
  int synthId = optInstrument;
  if (samplerMode && val >= 2 && val <= 11) {
    synthId = val;
  }
  if (synthId >= 2 && synthId <= 11) {
    toneNoteOn(&tone, synthId);
  }
  if (optInstrument >= 12) {
    ExtSampleView view;
    if (instrumentView(optInstrument, &view) && view.length > 1) {
      int midi = synthToMidiNote(val, oct);
      int root = view.rootMidi > 0 ? view.rootMidi : 60;
      int semis = midi - root;
      if (semis > 36) {
        semis = 36;
      } else if (semis < -36) {
        semis = -36;
      }
      float ratio = powf(2.0f, (float)semis / 12.0f);
      int rate = view.rate > 0 ? view.rate : kSampleRate;
      int step = (int)(1000.0f * ((float)rate / (float)kSampleRate) * ratio + 0.5f);
      if (step < 1) {
        step = 1;
      }
      extBaseStep = step;
    }
  }
  ArmPcm(optInstrument);
}

int Voice::ReadExt() {
  ExtSampleView view;
  if (!instrumentView(voiceNum, &view) || !view.data || view.length < 1) {
    return 0;
  }
  int idx = sampleIndex / 1000;
  if (idx < 0) {
    idx = 0;
  }
  int loopA = 0;
  int loopB = view.length;
  bool oneshot = view.oneshot != 0;
  if (!oneshot && view.loopEnd > view.loopStart && view.loopEnd <= view.length) {
    loopA = view.loopStart;
    loopB = view.loopEnd;
  }
  int span = loopB - loopA;
  if (span < 1) {
    span = 1;
  }
  if (oneshot) {
    if (idx >= view.length) {
      return 0;
    }
  } else if (idx >= loopB) {
    idx = loopA + (idx - loopA) % span;
  } else if (idx < loopA) {
    idx = loopA;
  }
  if (idx < 0 || idx >= view.length) {
    return 0;
  }
  int sample = view.data[idx];
  int step = extBaseStep > 0 ? extBaseStep : 1000;
  if (bend14 != 8192) {
    step = scaleByBend(step, bend14);
  }
  sampleIndex += step;
  if (!oneshot && sampleIndex >= loopB * 1000) {
    int mod = span * 1000;
    int over = sampleIndex - loopA * 1000;
    if (mod > 0) {
      sampleIndex = loopA * 1000 + (over % mod);
    }
  }
  envelopeIndex += envelopeLength;
  if (envelopeIndex > 50000) {
    envelopeIndex = (envelopeNum == 3) ? 1 : 50000;
  }
  int env = envelopeNum;
  if (env > 3) {
    env = 0;
  }
  sample = (sample * volume * kEnvelopes[env][envelopeIndex / 500]) / 300;
  if (isDelay) {
    sample /= 3;
  }
  return sample;
}

void Voice::SetDelay(int val) {
  (void)val;
}

void Voice::SetVolume(int val) {
  switch (val) {
    case 0:
      mute = !mute;
      break;
    case 1:
      volume = (volume == 2) ? 3 : 2;
      break;
    case 2:
      overdrive = !overdrive;
      break;
  }
}

void Voice::SetOctave(int val) {
  octave = val;
}

void Voice::SetEnvelopeNum(int val) {
  envelopeNum = val;
}

void Voice::SetEnvelopeLength(int val) {
  envelopeLength = 4 - val;
}

void Voice::SetEffectNum(int val) {
  if (val == 0) {
    ResetEffects();
    return;
  }

  uint8_t *target = nullptr;
  switch (val) {
    case 1: target = &lowPassMult; break;
    case 2: target = &reverbMult; break;
    case 3: target = &phaserMult; break;
    case 4: target = &delayMult; break;
    case 5: target = &chordMult; break;
    case 6: target = &whooshMult; break;
    case 7: target = &pitchMult; break;
  }
  if (target) {
    (*target)++;
    if (*target > 2) {
      *target = 0;
    }
  }
  if (val == 4) {
    UpdateDelayOffset();
  }
}

void Voice::ResetEffects() {
  samplerMode = false;
  phaserMult = 0;
  delayMult = 0;
  reverbMult = 0;
  lowPassMult = 0;
  chordMult = 0;
  whooshMult = 0;
  pitchMult = 0;
  delaySamples = 0;
  fx = TrackFx();
  filtLp = 0;
  filtLp2 = 0;
  delayLp = 0;
  revDamp = 0;
  crushHold = 0;
  crushCount = 0;
  chorusPhase = 0;
  tremPhase = 0;
}

void Voice::CopyFx(const TrackFx &in) {
  fx = in;
  trackFxClamp(&fx);
  filtLp = 0;
  filtLp2 = 0;
  delayLp = 0;
  revDamp = 0;
  crushHold = 0;
  crushCount = 0;
}

void Voice::UpdateDelayOffset() {
  delaySamples = (delayMult > 0 && bps > 0.01f) ? (int)(delayMult * 11600.0f / bps) : 0;
}

void Voice::UpdateHistory(int sample) {
  sample = dspSat16(sample);
  sampleHistory[sampleHistoryIndex & kHistoryMask] = (int16_t)sample;
  sampleHistoryIndex = (uint16_t)((sampleHistoryIndex + 1) & kHistoryMask);
}

int Voice::HistoryAt(int back, int frac256) {
  if (back < 1) {
    back = 1;
  }
  if (back >= kHistoryLen - 1) {
    back = kHistoryLen - 2;
    frac256 = 0;
  }
  if (frac256 < 0) {
    frac256 = 0;
  } else if (frac256 > 255) {
    frac256 = 255;
  }
  int i0 = (sampleHistoryIndex - back) & kHistoryMask;
  int i1 = (i0 - 1) & kHistoryMask;
  return dspLerp8(sampleHistory[i0], sampleHistory[i1], frac256);
}

int Voice::GetHistorySample(int backOffset) {
  return HistoryAt(backOffset, 0);
}
