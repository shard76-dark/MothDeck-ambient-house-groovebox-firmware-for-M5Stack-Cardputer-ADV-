#include "Tracker.h"
#include "DspHot.h"
#include "PcmHold.h"
#include "Voice.h"
#include "MidiMap.h"
#include "BoardConfig.h"
#include "LoopInstrument.h"
#include <string.h>
#include <stdio.h>

__attribute__((weak)) bool loopInstrumentHit(int note, LoopHit *out) {
  (void)note;
  (void)out;
  return false;
}

Tracker::Tracker() {
  memset(ccMsb, 0, sizeof(ccMsb));
  memset(ccLsb, 0, sizeof(ccLsb));
  midiOutCount = 0;
  memset(loopPlay, 0, sizeof(loopPlay));
  memset(&audition, 0, sizeof(audition));
  bpmSlot = 0;
  extSync = false;
  clockCount = 0;
  lastClockTs = 0;
  haveClockTs = 0;
  tempoMs = 0;
  tempoClocks = 0;
  clocksSinceZero = 0;
  blockLearn = false;
  autoLength = true;
  editBar = 0;
  followView = true;
  patternCopyLen = 0;
  bpms[0] = 120;
  bpms[1] = 140;
  bpms[2] = 95;
  bpms[3] = 180;
  ClearAll(0);
  SetBPM(0);
}

uint16_t Tracker::PackStep(uint8_t note, int8_t oct, uint8_t inst) {
  if (note > 15) {
    note = 15;
  }
  int biased = (int)oct + 8;
  if (biased < 0) {
    biased = 0;
  } else if (biased > 15) {
    biased = 15;
  }
  if (inst > 63) {
    inst = 63;
  }
  return (uint16_t)((note & 0x0F) | ((biased & 0x0F) << 4) | ((inst & 0x3F) << 8));
}

uint16_t Tracker::EmptyStep() {
  return PackStep(0, 0, 0);
}

uint16_t Tracker::CellAt(int track, int step) const {
  if (track < 0 || track > 3 || step < 0 || step >= kMaxSteps) {
    return EmptyStep();
  }
  return steps[track][step];
}

void Tracker::SetCell(int track, int step, uint8_t note, int8_t oct, uint8_t inst) {
  if (track < 0 || track > 3 || step < 0 || step >= kMaxSteps) {
    return;
  }
  steps[track][step] = PackStep(note, oct, inst);
}

void Tracker::ClearNote(int track, int step) {
  uint16_t cell = CellAt(track, step);
  SetCell(track, step, 0, (int8_t)(((cell >> 4) & 0x0F) - 8), (uint8_t)((cell >> 8) & 0x3F));
}

uint8_t Tracker::NoteAt(int track, int step) const {
  return (uint8_t)(CellAt(track, step) & 0x0F);
}

int8_t Tracker::OctaveAt(int track, int step) const {
  return (int8_t)(((CellAt(track, step) >> 4) & 0x0F) - 8);
}

uint8_t Tracker::InstAt(int track, int step) const {
  return (uint8_t)((CellAt(track, step) >> 8) & 0x3F);
}

int Tracker::PatternSlots() const {
  int len = patternLength >= kStepsPerBar ? patternLength : kStepsPerBar;
  int slots = kMaxSteps / len;
  if (slots < 1) {
    slots = 1;
  } else if (slots > 4) {
    slots = 4;
  }
  return slots;
}

int Tracker::Bars() const {
  int bars = patternLength / kStepsPerBar;
  if (bars < 1) {
    return 1;
  }
  if (bars > kMaxBars) {
    return kMaxBars;
  }
  return bars;
}

static int barsFromSteps(int steps) {
  if (steps < 1) {
    return 1;
  }
  int bars = (steps + Tracker::kStepsPerBar - 1) / Tracker::kStepsPerBar;
  if (bars < 1) {
    bars = 1;
  } else if (bars > Tracker::kMaxBars) {
    bars = Tracker::kMaxBars;
  }
  return bars;
}

void Tracker::ClampTransport() {
  int slots = PatternSlots();
  if (currentPattern < 0 || currentPattern >= slots) {
    currentPattern = slots - 1;
    if (currentPattern < 0) {
      currentPattern = 0;
    }
  }
  int start = patternLength * currentPattern;
  int end = start + patternLength;
  if (start < 0) {
    start = 0;
  }
  if (end > kMaxSteps) {
    end = kMaxSteps;
  }
  if (trackIndex < start || trackIndex >= end) {
    trackIndex = start;
  }
  int bars = Bars();
  if (editBar < 0) {
    editBar = 0;
  } else if (editBar >= bars) {
    editBar = bars - 1;
  }
}

void Tracker::SetBars(int bars) {
  if (bars < 1) {
    bars = 1;
  } else if (bars > kMaxBars) {
    bars = kMaxBars;
  }
  patternLength = bars * kStepsPerBar;
  ClampTransport();
}

void Tracker::SyncEditBar() {
  if (!followView) {
    return;
  }
  int start = patternLength * currentPattern;
  int local = trackIndex - start;
  if (local < 0) {
    local = 0;
  }
  int bar = local / kStepsPerBar;
  int bars = Bars();
  if (bar >= bars) {
    bar = bars - 1;
  }
  if (bar < 0) {
    bar = 0;
  }
  editBar = bar;
}

void Tracker::NudgeEditBar(int dir) {
  followView = false;
  int bars = Bars();
  int bar = editBar + (dir < 0 ? -1 : 1);
  if (bar < 0) {
    bar = 0;
  } else if (bar >= bars) {
    bar = bars - 1;
  }
  editBar = bar;
  SetHintF("Bar %d", editBar + 1);
}

bool Tracker::LearnLoopLength() {
  int clocks = clocksSinceZero;
  clocksSinceZero = 0;
  if (blockLearn || !autoLength) {
    blockLearn = false;
    return false;
  }
  if (clocks < kStepsPerBar * 6) {
    return false;
  }
  int nearest = (clocks + 48) / 96;
  int err = clocks - nearest * 96;
  if (err < 0) {
    err = -err;
  }
  if (nearest < 1 || err > 12) {
    return false;
  }
  if (nearest > kMaxBars) {
    nearest = kMaxBars;
  }
  int before = patternLength;
  SetBars(nearest);
  return patternLength != before;
}

