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

// Like parseWavHeader, but the data chunk may extend past `len`. Used when
// only the header has been read and the frames will be streamed.
bool parseWavPrefix(const uint8_t *data, int len, WavInfo *out);

// Downmixes to signed 16-bit mono. Returns frames written, or -1.
int decodeWavMono(const uint8_t *data, int len, int16_t *dst, int dstFrames, WavInfo *out);

#endif
