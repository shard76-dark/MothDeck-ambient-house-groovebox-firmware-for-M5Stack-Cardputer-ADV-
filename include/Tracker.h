#ifndef Tracker_h
#define Tracker_h
#include "Voice.h"
#include "MidiProtocol.h"
#include "SongFile.h"
#include "Snap.h"
#include "PluginFormat.h"
#include "BoardConfig.h"

class Tracker {
public:
  static const int kMaxSteps = 256;
  static const int kStepsPerBar = 16;
  static const int kMaxBars = 8;
  static const int kMaxPatternSteps = kStepsPerBar * kMaxBars;

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
  bool solo;
  uint8_t NoteAt(int track, int step) const;
  int8_t OctaveAt(int track, int step) const;
  uint8_t InstAt(int track, int step) const;
  // 0 when the step is empty, otherwise 1–4 steps. Code 0 is one step.
  uint8_t NoteLenAt(int track, int step) const;
  int PatternSlots() const;
  int Bars() const;

  struct LoopPlay {
    uint8_t enabled;
    uint8_t pending;
    uint8_t quantize;
    const int16_t *pcm;
    int hold;
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
  // Set by MIDI Start/Continue. Steps then move on clock (6 clocks = one
  // 16th), and the internal sample counter does not also advance them.
  bool extSync;
  int clockCount;
  uint16_t lastClockTs;
  uint8_t haveClockTs;
  uint32_t tempoMs;
  int tempoClocks;
  // Clocks since the last position-zero (Start or Song Position 0). Used to
  // learn an external loop length. A Stop, Space, or a non-zero Song
  // Position abandons the measurement.
  int clocksSinceZero;
  bool blockLearn;
  bool autoLength;
  // Bar shown on the 16-step editor. Follows the playhead unless the user
  // pages with Fn+, and Fn+/.
  int editBar;
  bool followView;
  uint8_t bpms[4];
  uint8_t bpmSlot;
  uint8_t ccMsb[32];
  uint8_t ccLsb[32];
  MidiEvent midiOutQ[4];
  uint8_t midiOutCount;
  // One word per step: note in bits 0-3 (0 empty, 1-12 pitch), octave+8 in
  // bits 4-7 (-8..7), instrument in bits 8-13 (0-63), hold length in bits
  // 14-15 (0 = one step, then two, three, four). A one-step note leaves
  // those bits clear, so its song byte stays 1-12. 4*256*2 bytes replaces
  // the old 4*256*3 note/octave/instrument grids. The TTGO build keeps this
  // grid in PSRAM. The Cardputer build keeps it in BSS.
#if MOTHDECK_PSRAM_PATTERNS
  uint16_t (*steps)[kMaxSteps];
  uint16_t (*patternCopy)[kMaxPatternSteps];
#else
  uint16_t steps[4][kMaxSteps];
  uint16_t patternCopy[4][kMaxPatternSteps];
#endif
  int patternCopyLen;

  void SoloTrack(bool repeat);
  void SetInstrument(int val);
  void RememberVoiceLabel(int val);
  void SyncTrackVoicesFromSteps();
  int InferTrackVoice(int track) const;
  void ArmTransport();
  void QueueMidi(MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value);
  void AdvanceStep();
  void MidiStart();
  void MidiContinue();
  void MidiStop();
  void MidiClock(uint16_t timestamp13);
  void MidiSongPosition(int sixteenth);
  bool LearnLoopLength();
  void ApplyExternalBpm(int bpm);
  void SetBars(int bars);
  void NudgeEditBar(int dir);
  void ClampTransport();
  void SyncEditBar();
  uint16_t CellAt(int track, int step) const;
  void SetCell(int track, int step, uint8_t note, int8_t oct, uint8_t inst, uint8_t lenCode = 0);
  void ClearNote(int track, int step);
  static uint16_t PackStep(uint8_t note, int8_t oct, uint8_t inst, uint8_t lenCode = 0);
  static uint16_t EmptyStep();
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
  void AdjustFx(int packed);
  void ApplyPatch(const PatchAssign &patch);
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