void Tracker::AdvanceStep() {
  barCount++;
  if (barCount > 3) {
    tempoBlink = 30;
    barCount = 0;
    if (!pressedOnce) {
      SetNote(7, 0);
    }
  }

  ClampTransport();
  int local = trackIndex - patternLength * currentPattern;
  if (local < 0) {
    local = 0;
  }
  LaunchPending(local);

  for (int i = 0; i < 4; i++) {
    int note = NoteAt(i, trackIndex);
    if (note > 0) {
      int inst = InstAt(i, trackIndex);
      if (inst == kLoopsVoice) {
        TriggerLoopVoice(i, note - 1);
      }
      voices[i].SetNote(note - 1, false, OctaveAt(i, trackIndex), inst);
    }
  }

  trackIndex++;
  int patternEnd = patternLength * (currentPattern + 1);
  if (trackIndex >= patternEnd || trackIndex >= kMaxSteps) {
    if (allPatternPlay) {
      currentPattern++;
      if (currentPattern >= PatternSlots()) {
        currentPattern = 0;
      }
    }
    trackIndex = patternLength * currentPattern;
  }
  SyncEditBar();
}

void Tracker::ApplyExternalBpm(int bpm) {
  if (bpm < 40) {
    bpm = 40;
  } else if (bpm > 240) {
    bpm = 240;
  }
  if (bpms[bpmSlot] == (uint8_t)bpm) {
    return;
  }
  bpms[bpmSlot] = (uint8_t)bpm;
  SetBPM(bpmSlot);
  SetHintF("BPM: %d", bpm);
}

void Tracker::MidiStart() {
  bool learned = LearnLoopLength();
  extSync = true;
  isPlaying = true;
  pressedOnce = true;
  clockCount = 0;
  stepSampleCount = 0;
  barCount = 0;
  followView = true;
  editBar = 0;
  ClampTransport();
  trackIndex = patternLength * currentPattern;
  AdvanceStep();
  if (learned) {
    SetHintF("Bars %d", Bars());
  } else {
    SetHint("MIDI Start");
  }
}

void Tracker::MidiContinue() {
  extSync = true;
  isPlaying = true;
  pressedOnce = true;
  clockCount = 0;
  SetHint("MIDI Cont");
}

void Tracker::MidiStop() {
  isPlaying = false;
  extSync = false;
  clockCount = 0;
  clocksSinceZero = 0;
  blockLearn = true;
  SetHint("MIDI Stop");
}

void Tracker::MidiClock(uint16_t timestamp13) {
  if (haveClockTs) {
    uint16_t delta = (uint16_t)((timestamp13 - lastClockTs) & 0x1FFF);
    if (delta > 0 && delta < 2000) {
      tempoMs += delta;
      tempoClocks++;
      if (tempoClocks >= 24) {
        int bpm = (int)(60000.0f / (float)tempoMs + 0.5f);
        ApplyExternalBpm(bpm);
        tempoMs = 0;
        tempoClocks = 0;
      }
    }
  }
  lastClockTs = timestamp13 & 0x1FFF;
  haveClockTs = 1;
  if (!extSync || !isPlaying) {
    return;
  }
  clocksSinceZero++;
  clockCount++;
  // 24 clocks per quarter note, and a pattern step is a 16th, so 6 clocks.
  if (clockCount >= 6) {
    clockCount = 0;
    stepSampleCount = 0;
    AdvanceStep();
  }
}

void Tracker::MidiSongPosition(int sixteenth) {
  if (sixteenth < 0) {
    sixteenth = 0;
  }
  bool learned = false;
  if (sixteenth == 0) {
    learned = LearnLoopLength();
  } else {
    clocksSinceZero = 0;
    blockLearn = true;
  }
  int span = patternLength > 0 ? patternLength : kStepsPerBar;
  int local = sixteenth % span;
  ClampTransport();
  trackIndex = currentPattern * span + local;
  if (trackIndex >= kMaxSteps) {
    trackIndex = currentPattern * span;
  }
  clockCount = 0;
  stepSampleCount = 0;
  SyncEditBar();
  if (learned) {
    SetHintF("Bars %d", Bars());
  } else {
    SetHint("MIDI Pos");
  }
}

int Tracker::UpdateTracker() {
  tempoBlink = 0;

  if (extSync) {
    // Keep loop phase moving inside the current step, but do not start the
    // next step from the sample counter. The clock does that.
    if (samplesPerStep > 1 && stepSampleCount + 1 < samplesPerStep) {
      stepSampleCount++;
    }
  } else if (++stepSampleCount >= samplesPerStep) {
    stepSampleCount = 0;
    AdvanceStep();
  }

  const int div = 2 + masterVolume * 5;
  int mix = 0;
  for (int i = 0; i < 4; i++) {
    int extra = 0;
    if (loopPlay[i].enabled && loopPlay[i].frames > 1 && (loopPlay[i].pcm || loopPlay[i].hold > 0)) {
      extra = ReadLoop(&loopPlay[i]);
    }
    // The loop used to replace the voice, so a sample cut the drums (and
    // the insert never saw the loop). Both go through the track FX.
    int samp = voices[i].OutputWith(extra) / div;
    lastSamples[i] = samp;
    mix += samp;
  }
  if (audition.enabled && audition.frames > 1 && (audition.pcm || audition.hold > 0)) {
    mix += ReadLoop(&audition) / (div + 2);
  }
  mix = dspSat16(mix);
  sample = mix;
  return mix;
}

void Tracker::SetHint(const char *msg) {
  hintTime = 120;
  strncpy(hint, msg, 14);
  hint[14] = '\0';
}

void Tracker::SetHintF(const char *fmt, int val) {
  char buf[15];
  snprintf(buf, sizeof(buf), fmt, val);
  SetHint(buf);
}

