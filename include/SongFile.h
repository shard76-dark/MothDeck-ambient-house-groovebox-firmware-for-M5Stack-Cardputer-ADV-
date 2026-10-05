#ifndef SongFile_h
#define SongFile_h
#include <stdint.h>

static const int kSongTracks = 4;
static const int kSongSteps = 256;
static const int kSongSlots = 4;
static const uint8_t kSongVersion = 1;
static const uint8_t kSongVersionV2 = 2;
static const int kPluginNameLen = 24;
static const int kSongPluginSlots = 8;
// magic + version + pattern + mixer/transport + 3 track grids + 4 voices + checksum
static const int kSongFileBytes = 3155;
static const int kSongV1Payload = 3153;
// v1 payload + pluginCount + 8 refs + 4 loop refs + checksum
static const int kSongFileBytesMax = 3556;

struct SongVoice {
  uint8_t volume;
  uint8_t mute;
  uint8_t samplerMode;
  uint8_t overdrive;
  int8_t octave;
  uint8_t envelopeNum;
  uint8_t envelopeLength;
  uint8_t phaserMult;
  uint8_t lowPassMult;
  uint8_t reverbMult;
  uint8_t chordMult;
  uint8_t pitchMult;
  uint8_t delayMult;
  uint8_t whooshMult;
  uint16_t bend14;
};

// A plugin folder name stored with the song. id is the value written into
// the instrument grid (12..63). Empty name means the reference was lost.
struct SongPluginRef {
  uint8_t id;
  char name[kPluginNameLen];
};

// One loop assignment per track. quantize: 0 now, 1 next beat, 2 next bar.
struct SongLoopRef {
  uint8_t enabled;
  uint8_t quantize;
  char library[kPluginNameLen];
  char name[kPluginNameLen];
};

struct SongData {
  uint16_t patternLength;
  uint8_t masterVolume;
  uint8_t bpmSlot;
  uint8_t bpms[4];
  uint8_t currentVoice;
  uint8_t selectedTrack;
  uint8_t currentPattern;
  uint8_t allPatternPlay;
  uint8_t tracks[kSongTracks][kSongSteps];
  int8_t octaves[kSongTracks][kSongSteps];
  uint8_t instruments[kSongTracks][kSongSteps];
  SongVoice voices[kSongTracks];
  uint8_t pluginCount;
  SongPluginRef plugins[kSongPluginSlots];
  SongLoopRef loops[kSongTracks];
};

// Writes version 1 (3155 bytes, MothOS compatible) when no plugin or loop
// reference is in use. Writes version 2 otherwise.
int songEncode(const SongData &song, uint8_t *dst, int dstLen);
bool songDecode(const uint8_t *src, int srcLen, SongData *song);
int songEncodedSize(const SongData &song);
bool songNeedsV2(const SongData &song);

#endif
