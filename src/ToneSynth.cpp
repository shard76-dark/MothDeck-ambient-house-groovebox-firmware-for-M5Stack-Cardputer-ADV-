#include "ToneSynth.h"
#include "DspHot.h"

// 512-entry sine. Filled once. The audio path only reads it.
static int16_t kSine[512];
static int kSineReady = 0;

static void ensureSine() {
  if (kSineReady) {
    return;
  }
  for (int i = 0; i < 512; i++) {
    // Bhaskara on a quarter wave, four quadrants. No libm in the hot path.
    int q = i >> 7;
    int a = i & 127;
    if (q & 1) {
      a = 127 - a;
    }
    int u = a * 256;
    int u3 = (int)(((int64_t)u * u / 32768) * u / 32768);
    int s = (int)((25736 * (int64_t)u - 10500 * (int64_t)u3) / 32768);
    if (s > 32767) {
      s = 32767;
    }
    if (q >= 2) {
      s = -s;
    }
    kSine[i] = (int16_t)s;
  }
  kSine[0] = 0;
  kSine[128] = 32767;
  kSine[256] = 0;
  kSine[384] = -32767;
  kSineReady = 1;
}

static int sineAt(uint32_t phase) {
  return kSine[(phase >> 23) & 511];
}

static int sawAt(uint32_t phase) {
  return (int)(phase >> 16) - 32768;
}

static int triAt(uint32_t phase) {
  uint32_t q = phase >> 16;
  if (q < 32768) {
    return -32768 + (int)(q * 2);
  }
  return 32767 - (int)((q - 32768) * 2);
}

static int squareAt(uint32_t phase) {
  return (phase & 0x80000000u) ? 22000 : -22000;
}


static int noiseAt(uint32_t *state) {
  *state = (*state * 1664525u) + 1013904223u;
  return (int)(*state >> 16) - 32768;
}

static uint32_t freqInc(int baseFreq) {
  if (baseFreq < 1) {
    baseFreq = 1;
  } else if (baseFreq > 20000) {
    baseFreq = 20000;
  }
  uint32_t inc = (uint32_t)(((uint64_t)baseFreq << 32) / 168000u);
  if (inc < 1) {
    inc = 1;
  }
  return inc;
}


void toneNoteOn(ToneVoice *voice, int id) {
  if (!voice) {
    return;
  }
  ensureSine();
  voice->phase = 0;
  voice->phaseB = 0;
  voice->phaseC = 0;
  voice->lfo = 0;
  voice->noise = 0x12345678u ^ (uint32_t)(id * 97);
  voice->lp = 0;
  voice->amp = 32767;
  voice->samples = 0;
  voice->id = (int8_t)id;
}