void Tracker::SetCommand(char command, int val) {
  switch (command) {
    case 'T':
      SetTrackNum(val);
      SetHintF("Track: %d", val + 1);
      break;
    case 'B':
      SetBPM(val);
      SetHintF("BPM: %d", bpms[val]);
      break;
    case 'N':
      ArmTransport();
      SetNote(val, selectedTrack);
      QueueMidi(MIDI_MSG_NOTE_ON, (uint8_t)selectedTrack, (uint8_t)synthToMidiNote(val, voices[selectedTrack].octave), 100);
      break;
    case 'O':
      SetOctave(val);
      SetHintF("Octave: %d", val);
      break;
    case 'L':
      SetEnvelopeLength(val);
      SetHintF("Note Len: %d", val + 1);
      break;
    case 'E':
      SetEnvelopeNum(val);
      switch (val) {
        case 0: SetHint("Fade Out"); break;
        case 1: SetHint("Fade In"); break;
        case 2: SetHint("No Fade"); break;
        case 3: SetHint("Loop"); break;
      }
      break;
    case 'V':
      if (val == 3) {
        SoloTrack(false);
      } else {
        SetVolume(val);
        if (val == 2) {
          SetHint(voices[selectedTrack].overdrive ? "ODrv On" : "ODrv Off");
        } else if (val == 0) {
          SetHint(voices[selectedTrack].mute ? "Mute" : "Unmute");
        } else {
          SetHintF("Volume: %d", voices[selectedTrack].volume);
        }
      }
      break;
    case 'D':
      SetEffect(val + 4);
      switch (val) {
        case 0: SetHintF("Echo: %d", voices[selectedTrack].delayMult); break;
        case 1: SetHintF("ArpChord: %d", voices[selectedTrack].chordMult); break;
        case 2: SetHintF("Whoosh: %d", voices[selectedTrack].whooshMult); break;
        case 3: SetHintF("Pitchbend: %d", voices[selectedTrack].pitchMult); break;
      }
      break;
    case 'A':
      SetEffect(val);
      switch (val) {
        case 0: SetHint("Effects Off"); break;
        case 1: SetHintF("Low Pass: %d", voices[selectedTrack].lowPassMult); break;
        case 2: SetHintF("Retrig: %d", voices[selectedTrack].reverbMult); break;
        case 3: SetHintF("Wobble: %d", voices[selectedTrack].phaserMult); break;
      }
      break;
    case '^':
      ClearTrackNum(val);
      SetHintF("Clr Track: %d", val + 1);
      break;
    case '$':
      SetPatternNum(val);
      SetHintF("Pattern: %d", currentPattern + 1);
      break;
    case '#':
      ClearPatternNum(val);
      SetHintF("Clr Pattern: %d", val + 1);
      break;
    case 'X':
      ClearAll(val);
      SetHintF("Bars %d", Bars());
      break;
    case 'Y':
      SetBars(val);
      SetHintF("Bars %d", Bars());
      break;
    case 'W':
      NudgeEditBar(val);
      break;
    case 'P':
      TogglePlayStop();
      SetHint(isPlaying ? "Rec On" : "Rec Off");
      break;
    case 'I':
      SetInstrument(val);
      QueueBankMidi();
      if (val == kLoopsVoice) {
        SetHint("Loops");
      } else if (val > 1) {
        SetHintF("Instrument: %d", val);
      } else if (val == 1) {
        SetHint("SFX Bank");
      } else {
        SetHint("Drum Bank");
      }
      break;
    case 'H':
      masterVolume = masterVolume ? 0 : 1;
      SetHintF("Mstr Volume: %d", 2 - masterVolume);
      break;
    case 'C':
      allPatternPlay = !allPatternPlay;
      SetHint(allPatternPlay ? "Song Mode" : "Pattern Mode");
      break;
    case 'v':
      if (val < 0) {
        val = 0;
      } else if (val > 8) {
        val = 8;
      }
      voices[selectedTrack].volume = (uint8_t)val;
      QueueVolumeMidi();
      SetHintF("Volume: %d", val);
      break;
    case 'f':
      AdjustFx(val);
      break;
    case '_':
      ClearNote(selectedTrack, trackIndex);
      SetHint("Step clr");
      break;
    case 'b':
      NudgeBpm(val);
      break;
    case '*':
      if (val == 0) {
        SetHint("Copy Pattern");
        CopyPattern();
      } else if (val == 1) {
        SetHint("Paste Pattern");
        PastePattern();
      } else if (val == 2) {
        SetHint("Paste All Patt");
        PastePatternAll();
      } else if (val == 3) {
        voices[selectedTrack].samplerMode = !voices[selectedTrack].samplerMode;
        voices[selectedTrack].SetEnvelopeNum(2);
        SetHintF("Samp Mode: %d", voices[selectedTrack].samplerMode);
      }
      break;
  }
}

static int clampStep(int cur, int dir, int step, int hi) {
  int v = cur + dir * step;
  if (v < 0) {
    v = 0;
  } else if (v > hi) {
    v = hi;
  }
  return v;
}

void Tracker::AdjustFx(int packed) {
  int row = packed & 0xFF;
  int dir = (int)(int8_t)((packed >> 8) & 0xFF);
  if (dir > 0) {
    dir = 1;
  } else if (dir < 0) {
    dir = -1;
  } else {
    dir = 1;
  }
  TrackFx &fx = voices[selectedTrack].fx;
  switch (row) {
    case 0: {
      int mode = (int)fx.filter + dir;
      if (mode > 2) {
        mode = 0;
      } else if (mode < 0) {
        mode = 2;
      }
      fx.filter = (uint8_t)mode;
      SetHint(mode == 1 ? "Low pass" : (mode == 2 ? "High pass" : "Filter off"));
      break;
    }
    case 1:
      fx.cutoff = (uint8_t)clampStep(fx.cutoff, dir, 8, 127);
      SetHintF("Cutoff: %d", fx.cutoff);
      break;
    case 2:
      fx.res = (uint8_t)clampStep(fx.res, dir, 8, 80);
      SetHintF("Res: %d", fx.res);
      break;
    case 3: {
      int div = (int)fx.delayDiv + dir;
      if (div > 3) {
        div = 0;
      } else if (div < 0) {
        div = 3;
      }
      fx.delayDiv = (uint8_t)div;
      SetHint(div == 1 ? "Delay 1/32" : (div == 2 ? "Delay 1/16" : (div == 3 ? "Delay 1/8" : "Delay off")));
      break;
    }
    case 4:
      fx.delayFb = (uint8_t)clampStep(fx.delayFb, dir, 10, 70);
      SetHintF("Feedback: %d", fx.delayFb);
      break;
    case 5:
      fx.delayMix = (uint8_t)clampStep(fx.delayMix, dir, 10, 100);
      SetHintF("Dly mix: %d", fx.delayMix);
      break;
    case 6:
      fx.reverb = (uint8_t)clampStep(fx.reverb, dir, 10, 100);
      SetHintF("Reverb: %d", fx.reverb);
      break;
    case 7:
      fx.crush = (uint8_t)clampStep(fx.crush, dir, 1, 4);
      SetHintF("Crush: %d", fx.crush);
      break;
    case 8:
      fx.drive = (uint8_t)clampStep(fx.drive, dir, 10, 100);
      SetHintF("Drive: %d", fx.drive);
      break;
    case 9:
      fx.chorus = (uint8_t)clampStep(fx.chorus, dir, 10, 100);
      SetHintF("Chorus: %d", fx.chorus);
      break;
    case 10:
      fx.tremolo = (uint8_t)clampStep(fx.tremolo, dir, 10, 100);
      SetHintF("Tremolo: %d", fx.tremolo);
      break;
    default:
      break;
  }
}

