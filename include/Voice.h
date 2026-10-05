#ifndef Voice_h
#define Voice_h
#include <stdint.h>
#include "ToneSynth.h"
#include "SongFile.h"

class Voice {
public:
  bool samplerMode;
  bool overdrive;
  bool soloMute;
  bool mute;
  uint8_t volume;
  int8_t octave;
  int8_t recOctave;
  uint8_t phaserMult;
  uint8_t lowPassMult;
  uint8_t reverbMult;
  uint8_t chordMult;
  uint8_t pitchMult;
  uint8_t delayMult;
  uint8_t whooshMult;
  uint16_t bend14;
  float bps;
  TrackFx fx;

  Voice();
  int UpdateVoice();
  // Renders this voice plus an extra sample (a loop on the same track), then
  // the insert. Drums, sound effects, and one-shot samples keep their own
  // cursors so a new hit mixes with the ones still ringing.
  int OutputWith(int extra);
  void ReleaseShots();
  void SetNote(int val, bool delay, int optOctave, int optInstrument);
  void SetVolume(int val);
  void SetOctave(int val);
  void SetDelay(int val);
  void SetEnvelopeNum(int val);
  void SetEnvelopeLength(int val);
  void SetEffectNum(int val);
  void ResetEffects();
  void CopyFx(const TrackFx &in);
  void UpdateDelayOffset();
  uint8_t EnvelopeNum() const { return envelopeNum; }
  uint8_t EnvelopeLength() const { return envelopeLength; }
  void RestoreEnvelope(uint8_t num, uint8_t length) {
    envelopeNum = num;
    envelopeLength = length;
  }

private:
  // Full-rate delay line. 8192 samples is about 186 ms at 44100 Hz.
  // The previous line stepped two samples per write, which aliased every
  // delay, chorus, and reverb tap.
  static const int kHistoryLen = 8192;
  static const int kHistoryMask = 8191;
  static_assert((kHistoryLen & (kHistoryLen - 1)) == 0, "history length is a power of two");

  int16_t sampleHistory[kHistoryLen];
  int32_t sampleIndex;
  int envelopeIndex;
  int baseFreq;
  int baseFreq_ch1;
  int baseFreq_ch2;
  int baseFreq_ch3;
  int baseFreq_ch4;
  int extBaseStep;
  int pitchDur;
  int chordStep;
  int whooshOffset;
  int phaserOffset;
  int delaySamples;
  uint16_t sampleHistoryIndex;
  int sampleLen;
  int8_t phaserDir;
  uint8_t envelopeLength;
  uint8_t envelopeNum;
  int8_t voiceNum;
  int8_t note;
  bool isDelay;

  // One-shot PCM that overlaps on this track. The data pointer is the
  // sample that was current at the trigger; MixShots drops the shot if
  // that buffer is unloaded.
  struct Shot {
    const void *data;
    int32_t index;
    int length;
    int step;
    uint16_t serial;
    uint8_t active;
    uint8_t eightBit;
    uint8_t reverse;
    uint8_t fromKit;
    uint8_t kind;
    uint8_t note;
    int8_t instrument;
  };
  static const int kShotCount = 8;
  Shot shots[kShotCount];
  uint16_t shotSerial;

  int ReadWaveform();
  int ReadDrumWaveform();
  int ReadSfxWaveform();
  int ReadExt();
  int ReadOneShot(const uint8_t *const *tables, const int *lengths, const int *rates);
  int ReadPcmShot(const void *data, int length, int rate, bool native, bool eightBit);

  ToneVoice tone;
  int GetBaseFreq(int val, int ioctave);
  int filtLp;
  int filtLp2;
  int delayLp;
  int revDamp;
  int crushHold;
  int crushCount;
  int chorusPhase;
  int tremPhase;

  void UpdateHistory(int sample);
  int GetHistorySample(int backOffset);
  int HistoryAt(int back, int frac256);
  int ApplyInserts(int sample);
  int FxDelayBack() const;
  int RenderSource();
  int Shape(int sample);
  void ArmPcm(int instrument);
  void StartShot(const void *data, int length, int step, bool eightBit, uint8_t kind, bool fromKit, int instrument);
  int MixShots();
};

#endif
