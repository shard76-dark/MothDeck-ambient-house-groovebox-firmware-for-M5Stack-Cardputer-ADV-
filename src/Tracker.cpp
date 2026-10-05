#include "Tracker.h"
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
  bpms[0] = 120;
  bpms[1] = 140;
  bpms[2] = 95;
  bpms[3] = 180;
  ClearAll(0);
  SetBPM(0);
}

int Tracker::UpdateTracker() {
  tempoBlink = 0;

  if (++stepSampleCount >= samplesPerStep) {
    stepSampleCount = 0;
    barCount++;
    if (barCount > 3) {
      tempoBlink = 30;
      barCount = 0;
      if (!pressedOnce) {
        SetNote(7, 0);
      }
    }

    int local = trackIndex - patternLength * currentPattern;
    if (local < 0) {
      local = 0;
    }
    LaunchPending(local);

    for (int i = 0; i < 4; i++) {
      int note = tracks[i][trackIndex];
      if (note > 0) {
        int inst = trackInstruments[i][trackIndex];
        if (inst == kLoopsVoice) {
          TriggerLoopVoice(i, note - 1);
        }
        voices[i].SetNote(note - 1, false, trackOctaves[i][trackIndex], inst);
      }
    }

    trackIndex++;
    int patternEnd = patternLength * (currentPattern + 1);
    if (trackIndex >= patternEnd) {
      if (allPatternPlay) {
        currentPattern++;
        if (currentPattern > 3) {
          currentPattern = 0;
        }
      }
      trackIndex = patternLength * currentPattern;
    }
  }

  const int div = 2 + masterVolume * 5;
  int mix = 0;
  for (int i = 0; i < 4; i++) {
    int samp;
    if (loopPlay[i].enabled && loopPlay[i].frames > 1 && (loopPlay[i].pcm || loopPlay[i].hold > 0)) {
      samp = ReadLoop(&loopPlay[i]) / div;
    } else {
      samp = voices[i].UpdateVoice() / div;
    }
    lastSamples[i] = samp;
    mix += samp;
  }
  if (audition.enabled && audition.frames > 1 && (audition.pcm || audition.hold > 0)) {
    mix += ReadLoop(&audition) / (div + 2);
  }
  if (mix > 32767) {
    mix = 32767;
  } else if (mix < -32768) {
    mix = -32768;
  }
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
      SetHintF("Pattern: %d", val + 1);
      break;
    case '#':
      ClearPatternNum(val);
      SetHintF("Clr Pattern: %d", val + 1);
      break;
    case 'X':
      ClearAll(val);
      SetHintF("New Song: %d", 32 * (val + 1));
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
      if (trackIndex >= 0 && trackIndex < kMaxSteps) {
        tracks[selectedTrack][trackIndex] = 0;
      }
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
    tracks[track][trackIndex] = val + 1;
    trackOctaves[track][trackIndex] = voices[selectedTrack].octave;
    trackInstruments[track][trackIndex] = (uint8_t)inst;
    lastNoteTrackIndex = trackIndex % patternLength;
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
  int start = patternLength * currentPattern;
  int end = start + patternLength;
  for (int i = start; i < end; i++) {
    tracks[val][i] = 0;
  }
}

void Tracker::SetPatternNum(int val) {
  trackIndex = trackIndex - (patternLength * currentPattern) + (patternLength * val);
  currentPattern = val;
}

void Tracker::ClearPatternNum(int val) {
  (void)val;
  int start = patternLength * currentPattern;
  int end = start + patternLength;
  for (int j = 0; j < 4; j++) {
    for (int i = start; i < end; i++) {
      tracks[j][i] = 0;
    }
  }
}

void Tracker::TogglePlayStop() {
  isPlaying = !isPlaying;
}

void Tracker::CopyPattern() {
  int start = patternLength * currentPattern;
  for (int j = 0; j < 4; j++) {
    memcpy(patternCopy[j], &tracks[j][start], patternLength);
    memcpy(patternCopyInstruments[j], &trackInstruments[j][start], patternLength);
    memcpy(patternCopyOctaves[j], &trackOctaves[j][start], patternLength);
  }
}