void Tracker::SoloTrack(bool repeat) {
  if (!repeat) {
    solo = !solo;
  }
  for (int i = 0; i < 4; i++) {
    voices[i].soloMute = solo && (i != selectedTrack);
  }
  SetHint(solo ? "Solo On" : "Solo Off");
}

void Tracker::SetEffect(int val) {
  voices[selectedTrack].SetEffectNum(val);
}

void Tracker::SetBPM(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 3) {
    val = 3;
  }
  bpmSlot = (uint8_t)val;
  bps = bpms[bpmSlot] / 60.0f;
  if (bps < 0.01f) {
    bps = 0.01f;
  }
  samplesPerStep = (uint32_t)(11025.0f / bps + 0.5f);
  if (samplesPerStep < 1) {
    samplesPerStep = 1;
  }
  for (int i = 0; i < 4; i++) {
    voices[i].bps = bps;
    voices[i].UpdateDelayOffset();
  }
}

void Tracker::SetEnvelopeNum(int val) {
  voices[selectedTrack].SetEnvelopeNum(val);
}

void Tracker::SetEnvelopeLength(int val) {
  voices[selectedTrack].SetEnvelopeLength(val);
}

void Tracker::SetOctave(int val) {
  voices[selectedTrack].SetOctave(val);
}

void Tracker::SetVolume(int val) {
  voices[selectedTrack].SetVolume(val);
  if (val == 1) {
    QueueVolumeMidi();
  }
}

void Tracker::SetNote(int val, int track) {
  if (track < 0 || track > 3) {
    return;
  }
  int inst = trackVoice[track];
  if (isPlaying && pressedOnce) {
    uint8_t note = 0;
    if (val >= 0) {
      note = (uint8_t)(val + 1);
    }
    SetCell(track, trackIndex, note, voices[selectedTrack].octave, (uint8_t)inst);
    int span = patternLength > 0 ? patternLength : 1;
    lastNoteTrackIndex = trackIndex % span;
  } else {
    voices[track].SetNote(val, false, -1, inst);
    if (inst == kLoopsVoice) {
      TriggerLoopVoice(track, val);
    }
  }
}

void Tracker::SetTrackNum(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 3) {
    val = 3;
  }
  selectedTrack = val;
  SoloTrack(true);
  RememberVoiceLabel(trackVoice[selectedTrack]);
}

void Tracker::ClearTrackNum(int val) {
  if (val < 0 || val > 3) {
    return;
  }
  int start = patternLength * currentPattern;
  int end = start + patternLength;
  if (end > kMaxSteps) {
    end = kMaxSteps;
  }
  for (int i = start; i < end; i++) {
    ClearNote(val, i);
  }
}

void Tracker::SetPatternNum(int val) {
  int slots = PatternSlots();
  if (val < 0) {
    val = 0;
  } else if (val >= slots) {
    val = slots - 1;
  }
  int local = trackIndex - (patternLength * currentPattern);
  if (local < 0) {
    local = 0;
  } else if (local >= patternLength) {
    local = 0;
  }
  currentPattern = val;
  trackIndex = patternLength * currentPattern + local;
  if (trackIndex >= kMaxSteps) {
    trackIndex = patternLength * currentPattern;
  }
  SyncEditBar();
}

void Tracker::ClearPatternNum(int val) {
  (void)val;
  int start = patternLength * currentPattern;
  int end = start + patternLength;
  if (end > kMaxSteps) {
    end = kMaxSteps;
  }
  for (int j = 0; j < 4; j++) {
    for (int i = start; i < end; i++) {
      ClearNote(j, i);
    }
  }
}

void Tracker::TogglePlayStop() {
  // The keyboard transport is the internal clock. MIDI Start turns
  // external sync back on. Drop any half-measured external loop so the
  // next Start does not resize the pattern.
  extSync = false;
  clocksSinceZero = 0;
  blockLearn = true;
  isPlaying = !isPlaying;
  if (isPlaying) {
    followView = true;
    SyncEditBar();
  }
}

void Tracker::CopyPattern() {
  int start = patternLength * currentPattern;
  int n = patternLength;
  if (n > kMaxPatternSteps) {
    n = kMaxPatternSteps;
  }
  if (start < 0) {
    start = 0;
  }
  patternCopyLen = n;
  uint16_t empty = EmptyStep();
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < kMaxPatternSteps; i++) {
      if (i < n && start + i < kMaxSteps) {
        patternCopy[j][i] = steps[j][start + i];
      } else {
        patternCopy[j][i] = empty;
      }
    }
  }
}

void Tracker::PastePattern() {
  int start = patternLength * currentPattern;
  int n = patternCopyLen > 0 ? patternCopyLen : patternLength;
  if (n > patternLength) {
    n = patternLength;
  }
  if (start < 0) {
    return;
  }
  uint16_t empty = EmptyStep();
  int limit = patternCopyLen > 0 ? n : patternLength;
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < limit; i++) {
      int idx = start + i;
      if (idx < 0 || idx >= kMaxSteps) {
        break;
      }
      steps[j][idx] = patternCopyLen > 0 ? patternCopy[j][i] : empty;
    }
  }
  SyncTrackVoicesFromSteps();
}

