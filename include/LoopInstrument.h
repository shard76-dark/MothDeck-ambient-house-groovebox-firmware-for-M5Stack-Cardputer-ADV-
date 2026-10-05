#ifndef LoopInstrument_h
#define LoopInstrument_h
#include <stdint.h>

// A loop the Loops instrument may start from the audio task.
// hold > 0 streams from the card (see PcmHold). pcm is only set for a
// fully resident buffer, which the card path no longer uses.
struct LoopHit {
  const int16_t *pcm;
  int hold;
  int frames;
  int rate;
  int bpm;
  char library[24];
  char name[24];
};

bool loopInstrumentHit(int note, LoopHit *out);

#endif
