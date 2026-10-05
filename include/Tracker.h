#ifndef Tracker_h
#define Tracker_h
#include "Voice.h"
#include "MidiProtocol.h"
#include "SongFile.h"
#include "Snap.h"

class Tracker {
public:
  static const int kMaxSteps = 256;
  static const int kMaxCopy = 64;

  int lastNoteTrackIndex;
  uint8_t hintTime;
  char hint[15];
  uint8_t masterVolume;
  bool pressedOnce;
  bool isPlaying;
  int sample;
  uint8_t tempoBlink;
  int selectedTrack;
  bool allPatternPlay;
  int currentPattern;
  int patternLength;
  int trackIndex;
  Voice voices[4];
  int lastSamples[4];
  char oledInstString[20];
  int currentVoice;
  // Instrument assigned to each track. Selecting a track recalls this value.
  // It does not copy the previous track's instrument across.
  uint8_t trackVoice[4];
  uint8_t tracks[4][kMaxSteps];
  int8_t trackOctaves[4][kMaxSteps];
  uint8_t trackInstruments[4][kMaxSteps];
  bool solo;

  struct LoopPlay {
    uint8_t enabled;
    uint8_t pending;
    uint8_t quantize;
    const int16_t *pcm;
    int frames;
    int rate;
    int bpm;
    uint32_t phase;
    uint32_t inc;
    char library[24];
    char name[24];
  };
  LoopPlay loopPlay[4];
  LoopPlay audition;

  Tracker();
  int UpdateTracker();
  void SetCommand(char command, int val);
  void HandleMidi(const MidiEvent &event);
  bool PopMidiOut(MidiEvent *event);
  void CaptureSong(SongData *song) const;
  void ApplySong(const SongData &song);
  void SetHint(const char *msg);
  void SetHintF(const char *fmt, int val);
  void PlayPitch(int pitch, int octave);
  void NudgeBpm(int delta);
  void ArmLoop(int track, const LoopArm &arm);
  void StopLoop(int track);
  void StartAudition(const LoopArm &arm);
  void StopAudition();
  void WritePattern(int track, const uint8_t *steps, int count);
  void FillSnap(Snap *snap) const;
  // Starts a preloaded SD loop on this track, phased to the current bar.
  void TriggerLoopVoice(int track, int note);
  int Bpm() const { return bpms[bpmSlot]; }
  uint8_t BpmSlot() const { return bpmSlot; }

private:
  float bps;
  uint32_t samplesPerStep;
  uint32_t stepSampleCount;
  int barCount;
  uint8_t bpms[4];
  uint8_t bpmSlot;
  uint8_t ccMsb[32];
  uint8_t ccLsb[32];
  MidiEvent midiOutQ[4];
  uint8_t midiOutCount;
  uint8_t patternCopy[4][kMaxCopy];
  int8_t patternCopyOctaves[4][kMaxCopy];
  uint8_t patternCopyInstruments[4][kMaxCopy];

  void SoloTrack(bool repeat);
  void SetInstrument(int val);
  void RememberVoiceLabel(int val);
  void SyncTrackVoicesFromSteps();
  int InferTrackVoice(int track) const;
  void ArmTransport();
  void QueueMidi(MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value);
  void QueueBankMidi();
  void QueueVolumeMidi();
  void ApplyController(const MidiEvent &event);
  void SetNote(int val, int track);
  void SetEffect(int val);
  void SetBPM(int val);
  void SetEnvelopeNum(int val);
  void SetEnvelopeLength(int val);
  void SetOctave(int val);
  void SetVolume(int val);
  void SetTrackNum(int val);
  void ClearTrackNum(int val);
  void SetPatternNum(int val);
  void ClearPatternNum(int val);
  void TogglePlayStop();
  void CopyPattern();
  void PastePattern();
  void PastePatternAll();
  void ClearAll(int val);
  void RecomputeLoopInc(LoopPlay *loop);
  int ReadLoop(LoopPlay *loop);
  void LaunchPending(int localStep);
};

#endif