void Tracker::PastePatternAll() {
  int n = patternCopyLen > 0 ? patternCopyLen : patternLength;
  if (n > patternLength) {
    n = patternLength;
  }
  uint16_t empty = EmptyStep();
  int limit = patternCopyLen > 0 ? n : patternLength;
  int slots = PatternSlots();
  for (int r = 0; r < slots; r++) {
    int start = patternLength * r;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < limit; i++) {
        int idx = start + i;
        if (idx < 0 || idx >= kMaxSteps) {
          break;
        }
        steps[j][idx] = patternCopyLen > 0 ? patternCopy[j][i] : empty;
      }
    }
  }
  SyncTrackVoicesFromSteps();
}

void Tracker::ClearAll(int val) {
  selectedTrack = 0;
  currentPattern = 0;
  isPlaying = true;
  extSync = false;
  clockCount = 0;
  pressedOnce = false;
  allPatternPlay = false;
  currentVoice = 0;
  trackIndex = 0;
  stepSampleCount = 0;
  barCount = 0;
  memset(trackVoice, 0, sizeof(trackVoice));
  RememberVoiceLabel(0);
  for (int j = 0; j < 4; j++) {
    voices[j].ResetEffects();
    voices[j].SetEnvelopeNum(0);
    voices[j].volume = 2;
    voices[j].SetOctave(1);
    voices[j].bend14 = 8192;
    voices[j].ReleaseShots();
  }
  int bars = val + 1;
  if (bars < 1) {
    bars = 1;
  } else if (bars > kMaxBars) {
    bars = kMaxBars;
  }
  patternLength = bars * kStepsPerBar;
  editBar = 0;
  followView = true;
  clocksSinceZero = 0;
  blockLearn = true;
  autoLength = true;
  patternCopyLen = 0;
  uint16_t empty = EmptyStep();
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < kMaxSteps; s++) {
      steps[t][s] = empty;
    }
    for (int s = 0; s < kMaxPatternSteps; s++) {
      patternCopy[t][s] = empty;
    }
  }
  memset(loopPlay, 0, sizeof(loopPlay));
  memset(&audition, 0, sizeof(audition));
}

void Tracker::RememberVoiceLabel(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 63) {
    val = 63;
  }
  currentVoice = val;
  memset(oledInstString, 0, sizeof(oledInstString));
  if (val == kLoopsVoice) {
    memcpy(oledInstString, "LOOPS", 6);
  } else if (val > 11) {
    snprintf(oledInstString, sizeof(oledInstString), "PLG%d", val);
  } else if (val > 1) {
    snprintf(oledInstString, sizeof(oledInstString), "INS%d", val);
  } else if (val == 1) {
    memcpy(oledInstString, "SFX", 4);
  } else {
    memcpy(oledInstString, "DRUM", 5);
  }
}

int Tracker::InferTrackVoice(int track) const {
  if (track < 0 || track > 3) {
    return 0;
  }
  uint8_t first = InstAt(track, 0);
  bool same = true;
  for (int s = 1; s < kMaxSteps; s++) {
    if (InstAt(track, s) != first) {
      same = false;
      break;
    }
  }
  if (same) {
    return first;
  }
  for (int s = 0; s < kMaxSteps; s++) {
    if (NoteAt(track, s) > 0) {
      return InstAt(track, s);
    }
  }
  return 0;
}

void Tracker::SyncTrackVoicesFromSteps() {
  for (int t = 0; t < 4; t++) {
    trackVoice[t] = (uint8_t)InferTrackVoice(t);
  }
  if (selectedTrack < 0 || selectedTrack > 3) {
    selectedTrack = 0;
  }
  RememberVoiceLabel(trackVoice[selectedTrack]);
}

void Tracker::SetInstrument(int val) {
  if (val < 0) {
    val = 0;
  } else if (val > 63) {
    val = 63;
  }
  if (selectedTrack < 0 || selectedTrack > 3) {
    selectedTrack = 0;
  }
  trackVoice[selectedTrack] = (uint8_t)val;
  for (int i = 0; i < kMaxSteps; i++) {
    uint16_t cell = steps[selectedTrack][i];
    SetCell(selectedTrack, i, (uint8_t)(cell & 0x0F), (int8_t)(((cell >> 4) & 0x0F) - 8), (uint8_t)val);
  }
  if (val != kLoopsVoice) {
    StopLoop(selectedTrack);
  }
  RememberVoiceLabel(val);
}

void Tracker::ArmTransport() {
  if (!pressedOnce) {
    stepSampleCount = 0;
    trackIndex = 0;
    barCount = 0;
    tempoBlink = 30;
  }
  pressedOnce = true;
}

void Tracker::QueueMidi(MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value) {
  if (midiOutCount >= 4) {
    return;
  }
  MidiEvent &slot = midiOutQ[midiOutCount++];
  slot.type = type;
  slot.channel = (uint8_t)(channel & 0x0F);
  slot.number = number;
  slot.value = value;
  slot.value14 = 0;
}

void Tracker::QueueBankMidi() {
  int inst = currentVoice;
  if (inst < 0) {
    inst = 0;
  } else if (inst > 63) {
    inst = 11;
  }
  QueueMidi(MIDI_MSG_MSB, (uint8_t)selectedTrack, 0, (uint8_t)inst);
  QueueMidi(MIDI_MSG_LSB, (uint8_t)selectedTrack, 0, 0);
}

void Tracker::QueueVolumeMidi() {
  int packed = volumeTo14(voices[selectedTrack].volume);
  QueueMidi(MIDI_MSG_MSB, (uint8_t)selectedTrack, 7, (uint8_t)(packed >> 7));
  QueueMidi(MIDI_MSG_LSB, (uint8_t)selectedTrack, 7, (uint8_t)(packed & 0x7F));
}

bool Tracker::PopMidiOut(MidiEvent *event) {
  if (!event || midiOutCount == 0) {
    return false;
  }
  *event = midiOutQ[0];
  for (int i = 1; i < midiOutCount; i++) {
    midiOutQ[i - 1] = midiOutQ[i];
  }
  midiOutCount--;
  return true;
}

