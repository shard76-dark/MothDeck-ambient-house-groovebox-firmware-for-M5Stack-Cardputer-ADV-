#include "Voice.h"
#include "MidiMap.h"
#include "DefaultSamples.h"
#include "InstrumentBank.h"
#include "DrumKit.h"
#include "BoardConfig.h"
#include <math.h>

// Keyboard order, not the old kick/snare/hat groups. See docs/FORMATS.md.
// C kick, C# rim, D snare, D# clap, E hat, F open hat, F# perc, G tom,
// G# shaker, A ride, A# snap, B crash. All tables are 22050 Hz.
static const int16_t *const kDrumWaves[12] = {
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

static const int16_t *const kSfxWaves[12] = {
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
  SetEnvelopeLength(1);
  ResetEffects();
}

int Voice::UpdateVoice() {
  if (soloMute || mute) {
    return 0;
  }

  int sample;
  if (voiceNum >= 12) {
    sample = ReadExt();
  } else if (voiceNum > 1 || samplerMode) {
    sample = ReadWaveform();
  } else if (voiceNum == 1) {
    sample = ReadSfxWaveform();
  } else {
    sample = ReadDrumWaveform();
  }

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

  if (delayMult > 0) {
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

int Voice::ReadPcmShot(const int16_t *data, int length, int rate) {
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
    sample = data[idx];
    int oct = (recOctave > -1) ? (recOctave + 1) : (octave + 1);
    int step = oct * 500;
    if (rate > 0 && rate != kSampleRate) {
      step = (int)(((int64_t)step * rate) / kSampleRate);
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

int Voice::ReadOneShot(const int16_t *const *tables, const int *lengths, const int *rates) {
  if ((unsigned)note > 11 || !tables || !lengths) {
    return 0;
  }
  int rate = (rates && rates[note] > 0) ? rates[note] : kSampleRate;
  return ReadPcmShot(tables[note], lengths[note], rate);
}

int Voice::ReadDrumWaveform() {
  DrumHitView hit;
  if (drumKitHit(note, &hit)) {
    return ReadPcmShot(hit.data, hit.length, hit.rate);
  }
  return ReadOneShot(kDrumWaves, kDrumLens, kDrumRates);
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
}

void Voice::UpdateDelayOffset() {
  delaySamples = (delayMult > 0 && bps > 0.01f) ? (int)(delayMult * 11600.0f / bps) : 0;
}

void Voice::UpdateHistory(int sample) {
  if (sample > 32767) {
    sample = 32767;
  } else if (sample < -32768) {
    sample = -32768;
  }
  sampleHistory[(sampleHistoryIndex >> 1) & (kHistoryLen - 1)] = (int16_t)sample;
  sampleHistoryIndex = (uint16_t)((sampleHistoryIndex + 1) & kHistoryIndexMask);
}

int Voice::GetHistorySample(int backOffset) {
  return sampleHistory[(((sampleHistoryIndex - backOffset) & kHistoryIndexMask) >> 1)];
}
