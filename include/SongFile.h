#ifndef SongFile_h
#define SongFile_h
#include <stdint.h>

static const int kSongTracks = 4;
static const int kSongSteps = 256;
static const int kSongSlots = 4;
static const uint8_t kSongVersion = 1;
static const uint8_t kSongVersionV2 = 2;
static const uint8_t kSongVersionV3 = 3;
static const int kPluginNameLen = 24;
static const int kSongPluginSlots = 8;
// magic + version + pattern + mixer/transport + 3 track grids + 4 voices + checksum
static const int kSongFileBytes = 3155;
static const int kSongV1Payload = 3153;
// Twelve bytes per track, after the version 2 tail. The last byte is reserved.
static const int kSongFxBytes = 12;
// v2 max is 3556. Version 3 adds 4 * 12 effect bytes: 3604.
static const int kSongFileBytesMax = 3604;

// Insert effect on one track. Zero means the insert is off.
struct TrackFx {
  uint8_t filter;    // 0 off, 1 low pass, 2 high pass
  uint8_t cutoff;    // 0..127
  uint8_t res;       // 0..80
  uint8_t delayDiv;  // 0 off, 1 = 1/32, 2 = 1/16, 3 = 1/8 (clamped to the history buffer)
  uint8_t delayFb;   // 0..70
  uint8_t delayMix;  // 0..100
  uint8_t reverb;    // 0..100
  uint8_t crush;     // 0..4
  uint8_t drive;     // 0..100
  uint8_t chorus;    // 0..100
  uint8_t tremolo;   // 0..100, mono amplitude (the mix is mono)
  uint8_t reserved;
};

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
  TrackFx fx[kSongTracks];
};

// Writes version 1 (3155 bytes, MothOS compatible) when no plugin, loop, or
// insert effect is in use. Version 2 adds plugins and loops. Version 3 adds
// the per-track insert block on top of version 2.
int songEncode(const SongData &song, uint8_t *dst, int dstLen);
bool songDecode(const uint8_t *src, int srcLen, SongData *song);
int songEncodedSize(const SongData &song);
bool songNeedsV2(const SongData &song);
bool songNeedsV3(const SongData &song);
bool trackFxActive(const TrackFx &fx);
void trackFxClamp(TrackFx *fx);

#endif
