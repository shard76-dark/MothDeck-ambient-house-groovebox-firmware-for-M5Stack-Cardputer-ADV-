#ifndef PluginFormat_h
#define PluginFormat_h
#include <stdint.h>
#include "SongFile.h"

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

// Which assign fields the patch file actually set. A missing key does not
// overwrite the track.
static const uint32_t PATCH_VOLUME = 1u << 0;
static const uint32_t PATCH_OCTAVE = 1u << 1;
static const uint32_t PATCH_SAMPLER = 1u << 2;
static const uint32_t PATCH_OVERDRIVE = 1u << 3;
static const uint32_t PATCH_ENVELOPE = 1u << 4;
static const uint32_t PATCH_ENVLEN = 1u << 5;
static const uint32_t PATCH_LOWPASS = 1u << 6;
static const uint32_t PATCH_WHOOSH = 1u << 7;
static const uint32_t PATCH_WOBBLE = 1u << 8;
static const uint32_t PATCH_PITCH = 1u << 9;
static const uint32_t PATCH_ARP = 1u << 10;

static const uint32_t PATCH_FILTER = 1u << 0;
static const uint32_t PATCH_CUTOFF = 1u << 1;
static const uint32_t PATCH_RES = 1u << 2;
static const uint32_t PATCH_DELAY = 1u << 3;
static const uint32_t PATCH_FEEDBACK = 1u << 4;
static const uint32_t PATCH_MIX = 1u << 5;
static const uint32_t PATCH_REVERB = 1u << 6;
static const uint32_t PATCH_CRUSH = 1u << 7;
static const uint32_t PATCH_DRIVE = 1u << 8;
static const uint32_t PATCH_CHORUS = 1u << 9;
static const uint32_t PATCH_TREMOLO = 1u << 10;

static const uint32_t PATCH_SCALE = 1u << 0;
static const uint32_t PATCH_ROOT = 1u << 1;
static const uint32_t PATCH_OSC2 = 1u << 2;
static const uint32_t PATCH_COARSE = 1u << 3;
static const uint32_t PATCH_BLEND = 1u << 4;
static const uint32_t PATCH_GLIDE = 1u << 5;

enum PatchSource : uint8_t {
  PATCH_BUILTIN = 0,
  PATCH_SAMPLE = 1,
  PATCH_SUB = 2,
  PATCH_FM = 3,
  PATCH_WAVE = 4
};

// One parsed mothdeck-patch file. active is 0 when the folder is an older
// mothdeck-instrument manifest.
struct PatchAssign {
  uint8_t active;
  uint8_t instrument;
  uint8_t source;
  uint32_t voiceMask;
  uint32_t fxMask;
  uint32_t blockMask;
  SongVoice voice;
  TrackFx fx;
  TrackBlock block;
  InstrumentManifest audio;
  char name[32];
  char error[48];
};

bool patchManifestMagic(const char *text, int len);
bool parsePatchManifest(const char *text, int len, PatchAssign *out);

#endif