void Tracker::ApplyController(const MidiEvent &event) {
  if (event.number == kMidiPitchCc) {
    voices[selectedTrack].bend14 = event.value14;
    SetHint("Pitch Bend");
    return;
  }
  if (event.number >= 32) {
    return;
  }
  if (event.type == MIDI_MSG_MSB) {
    ccMsb[event.number] = event.value;
  } else if (event.type == MIDI_MSG_LSB) {
    ccLsb[event.number] = event.value;
  }
  switch (event.number) {
    case 0: {
      int inst = instrumentFromBank(ccMsb[0], ccLsb[0], event.type == MIDI_MSG_LSB);
      SetInstrument(inst);
      SetHintF("Bank %d", inst);
      break;
    }
    case 1: {
      int amount = ((int)event.value14 * 3) / 16384;
      voices[selectedTrack].lowPassMult = (uint8_t)amount;
      SetHintF("Mod %d", amount);
      break;
    }
    case 7: {
      int level = volumeFrom14(event.value14);
      voices[selectedTrack].volume = (uint8_t)level;
      SetHintF("Vol %d", level);
      break;
    }
    default:
      if (event.type == MIDI_MSG_MSB) {
        SetHintF("MSB %d", event.number);
      } else {
        SetHintF("LSB %d", event.number);
      }
      break;
  }
}

void Tracker::HandleMidi(const MidiEvent &event) {
  switch (event.type) {
    case MIDI_MSG_CLOCK:
      MidiClock(event.value14);
      return;
    case MIDI_MSG_START:
      MidiStart();
      return;
    case MIDI_MSG_CONTINUE:
      MidiContinue();
      return;
    case MIDI_MSG_STOP:
      MidiStop();
      return;
    case MIDI_MSG_SONG_POS:
      MidiSongPosition((int)event.value14);
      return;
    default:
      break;
  }
  // Channels 1-4 (status nibble 0-3) are the four tracks. Any other
  // channel plays whichever track is already selected.
  if (event.channel < 4) {
    selectedTrack = event.channel;
  }
  switch (event.type) {
    case MIDI_MSG_NOTE_ON: {
      int pitch = 0;
      int oct = 0;
      midiNoteToSynth(event.number, pitch, oct);
      ArmTransport();
      voices[selectedTrack].SetOctave(oct);
      SetNote(pitch, selectedTrack);
      break;
    }
    case MIDI_MSG_NOTE_OFF:
      break;
    case MIDI_MSG_MSB:
    case MIDI_MSG_LSB:
      ApplyController(event);
      break;
    default:
      break;
  }
}

void Tracker::CaptureSong(SongData *song) const {
  if (!song) {
    return;
  }
  memset(song, 0, sizeof(*song));
  song->patternLength = (uint16_t)patternLength;
  song->masterVolume = masterVolume;
  song->bpmSlot = bpmSlot;
  memcpy(song->bpms, bpms, sizeof(song->bpms));
  song->currentVoice = (uint8_t)currentVoice;
  song->selectedTrack = (uint8_t)selectedTrack;
  song->currentPattern = (uint8_t)currentPattern;
  song->allPatternPlay = allPatternPlay ? 1 : 0;
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < kMaxSteps; s++) {
      song->tracks[t][s] = NoteAt(t, s);
      song->octaves[t][s] = OctaveAt(t, s);
      song->instruments[t][s] = InstAt(t, s);
    }
    const Voice &voice = voices[t];
    SongVoice &out = song->voices[t];
    out.volume = voice.volume;
    out.mute = voice.mute ? 1 : 0;
    out.samplerMode = voice.samplerMode ? 1 : 0;
    out.overdrive = voice.overdrive ? 1 : 0;
    out.octave = voice.octave;
    out.envelopeNum = voice.EnvelopeNum();
    out.envelopeLength = voice.EnvelopeLength();
    out.phaserMult = voice.phaserMult;
    out.lowPassMult = voice.lowPassMult;
    out.reverbMult = voice.reverbMult;
    out.chordMult = voice.chordMult;
    out.pitchMult = voice.pitchMult;
    out.delayMult = voice.delayMult;
    out.whooshMult = voice.whooshMult;
    out.bend14 = voice.bend14;
    song->fx[t] = voice.fx;
    song->loops[t].enabled = (loopPlay[t].enabled || loopPlay[t].pending) ? 1 : 0;
    song->loops[t].quantize = loopPlay[t].quantize;
    memcpy(song->loops[t].library, loopPlay[t].library, 24);
    memcpy(song->loops[t].name, loopPlay[t].name, 24);
  }
}

void Tracker::ApplySong(const SongData &song) {
  patternLength = barsFromSteps(song.patternLength) * kStepsPerBar;
  masterVolume = song.masterVolume;
  memcpy(bpms, song.bpms, sizeof(bpms));
  currentPattern = song.currentPattern;
  if (currentPattern > 3) {
    currentPattern = 0;
  }
  selectedTrack = song.selectedTrack;
  if (selectedTrack < 0 || selectedTrack > 3) {
    selectedTrack = 0;
  }
  allPatternPlay = song.allPatternPlay != 0;
  solo = false;
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < kMaxSteps; s++) {
      uint8_t note = song.tracks[t][s];
      if (note > 15) {
        note = 0;
      }
      SetCell(t, s, note, song.octaves[t][s], song.instruments[t][s]);
    }
    Voice &voice = voices[t];
    const SongVoice &in = song.voices[t];
    voice.volume = in.volume;
    voice.mute = in.mute != 0;
    voice.samplerMode = in.samplerMode != 0;
    voice.overdrive = in.overdrive != 0;
    voice.octave = in.octave;
    voice.RestoreEnvelope(in.envelopeNum, in.envelopeLength);
    voice.phaserMult = in.phaserMult;
    voice.lowPassMult = in.lowPassMult;
    voice.reverbMult = in.reverbMult;
    voice.chordMult = in.chordMult;
    voice.pitchMult = in.pitchMult;
    voice.delayMult = in.delayMult;
    voice.whooshMult = in.whooshMult;
    voice.bend14 = in.bend14;
    voice.CopyFx(song.fx[t]);
    voice.soloMute = false;
    voice.ReleaseShots();
  }
  SyncTrackVoicesFromSteps();
  if (selectedTrack < 0 || selectedTrack > 3) {
    selectedTrack = 0;
  }
  bool selectedHasNotes = false;
  for (int s = 0; s < kMaxSteps; s++) {
    if (NoteAt(selectedTrack, s) > 0) {
      selectedHasNotes = true;
      break;
    }
  }
  if (!selectedHasNotes) {
    int voice = song.currentVoice;
    if (voice < 0) {
      voice = 0;
    } else if (voice > 63) {
      voice = 63;
    }
    trackVoice[selectedTrack] = (uint8_t)voice;
  }
  RememberVoiceLabel(trackVoice[selectedTrack]);
  editBar = 0;
  followView = true;
  clocksSinceZero = 0;
  blockLearn = true;
  ClampTransport();
  trackIndex = patternLength * currentPattern;
  stepSampleCount = 0;
  pressedOnce = true;
  memset(loopPlay, 0, sizeof(loopPlay));
  memset(&audition, 0, sizeof(audition));
  SetBPM(song.bpmSlot);
}