void Tracker::PastePattern() {
  int start = patternLength * currentPattern;
  for (int j = 0; j < 4; j++) {
    memcpy(&tracks[j][start], patternCopy[j], patternLength);
    memcpy(&trackInstruments[j][start], patternCopyInstruments[j], patternLength);
    memcpy(&trackOctaves[j][start], patternCopyOctaves[j], patternLength);
  }
  SyncTrackVoicesFromSteps();
}

void Tracker::PastePatternAll() {
  for (int r = 0; r < 4; r++) {
    int start = patternLength * r;
    for (int j = 0; j < 4; j++) {
      memcpy(&tracks[j][start], patternCopy[j], patternLength);
      memcpy(&trackInstruments[j][start], patternCopyInstruments[j], patternLength);
      memcpy(&trackOctaves[j][start], patternCopyOctaves[j], patternLength);
    }
  }
  SyncTrackVoicesFromSteps();
}

void Tracker::ClearAll(int val) {
  selectedTrack = 0;
  currentPattern = 0;
  isPlaying = true;
  pressedOnce = false;
  allPatternPlay = false;
  currentVoice = 0;
  trackIndex = 0;
  stepSampleCount = 0;
  barCount = 0;
  memset(tracks, 0, sizeof(tracks));
  memset(trackInstruments, 0, sizeof(trackInstruments));
  memset(trackOctaves, 0, sizeof(trackOctaves));
  memset(trackVoice, 0, sizeof(trackVoice));
  RememberVoiceLabel(0);
  for (int j = 0; j < 4; j++) {
    voices[j].ResetEffects();
    voices[j].SetEnvelopeNum(0);
    voices[j].volume = 2;
    voices[j].SetOctave(1);
    voices[j].bend14 = 8192;
  }
  patternLength = 32 + (32 * val);
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
  uint8_t first = trackInstruments[track][0];
  bool same = true;
  for (int s = 1; s < kMaxSteps; s++) {
    if (trackInstruments[track][s] != first) {
      same = false;
      break;
    }
  }
  if (same) {
    return first;
  }
  for (int s = 0; s < kMaxSteps; s++) {
    if (tracks[track][s] > 0) {
      return trackInstruments[track][s];
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
    trackInstruments[selectedTrack][i] = (uint8_t)val;
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
    memcpy(song->tracks[t], tracks[t], kMaxSteps);
    memcpy(song->octaves[t], trackOctaves[t], kMaxSteps);
    memcpy(song->instruments[t], trackInstruments[t], kMaxSteps);
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
  patternLength = song.patternLength;
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
    memcpy(tracks[t], song.tracks[t], kMaxSteps);
    memcpy(trackOctaves[t], song.octaves[t], kMaxSteps);
    memcpy(trackInstruments[t], song.instruments[t], kMaxSteps);
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
  }
  SyncTrackVoicesFromSteps();
  if (selectedTrack < 0 || selectedTrack > 3) {
    selectedTrack = 0;
  }
  bool selectedHasNotes = false;
  for (int s = 0; s < kMaxSteps; s++) {
    if (tracks[selectedTrack][s] > 0) {
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
    tracks[track][start + i] = note;
    trackOctaves[track][start + i] = voices[track].octave;
    trackInstruments[track][start + i] = trackVoice[track];
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
  int origin = (shown / 16) * 16;
  int patStart = patternLength * currentPattern;
  int patEnd = patStart + patternLength;
  if (origin < patStart) {
    origin = patStart;
  }
  if (origin + 16 > patEnd && patEnd >= 16) {
    origin = patEnd - 16;
  }
  snap->barOrigin = (uint16_t)origin;
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
      snap->notes[t][s] = (idx >= 0 && idx < kMaxSteps) ? tracks[t][idx] : 0;
    }
  }
}
