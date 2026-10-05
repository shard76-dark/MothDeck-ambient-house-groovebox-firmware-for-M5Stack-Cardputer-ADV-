#ifndef LoopInstrument_h
#define LoopInstrument_h
#include <stdint.h>

// A loop the Loops instrument may start from the audio task.
// PCM is already in memory. This call does not touch the SD card.
struct LoopHit {
  const int16_t *pcm;
  int frames;
  int rate;
  int bpm;
  char library[24];
  char name[24];
};

bool loopInstrumentHit(int note, LoopHit *out);

#endif
