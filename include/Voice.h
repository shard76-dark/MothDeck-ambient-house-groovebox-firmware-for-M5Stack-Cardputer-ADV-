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
  static const int kHistoryLen = 8192;
  static const int kHistoryIndexMask = 16383;

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

  int ReadWaveform();
  int ReadDrumWaveform();
  int ReadSfxWaveform();
  int ReadExt();
  int ReadOneShot(const int16_t *const *tables, const int *lengths, const int *rates);
  int ReadPcmShot(const int16_t *data, int length, int rate);

  ToneVoice tone;
  int GetBaseFreq(int val, int ioctave);
  int filtLp;
  int crushHold;
  int crushCount;
  int chorusPhase;
  int tremPhase;

  void UpdateHistory(int sample);
  int GetHistorySample(int backOffset);
  int ApplyInserts(int sample);
  int FxDelayBack() const;
};

#endif
