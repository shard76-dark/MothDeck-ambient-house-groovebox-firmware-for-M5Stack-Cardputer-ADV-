#include "BoardConfig.h"

#if MOTHDECK_BOARD_HANDHELD

#include "AudioEngine.h"
#include "BeatDisplay.h"
#include "BleMidi.h"
#include "HandheldIo.h"
#include "KeypadMap.h"
#include "Ui.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

static Preferences prefs;
static bool prefsLoaded = false;
static char bleName[20] = MOTHDECK_BLE_NAME_DEFAULT;
static uint8_t bleOn = 0;
static uint8_t bleLoad = 1;
static int syncMode = 1;
static uint8_t outVol = 160;
static int latch = -1;
static bool bpmEdit = false;

static const struct {
  char ch;
  uint8_t col[5];
} kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'#', {0x12, 0x3F, 0x12, 0x3F, 0x12}}, {'%', {0x63, 0x13, 0x28, 0x66, 0x41}},
    {'\'', {0x00, 0x07, 0x03, 0x00, 0x00}}, {'*', {0x2A, 0x1C, 0x3E, 0x1C, 0x2A}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}}, {',', {0x40, 0x30, 0x10, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}}, {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x60, 0x10, 0x08, 0x06, 0x01}}, {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}}, {'2', {0x62, 0x51, 0x49, 0x49, 0x46}},
    {'3', {0x22, 0x49, 0x49, 0x49, 0x36}}, {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}}, {'6', {0x3E, 0x49, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}}, {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x49, 0x3E}}, {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'?', {0x02, 0x01, 0x59, 0x05, 0x02}}, {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x41, 0x3E}}, {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x3A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x30, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x04, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}}, {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *glyphFor(char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = (char)(ch - 'a' + 'A');
  }
  for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++) {
    if (kGlyphs[i].ch == ch) {
      return kGlyphs[i].col;
    }
  }
  return kGlyphs[0].col;
}

static void drawText(int x, int y, const char *text) {
  if (!text) {
    return;
  }
  while (*text && x < 128) {
    handheldOledGlyph(x, y, glyphFor(*text));
    x += 6;
    text++;
  }
}

void uiLoadPrefs() {
  if (prefsLoaded) {
    return;
  }
  prefs.begin("mothdeck", false);
  outVol = prefs.getUChar("vol", 160);
  String stored = prefs.getString("ble", MOTHDECK_BLE_NAME_DEFAULT);
  snprintf(bleName, sizeof(bleName), "%s", stored.c_str());
  bool migrateName = prefs.getUChar("nm12", 0) == 0;
  if (!bleName[0] || migrateName || strcmp(bleName, "MothDeck") == 0 || strcmp(bleName, "MothSynth") == 0) {
    snprintf(bleName, sizeof(bleName), "%s", MOTHDECK_BLE_NAME_DEFAULT);
  }
  if (migrateName || strcmp(bleName, stored.c_str()) != 0) {
    prefs.putString("ble", bleName);
    prefs.putUChar("nm12", 1);
  }
  bleOn = prefs.getUChar("bleEn", 0) ? 1 : 0;
  bleLoad = prefs.getUChar("bleLoad", 1) ? 1 : 0;
  syncMode = prefs.getUChar("sync", 1);
  if (syncMode > 2) {
    syncMode = 1;
  }
  prefsLoaded = true;
}

const char *uiBleName() {
  return bleName;
}

bool uiBleEnabled() {
  uiLoadPrefs();
  return bleOn != 0;
}

bool uiBleWantLoad() {
  uiLoadPrefs();
  return bleLoad != 0;
}

int uiSyncMode() {
  uiLoadPrefs();
  return syncMode;
}

void uiBegin() {
  uiLoadPrefs();
  audioSetSpeakerVolume(outVol);
}

void uiMountStorage(bool withLoops) {
  (void)withLoops;
  Serial.println("UI: no SD on this board");
}

static const char *latchName() {
  if (bpmEdit) {
    return "BPM";
  }
  if (latch == 0) {
    return "F1";
  }
  if (latch == 1) {
    return "F2";
  }
  if (latch == 2) {
    return "F3";
  }
  if (latch == 3) {
    return "F4";
  }
  return "LIVE";
}