void Tracker::PlayPitch(int pitch, int octave) {
  if (pitch < 0) {
    pitch = 0;
  } else if (pitch > 11) {
    pitch = 11;
  }
  if (octave < 0) {
    octave = 0;
  } else if (octave > 3) {
    octave = 3;
  }
  int saved = voices[selectedTrack].octave;
  voices[selectedTrack].octave = (int8_t)octave;
  ArmTransport();
  SetNote(pitch, selectedTrack);
  QueueMidi(MIDI_MSG_NOTE_ON, (uint8_t)selectedTrack, (uint8_t)synthToMidiNote(pitch, octave), 100);
  voices[selectedTrack].octave = (int8_t)saved;
}

void Tracker::NudgeBpm(int delta) {
  int bpm = (int)bpms[bpmSlot] + delta;
  if (bpm < 40) {
    bpm = 40;
  } else if (bpm > 240) {
    bpm = 240;
  }
  bpms[bpmSlot] = (uint8_t)bpm;
  SetBPM(bpmSlot);
  for (int i = 0; i < 4; i++) {
    RecomputeLoopInc(&loopPlay[i]);
  }
  RecomputeLoopInc(&audition);
  SetHintF("BPM: %d", bpm);
}

void Tracker::RecomputeLoopInc(LoopPlay *loop) {
  if (!loop) {
    return;
  }
  int lb = loop->bpm > 0 ? loop->bpm : 120;
  int pb = bpms[bpmSlot] > 0 ? bpms[bpmSlot] : 120;
  int rate = loop->rate > 0 ? loop->rate : kSampleRate;
  uint64_t num = (uint64_t)rate * (uint64_t)pb;
  uint64_t den = (uint64_t)kSampleRate * (uint64_t)lb;
  if (den == 0) {
    den = 1;
  }
  loop->inc = (uint32_t)((num << 16) / den);
  if (loop->inc < 1) {
    loop->inc = 1;
  }
}

static int loopSampleAt(Tracker::LoopPlay *loop, int index) {
  if (loop->hold > 0) {
    return pcmHoldAt(loop->hold, index);
  }
  if (!loop->pcm || index < 0 || index >= loop->frames) {
    return 0;
  }
  return loop->pcm[index];
}

int Tracker::ReadLoop(LoopPlay *loop) {
  if (loop->frames < 2 || (loop->hold <= 0 && !loop->pcm)) {
    return 0;
  }
  uint32_t pos = loop->phase >> 16;
  uint32_t frac = loop->phase & 0xFFFF;
  int i0 = (int)(pos % (uint32_t)loop->frames);
  int i1 = i0 + 1;
  if (i1 >= loop->frames) {
    i1 = 0;
  }
  int s0 = loopSampleAt(loop, i0);
  int s1 = loopSampleAt(loop, i1);
  int s = s0 + (int)(((int64_t)(s1 - s0) * frac) >> 16);
  loop->phase += loop->inc > 0 ? loop->inc : 1;
  uint32_t limit = (uint32_t)loop->frames << 16;
  if (limit > 0 && loop->phase >= limit) {
    loop->phase %= limit;
  }
  return s;
}

void Tracker::LaunchPending(int localStep) {
  for (int i = 0; i < 4; i++) {
    if (!loopPlay[i].pending) {
      continue;
    }
    int q = loopPlay[i].quantize;
    bool hit = (q == 0) || (q == 1 && (localStep % 4) == 0) || (q == 2 && (localStep % 16) == 0);
    if (!hit) {
      continue;
    }
    loopPlay[i].pending = 0;
    loopPlay[i].enabled = 1;
    loopPlay[i].phase = 0;
    RecomputeLoopInc(&loopPlay[i]);
  }
}

static void copyName(char *dst, const char *src) {
  memset(dst, 0, 24);
  if (!src) {
    return;
  }
  strncpy(dst, src, 23);
}

void Tracker::TriggerLoopVoice(int track, int note) {
  if (track < 0 || track > 3) {
    return;
  }
  LoopHit hit;
  if (!loopInstrumentHit(note, &hit) || hit.frames < 2 || (hit.hold <= 0 && !hit.pcm)) {
    return;
  }
  LoopPlay &slot = loopPlay[track];
  slot.pcm = hit.pcm;
  slot.hold = hit.hold;
  slot.frames = hit.frames;
  slot.rate = hit.rate > 0 ? hit.rate : 8000;
  slot.bpm = hit.bpm > 0 ? hit.bpm : 120;
  slot.quantize = 0;
  slot.pending = 0;
  slot.enabled = 1;
  strncpy(slot.library, hit.library, sizeof(slot.library) - 1);
  slot.library[sizeof(slot.library) - 1] = 0;
  strncpy(slot.name, hit.name, sizeof(slot.name) - 1);
  slot.name[sizeof(slot.name) - 1] = 0;
  RecomputeLoopInc(&slot);
  int stepInBar = trackIndex % 16;
  if (stepInBar < 0) {
    stepInBar = 0;
  }
  uint32_t bar = samplesPerStep * 16;
  if (bar < 1) {
    bar = 1;
  }
  uint32_t into = (uint32_t)stepInBar * samplesPerStep + stepSampleCount;
  if (into > bar) {
    into = bar;
  }
  slot.phase = (uint32_t)(((uint64_t)into * (uint32_t)slot.frames << 16) / bar);
}

