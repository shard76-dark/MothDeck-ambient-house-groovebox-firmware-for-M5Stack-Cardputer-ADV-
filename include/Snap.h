#ifndef Snap_h
#define Snap_h
#include <stdint.h>
#include "BoardConfig.h"

// Published by the audio task. The UI only reads a copy.
struct Snap {
  uint32_t seq;
  uint8_t playing;
  uint8_t armed;
  uint8_t track;
  uint8_t pattern;
  uint8_t songMode;
  uint8_t solo;
  uint16_t step;
  uint16_t patternLength;
  uint8_t bpm;
  uint8_t bpmSlot;
  uint8_t octave;
  uint8_t currentVoice;
  uint8_t trackVoice[4];
  uint8_t masterVolume;
  uint8_t envNum;
  uint8_t envLen;
  char inst[20];
  char hint[16];
  int16_t level[4];
  uint8_t vol[4];
  uint8_t mute[4];
  uint8_t drive[4];
  uint8_t sampler[4];
  uint8_t lp[4];
  uint8_t rev[4];
  uint8_t pha[4];
  uint8_t dly[4];
  uint8_t arp[4];
  uint8_t whoosh[4];
  uint8_t pitchFx[4];
  uint8_t notes[4][16];
  // Selected track, current pattern, one byte per step (at most 8 bars).
  // Bits 0-3 are the note (0 empty, 1-12), bits 4-5 the length code
  // (0 = one step), bits 6-7 the octave 0-3. The 16-step strip above is
  // the classic view. Two snap slots, so this is 256 bytes, not a buffer.
  uint8_t roll[128];
#if MOTHDECK_CHORDS
  // Up to three extra notes in the same step, packed the same way as roll[].
  // Empty is 0. The Cardputer snap does not carry this.
  uint8_t rollChord[128][3];
#endif
  uint8_t rollCount;
  uint8_t loopOn[4];
  uint16_t barOrigin;
  uint8_t fxFilter;
  uint8_t fxCutoff;
  uint8_t fxRes;
  uint8_t fxDelay;
  uint8_t fxFb;
  uint8_t fxMix;
  uint8_t fxRev;
  uint8_t fxCrush;
  uint8_t fxDrive;
  uint8_t fxChorus;
  uint8_t fxTrem;
  uint8_t scaleMode;
  uint8_t scaleRoot;
  uint8_t arpMode;
  uint8_t osc2Wave;
  int8_t osc2Coarse;
  uint8_t blend;
  uint16_t glideMs;
  uint8_t barIndex;
  uint8_t barCount;
  uint8_t patternSlots;
};

struct LoopArm {
  const int16_t *pcm;
  int hold;
  int frames;
  int rate;
  int bpm;
  uint8_t quantize;
  char library[24];
  char name[24];
};

#endif