void uiPoll(BleMidi &ble) {
  (void)ble;
  int key = -1;
  bool hold = false;
  if (!handheldPollKey(&key, &hold)) {
    return;
  }
  if (hold && key == 0) {
    bpmEdit = !bpmEdit;
    latch = -1;
    return;
  }
  if (bpmEdit) {
    if (key == 0) {
      bpmEdit = false;
      return;
    }
    if (key == 12) {
      audioCommand('b', -1);
    } else if (key == 13) {
      audioCommand('b', 1);
    } else if (key == 14) {
      audioCommand('b', -5);
    } else if (key == 15) {
      audioCommand('b', 5);
    }
    return;
  }
  HandheldPadAct act = handheldBaseAct(latch, key);
  latch = act.latch;
  if (act.cmd) {
    audioCommand(act.cmd, act.val);
  }
}

static void updateLamps(const Snap &snap) {
  static int lastStep = -999;
  static uint32_t stepUntil = 0;
  static uint32_t beatUntil = 0;
  uint32_t now = millis();
  if (!snap.playing) {
    lastStep = (int)snap.step;
  } else if ((int)snap.step != lastStep) {
    stepUntil = now + 45;
    if (isBeatStep((int)snap.step, (int)snap.patternLength, (int)snap.pattern)) {
      beatUntil = now + 90;
    }
    lastStep = (int)snap.step;
  }
  bool stepOn = (int32_t)(stepUntil - now) > 0;
  bool beatOn = (int32_t)(beatUntil - now) > 0;
  handheldLeds(stepOn, beatOn, snap.playing != 0, snap.rec != 0);
}

void uiDraw(BleMidi &ble) {
  Snap snap;
  memset(&snap, 0, sizeof(snap));
  audioReadSnap(&snap);
  updateLamps(snap);

  bool playing = snap.playing != 0;
  BarBeat pos = barBeatFromNextStep((int)snap.step, (int)snap.patternLength, (int)snap.pattern);
  SevenSeg seg;
  formatBeatReadout(playing, pos.bar, pos.beat, snap.bpm, &seg);
  handheldShowDigits(seg.digit, seg.dot);

  handheldOledClear();
  char line[24];
  int mv = handheldBatteryMv();
  if (mv > 0) {
    snprintf(line, sizeof(line), "MOTH %d.%02dV", mv / 1000, (mv % 1000) / 10);
  } else {
    snprintf(line, sizeof(line), "MOTHDECK HH");
  }
  drawText(0, 0, line);

  snprintf(line, sizeof(line), "T%d P%d %d %s", snap.track + 1, snap.pattern + 1, snap.bpm,
           playing ? (snap.rec ? "REC" : "PLAY") : "STOP");
  drawText(0, 8, line);

  if (playing) {
    snprintf(line, sizeof(line), "BAR %d BEAT %d", pos.bar, pos.beat);
  } else {
    snprintf(line, sizeof(line), "BPM %d  %s", snap.bpm, latchName());
  }
  drawText(0, 16, line);

  int track = snap.track;
  if (track < 0 || track > 3) {
    track = 0;
  }
  for (int s = 0; s < 16; s++) {
    int x = 4 + s * 7;
    bool hit = snap.notes[track][s] != 0;
    handheldOledFill(x, hit ? 26 : 30, 5, hit ? 8 : 4, true);
    if (playing && s == pos.stepInBar) {
      handheldOledFill(x, 24, 5, 2, true);
    }
  }

  if (playing) {
    snprintf(line, sizeof(line), "%s", latchName());
  } else if (snap.hint[0]) {
    snprintf(line, sizeof(line), "%s", snap.hint);
  } else {
    snprintf(line, sizeof(line), "%s", latchName());
  }
  drawText(0, 36, line);

  if (snap.hint[0] && playing) {
    drawText(0, 44, snap.hint);
  } else if (bpmEdit) {
    drawText(0, 44, "C -1  C# +1");
  } else {
    snprintf(line, sizeof(line), "OCT %d %s", snap.octave, snap.inst);
    drawText(0, 44, line);
  }

  snprintf(line, sizeof(line), "%s %s", ble.Connected() ? "BLE" : "ble", ble.UserEnabled() ? "ON" : "OFF");
  drawText(0, 56, line);
  const char *fault = audioFaultText();
  if (fault && strcmp(fault, "audio ok") != 0) {
    drawText(64, 56, "NO DAC");
  }
  handheldOledFlush();
}

#endif