int toneSample(ToneVoice *voice, int id, int baseFreq) {
  if (!voice) {
    return 0;
  }
  if (!kSineReady || voice->id != (int8_t)id) {
    toneNoteOn(voice, id);
  }
  uint32_t inc = freqInc(baseFreq);
  int n = voice->samples;
  if (n < 2000000) {
    voice->samples = n + 1;
  }
  int y = 0;
  switch (id) {
    case 2: { // Sine. Clean, fast settle. The tracker envelope does the rest.
      y = sineAt(voice->phase);
      int gate = n < 60 ? (n * 32767 / 60) : 32767;
      y = dspMulQ(y, gate, 15);
      break;
    }
    case 3: { // Square, rounded so it is a hollow square and not a buzzer.
      y = dspPole(&voice->lp, squareAt(voice->phase), 6500);
      break;
    }
    case 4: { // Saw lead: two saws, a few cents apart, lowpass with the edge left in.
      uint32_t incB = inc * 10048u / 10000u;
      int a = sawAt(voice->phase);
      int b = sawAt(voice->phaseB);
      int raw = (a + b) / 2;
      int filtered = dspPole(&voice->lp, raw, 16000);
      y = filtered * 2 / 3 + raw / 4;
      voice->phaseB += incB;
      break;
    }
    case 5: { // Triangle. Mellow, almost no harmonics above the third.
      y = dspPole(&voice->lp, triAt(voice->phase) * 3 / 4, 20000);
      break;
    }
    case 6: { // Organ. Hammond-style drawbars 8' 4' 2 2/3' 2', plus a short click.
      int s1 = sineAt(voice->phase);
      int s2 = sineAt(voice->phase << 1);
      int s3 = sineAt(voice->phase * 3);
      int s4 = sineAt(voice->phase << 2);
      y = s1 / 2 + s2 / 4 + s3 / 6 + s4 / 8;
      if (n < 40) {
        y += noiseAt(&voice->noise) / (8 + n);
      }
      break;
    }
    case 7: { // Pluck. Bright filtered saw that closes and dies in a few hundred ms.
      int coef = 14000;
      if (n < 14000) {
        coef = 14000 - (int)((int64_t)n * 12000 / 14000);
      }
      y = dspPole(&voice->lp, sawAt(voice->phase), coef);
      if (n > 80) {
        voice->amp = dspMulQ(voice->amp, 32758, 15);
      }
      y = dspMulQ(y, voice->amp, 15);
      break;
    }
    case 8: { // Bell. FM, modulator near 3.5, index falls so the clang fades to a sine.
      uint32_t modPhase = voice->phase * 3u + (voice->phase >> 1);
      int mod = sineAt(modPhase);
      int index = 90000;
      if (n < 30000) {
        index = 90000 - (int)((int64_t)n * 80000 / 30000);
      } else {
        index = 10000;
      }
      uint32_t car = voice->phase + (uint32_t)(((int64_t)mod * index) >> 15);
      y = sineAt(car);
      if (n > 40) {
        voice->amp = dspMulQ(voice->amp, 32764, 15);
      }
      y = dspMulQ(y, voice->amp, 15);
      break;
    }
    case 9: { // Flute. Soft sine, a little second harmonic, breath, slow vibrato.
      voice->lfo += 90000u;
      int vib = sineAt(voice->lfo) / 80;
      uint32_t p = voice->phase + (uint32_t)vib;
      int body = sineAt(p);
      int harm = sineAt(p << 1) / 7;
      int breath = noiseAt(&voice->noise) / 18;
      y = body + harm + breath;
      int gate = n < 4000 ? (n * 32767 / 4000) : 32767;
      y = dspMulQ(y, gate, 15);
      break;
    }
    case 10: { // Bass. Sub sine an octave down plus two detuned saws, lowpassed.
      uint32_t incA = inc * 9952u / 10000u;
      uint32_t incB = inc * 10070u / 10000u;
      voice->lfo += inc >> 1;
      int sub = sineAt(voice->lfo);
      int a = sawAt(voice->phaseB);
      int b = sawAt(voice->phaseC);
      int mix = sub / 2 + a / 6 + b / 6;
      y = dspPole(&voice->lp, mix, 2600);
      voice->phaseB += incA;
      voice->phaseC += incB;
      break;
    }
    case 11: { // Pad. Slow attack, two detuned triangles, dark lowpass, held.
      uint32_t incA = inc * 9940u / 10000u;
      uint32_t incB = inc * 10080u / 10000u;
      int a = triAt(voice->phaseB);
      int b = triAt(voice->phaseC);
      int mix = a / 3 + b / 3 + sineAt(voice->phase) / 6;
      y = dspPole(&voice->lp, mix, 3200);
      int gate = n < 12000 ? dspMul(n, 30000) / 12000 : 30000;
      y = dspMulQ(y, gate, 15);
      voice->phaseB += incA;
      voice->phaseC += incB;
      break;
    }
    default:
      y = sineAt(voice->phase);
      break;
  }
  voice->phase += inc;
  return dspSat16(y);
}
