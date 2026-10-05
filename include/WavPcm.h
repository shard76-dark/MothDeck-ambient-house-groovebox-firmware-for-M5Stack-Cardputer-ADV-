#ifndef WavPcm_h
#define WavPcm_h
#include <stdint.h>

// Minimal PCM WAV reader. Accepts 8- or 16-bit, mono or stereo.
// Compressed formats and odd sizes are rejected. No allocation.

struct WavInfo {
  int rate;
  int channels;
  int bits;
  int frames;
  int dataOffset;
  char error[40];
};

bool parseWavHeader(const uint8_t *data, int len, WavInfo *out);

// Downmixes to signed 16-bit mono. Returns frames written, or -1.
int decodeWavMono(const uint8_t *data, int len, int16_t *dst, int dstFrames, WavInfo *out);

#endif
