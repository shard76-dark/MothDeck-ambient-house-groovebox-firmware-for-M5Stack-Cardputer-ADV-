#ifndef PluginFormat_h
#define PluginFormat_h
#include <stdint.h>

// Text manifest for /moth/instruments/<folder>/manifest.txt
// See docs/FORMATS.md. Parsed on device and by the host tests.

enum InstrumentKind : uint8_t {
  INST_SAMPLE = 0,
  INST_SUBTRACTIVE = 1,
  INST_FM = 2,
  INST_WAVETABLE = 3
};

enum InstWave : uint8_t {
  WAVE_SINE = 0,
  WAVE_SQUARE = 1,
  WAVE_SAW = 2,
  WAVE_TRIANGLE = 3
};

struct InstrumentManifest {
  char name[32];
  InstrumentKind kind;
  uint8_t rootMidi;
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
  uint8_t gain;
  uint8_t cutoff;
  uint8_t resonance;
  uint8_t fmRatio;
  uint8_t fmIndex;
  uint8_t wave;
  uint8_t oneshot;
  int32_t loopStart;
  int32_t loopEnd;
  int32_t sampleRate;
  char sampleFile[32];
  char error[48];
};

bool parseInstrumentManifest(const char *text, int len, InstrumentManifest *out);

// Folder names that are safe to join onto /moth/instruments/.
bool manifestNameSafe(const char *name);

#endif
