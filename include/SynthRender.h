#ifndef SynthRender_h
#define SynthRender_h
#include <stdint.h>

// Renders one cycle into dst so the sampler can play a parameter instrument
// without a sample file. n is the cycle length (use 168 so 1:1 playback at
// 44100 Hz sits near C4). Output stays inside int16.

void renderSubtractive(int16_t *dst, int n, int wave, int cutoff, int resonance);
void renderFm(int16_t *dst, int n, int ratio, int indexAmount);

#endif