void Tracker::ArmLoop(int track, const LoopArm &arm) {
  if (track < 0 || track > 3 || arm.frames < 2 || (arm.hold <= 0 && !arm.pcm)) {
    SetHint("No loop");
    return;
  }
  LoopPlay &slot = loopPlay[track];
  slot.pcm = arm.pcm;
  slot.hold = arm.hold;
  slot.frames = arm.frames;
  slot.rate = arm.rate > 0 ? arm.rate : kSampleRate;
  slot.bpm = arm.bpm > 0 ? arm.bpm : 120;
  slot.quantize = arm.quantize > 2 ? 0 : arm.quantize;
  copyName(slot.library, arm.library);
  copyName(slot.name, arm.name);
  RecomputeLoopInc(&slot);
  slot.phase = 0;
  if (slot.quantize == 0) {
    slot.pending = 0;
    slot.enabled = 1;
    SetHint("Loop go");
  } else {
    slot.enabled = 0;
    slot.pending = 1;
    SetHint(slot.quantize == 1 ? "Loop beat" : "Loop bar");
  }
}

void Tracker::StopLoop(int track) {
  if (track < 0 || track > 3) {
    return;
  }
  memset(&loopPlay[track], 0, sizeof(loopPlay[track]));
  SetHint("Loop stop");
}

void Tracker::StartAudition(const LoopArm &arm) {
  if (arm.frames < 2 || (arm.hold <= 0 && !arm.pcm)) {
    SetHint("No loop");
    return;
  }
  audition.pcm = arm.pcm;
  audition.hold = arm.hold;
  audition.frames = arm.frames;
  audition.rate = arm.rate > 0 ? arm.rate : kSampleRate;
  audition.bpm = arm.bpm > 0 ? arm.bpm : 120;
  audition.quantize = 0;
  audition.pending = 0;
  audition.enabled = 1;
  audition.phase = 0;
  copyName(audition.library, arm.library);
  copyName(audition.name, arm.name);
  RecomputeLoopInc(&audition);
  SetHint("Audition");
}

void Tracker::StopAudition() {
  memset(&audition, 0, sizeof(audition));
}

void Tracker::WritePattern(int track, const uint8_t *steps, int count) {
  if (track < 0 || track > 3 || !steps || count <= 0) {
    return;
  }
  int start = patternLength * currentPattern;
  int n = count < patternLength ? count : patternLength;
  for (int i = 0; i < n; i++) {
    uint8_t note = steps[i];
    if (note > 12) {
      note = 0;
    }
    if (start + i >= kMaxSteps) {
      break;
    }
    SetCell(track, start + i, note, voices[track].octave, trackVoice[track]);
  }
  SetHint("Pattern put");
}

void Tracker::FillSnap(Snap *snap) const {
  if (!snap) {
    return;
  }
  snap->playing = isPlaying ? 1 : 0;
  snap->armed = pressedOnce ? 1 : 0;
  snap->track = (uint8_t)selectedTrack;
  snap->pattern = (uint8_t)currentPattern;
  snap->songMode = allPatternPlay ? 1 : 0;
  snap->solo = solo ? 1 : 0;
  int shown = trackIndex;
  if (shown < 0) {
    shown = 0;
  }
  snap->step = (uint16_t)shown;
  snap->patternLength = (uint16_t)patternLength;
  snap->bpm = bpms[bpmSlot];
  snap->bpmSlot = bpmSlot;
  snap->octave = (uint8_t)voices[selectedTrack].octave;
  snap->currentVoice = (uint8_t)currentVoice;
  memcpy(snap->trackVoice, trackVoice, sizeof(snap->trackVoice));
  snap->masterVolume = masterVolume;
  snap->envNum = voices[selectedTrack].EnvelopeNum();
  snap->envLen = voices[selectedTrack].EnvelopeLength();
  memset(snap->inst, 0, sizeof(snap->inst));
  strncpy(snap->inst, oledInstString, sizeof(snap->inst) - 1);
  memset(snap->hint, 0, sizeof(snap->hint));
  strncpy(snap->hint, hint, sizeof(snap->hint) - 1);
  int patStart = patternLength * currentPattern;
  int bars = Bars();
  int bar = editBar;
  if (bar < 0) {
    bar = 0;
  } else if (bar >= bars) {
    bar = bars - 1;
  }
  int origin = patStart + bar * kStepsPerBar;
  if (origin < 0) {
    origin = 0;
  }
  if (origin >= kMaxSteps) {
    origin = patStart > 0 ? patStart : 0;
  }
  snap->barOrigin = (uint16_t)origin;
  snap->barIndex = (uint8_t)(bar + 1);
  snap->barCount = (uint8_t)bars;
  snap->patternSlots = (uint8_t)PatternSlots();
  for (int t = 0; t < 4; t++) {
    snap->level[t] = (int16_t)lastSamples[t];
    snap->vol[t] = voices[t].volume;
    snap->mute[t] = voices[t].mute ? 1 : 0;
    snap->drive[t] = voices[t].overdrive ? 1 : 0;
    snap->sampler[t] = voices[t].samplerMode ? 1 : 0;
    snap->lp[t] = voices[t].lowPassMult;
    snap->rev[t] = voices[t].reverbMult;
    snap->pha[t] = voices[t].phaserMult;
    snap->dly[t] = voices[t].delayMult;
    snap->arp[t] = voices[t].chordMult;
    snap->whoosh[t] = voices[t].whooshMult;
    snap->pitchFx[t] = voices[t].pitchMult;
    snap->loopOn[t] = loopPlay[t].enabled || loopPlay[t].pending ? 1 : 0;
    if (t == selectedTrack) {
      const TrackFx &fx = voices[t].fx;
      snap->fxFilter = fx.filter;
      snap->fxCutoff = fx.cutoff;
      snap->fxRes = fx.res;
      snap->fxDelay = fx.delayDiv;
      snap->fxFb = fx.delayFb;
      snap->fxMix = fx.delayMix;
      snap->fxRev = fx.reverb;
      snap->fxCrush = fx.crush;
      snap->fxDrive = fx.drive;
      snap->fxChorus = fx.chorus;
      snap->fxTrem = fx.tremolo;
    }
    for (int s = 0; s < 16; s++) {
      int idx = origin + s;
      snap->notes[t][s] = (idx >= 0 && idx < kMaxSteps) ? NoteAt(t, idx) : 0;
    }
  }
}
