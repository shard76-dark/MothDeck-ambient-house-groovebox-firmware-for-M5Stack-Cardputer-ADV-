#ifndef ToneSynth_h
#define ToneSynth_h
#include <stdint.h>

// Real-time voice for built-in instruments 2..11 (Sine through Pad).
// baseFreq is the tracker increment (1000 is about C4). Output is int16-scale
// and already includes the instrument's own attack, sustain, and decay.

struct ToneVoice {
  uint32_t phase;
  uint32_t phaseB;
  uint32_t phaseC;
  uint32_t lfo;
  uint32_t noise;
  int lp;
  int amp;
  int samples;
  int8_t id;
};

void toneNoteOn(ToneVoice *voice, int id);
int toneSample(ToneVoice *voice, int id, int baseFreq);

#endif
