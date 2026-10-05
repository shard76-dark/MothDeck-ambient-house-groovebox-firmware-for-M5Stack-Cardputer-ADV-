#include "Ui.h"
#include "ModalInput.h"
#include "AudioEngine.h"
#include "InstrumentBank.h"
#include "LoopLibrary.h"
#include "SdStorage.h"
#include "SdCard.h"
#include "LauncherExit.h"
#include "BleMidi.h"
#include "BoardConfig.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const uint16_t COL_BG = 0x1082;
static const uint16_t COL_BAR = 0x2104;
static const uint16_t COL_AMBER = 0xFD20;
static const uint16_t COL_TEXT = 0xEF5D;
static const uint16_t COL_DIM = 0x6B6D;
static const uint16_t COL_PLAY = 0x07E0;
static const uint16_t COL_WARN = 0xFBE0;
static const uint16_t COL_TRACK[4] = {0xFD20, 0x2D7F, 0xF81F, 0x07E0};

static const char *kPageName[] = {"Play", "Instrument", "Mixer", "Song", "Loops", "MIDI", "Settings", "Exit"};
static const int kPageCount = 8;
static const int kExitPage = 7;
static bool launcherOk = false;

static const char *kNoteName[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *kEnvName[] = {"Fade out", "Fade in", "No fade", "Loop"};

struct PianoKey {
  char key;
  int pitch;
  int add;
};

static const PianoKey kPiano[] = {
    {'z', 0, 0}, {'x', 2, 0}, {'c', 4, 0}, {'v', 5, 0}, {'b', 7, 0}, {'n', 9, 0}, {'m', 11, 0}, {',', 0, 1},
    {'s', 1, 0}, {'d', 3, 0}, {'g', 6, 0}, {'h', 8, 0}, {'j', 10, 0},
    {'q', 0, 1}, {'w', 1, 1}, {'e', 2, 1}, {'r', 3, 1}, {'t', 4, 1}, {'y', 5, 1},
    {'u', 6, 1}, {'i', 7, 1}, {'o', 8, 1}, {'p', 9, 1}, {'[', 10, 1}, {']', 11, 1},
};

static M5Canvas *canvas = nullptr;
static Preferences prefs;
static int page = 0;
static int cursor = 0;
static int mixRow = 0;
static int songSlot = 0;
static int loopLib = 0;
static int loopRow = 0;
static int quantize = 2;
static bool overlay = false;
static bool naming = false;
static char bleName[20] = MOTHDECK_BLE_NAME_DEFAULT;
static char edit[20];
static char toast[48];
static uint32_t toastUntil = 0;
static uint8_t outVol = 160;
static uint8_t bright = 180;
static uint32_t escSince = 0;
static bool escDown = false;
static bool escFired = false;
static int lengthChoice = 0;
static Snap snap;
static SdStorage storage;

static void toastSet(const char *msg) {
  snprintf(toast, sizeof(toast), "%s", msg ? msg : "");
  toastUntil = millis() + 2200;
}

static void savePrefs() {
  prefs.putUChar("vol", outVol);
  prefs.putUChar("bri", bright);
  prefs.putString("ble", bleName);
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static bool pianoKey(char c, int *pitch, int *add) {
  for (unsigned i = 0; i < sizeof(kPiano) / sizeof(kPiano[0]); i++) {
    if (kPiano[i].key == c) {
      *pitch = kPiano[i].pitch;
      *add = kPiano[i].add;
      return true;
    }
  }
  return false;
}

static void playNote(char c, bool shift, bool alt, bool opt) {
  int pitch = 0;
  int add = 0;
  if (!pianoKey(c, &pitch, &add)) {
    return;
  }
  int oct = (int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0);
  audioReadSnap(&snap);
  oct = (int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0);
  oct = clampi(oct, 0, 3);
  // PlayPitch is a tracker method. Route it as a note command plus octave.
  audioCommand('O', oct);
  audioCommand('N', pitch);
  audioCommand('O', snap.octave);
}

static int pageSpan() {
  return launcherOk ? kPageCount : kExitPage;
}

static int stepPage(int from, int delta) {
  int n = pageSpan();
  if (n < 1) {
    return 0;
  }
  if (from < 0 || from >= n) {
    from = 0;
  }
  return (from + delta + n) % n;
}

static void requestExit() {
  if (!launcherInstalled()) {
    launcherOk = false;
    toastSet("Launcher not found");
    return;
  }
  canvas->fillSprite(COL_BG);
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(8, 36);
  canvas->println("Returning to Launcher");
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(8, 52);
  canvas->println("Clears the boot slot");
  canvas->setCursor(8, 68);
  canvas->println("Press Enter on splash");
  canvas->pushSprite(0, 0);
  delay(500);
  if (!exitToLauncher()) {
    launcherOk = false;
    toastSet("Launcher not found");
  }
}

const char *uiBleName() {
  return bleName;
}

void uiBegin() {
  canvas = new M5Canvas(&M5Cardputer.Display);
  canvas->createSprite(240, 135);
  canvas->setTextSize(1);
  canvas->setTextColor(COL_TEXT);
  prefs.begin("mothdeck", false);
  outVol = prefs.getUChar("vol", 160);
  bright = prefs.getUChar("bri", 180);
  String stored = prefs.getString("ble", MOTHDECK_BLE_NAME_DEFAULT);
  snprintf(bleName, sizeof(bleName), "%s", stored.c_str());
  if (!bleName[0]) {
    snprintf(bleName, sizeof(bleName), "%s", MOTHDECK_BLE_NAME_DEFAULT);
  }
  M5Cardputer.Display.setBrightness(bright);
  audioSetSpeakerVolume(outVol);
  launcherOk = launcherInstalled();
  instrumentBank.Scan();
  loopLibrary.Scan();
}

static void statusBar(BleMidi &ble) {
  canvas->fillRect(0, 0, 240, 12, COL_BAR);
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 2);
  canvas->print("MothDeck");
  canvas->setTextColor(COL_TEXT);
  canvas->printf(" T%d", snap.track + 1);
  canvas->printf(" %d", snap.bpm);
  canvas->printf(" P%d", snap.pattern + 1);
  canvas->setTextColor(snap.playing ? COL_PLAY : COL_DIM);
  canvas->print(snap.playing ? " PLAY" : " STOP");
  canvas->setTextColor(ble.Connected() ? COL_PLAY : COL_DIM);
  canvas->print(ble.Connected() ? " MIDI" : " midi");
  int pct = M5.Power.getBatteryLevel();
  canvas->setTextColor(COL_TEXT);
  if (pct >= 0) {
    canvas->setCursor(196, 2);
    canvas->printf("%d%%", pct);
  }
}

static void legend(const char *text) {
  canvas->fillRect(0, 122, 240, 13, COL_BAR);
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 124);
  canvas->print(text);
}

static void drawToast() {
  if (!toast[0] || (int32_t)(millis() - toastUntil) > 0) {
    return;
  }
  canvas->fillRect(4, 100, 232, 18, COL_WARN);
  canvas->setTextColor(COL_BG);
  canvas->setCursor(8, 104);
  canvas->print(toast);
}

static void voiceName(int id, char *dst, int n) {
  if (n < 2) {
    return;
  }
  if (id >= 0 && id <= 11) {
    snprintf(dst, n, "%s", InstrumentBank::BuiltinName(id));
    return;
  }
  for (int i = 0; i < instrumentBank.Count(); i++) {
    const PluginInfo &p = instrumentBank.At(i);
    if (p.id == (uint8_t)id && (p.loaded || p.name[0])) {
      snprintf(dst, n, "%s", p.name[0] ? p.name : p.folder);
      return;
    }
  }
  snprintf(dst, n, "P%d", id);
}

static void drawTrackVoices(int y) {
  for (int t = 0; t < 4; t++) {
    char name[8];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    int x = t * 60;
    if (t == snap.track) {
      canvas->fillRect(x, y - 1, 59, 11, COL_TRACK[t]);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(x + 2, y);
    canvas->printf("%d %.6s", t + 1, name);
  }
}

static void drawPlay() {
  for (int t = 0; t < 4; t++) {
    int y = 16 + t * 12;
    canvas->setTextColor(t == snap.track ? COL_TRACK[t] : COL_DIM);
    canvas->setCursor(2, y);
    canvas->printf("%d", t + 1);
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(14, y);
    char name[8];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    canvas->printf("%.6s", name);
    int lv = snap.level[t];
    if (lv < 0) {
      lv = -lv;
    }
    int w = lv / 800;
    if (w > 40) {
      w = 40;
    }
    canvas->fillRect(70, y + 1, 40, 6, COL_BAR);
    canvas->fillRect(70, y + 1, w, 6, COL_TRACK[t]);
    if (snap.mute[t]) {
      canvas->setTextColor(COL_WARN);
      canvas->setCursor(114, y);
      canvas->print("M");
    }
    if (snap.loopOn[t]) {
      canvas->setTextColor(COL_PLAY);
      canvas->setCursor(124, y);
      canvas->print("L");
    }
  }
  int y = 66;
  for (int s = 0; s < 16; s++) {
    int x = 2 + s * 14;
    bool on = snap.notes[snap.track][s] > 0;
    bool head = (int)snap.barOrigin + s == (int)snap.step;
    uint16_t c = head ? COL_AMBER : (on ? COL_TRACK[snap.track] : COL_BAR);
    canvas->fillRect(x, y, 12, 10, c);
    if (on) {
      canvas->setTextColor(head ? COL_BG : COL_TEXT);
      canvas->setCursor(x + 1, y + 1);
      int note = snap.notes[snap.track][s];
      if (note >= 1 && note <= 12) {
        canvas->print(kNoteName[note - 1][0]);
      }
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 82);
  canvas->printf("Step %u/%u  Oct %u  %s", (unsigned)(snap.step % (snap.patternLength ? snap.patternLength : 1)) + 1,
                 snap.patternLength, snap.octave, snap.songMode ? "Song" : "Patt");
  canvas->setCursor(2, 94);
  canvas->setTextColor(COL_TEXT);
  canvas->print(snap.hint);
  canvas->setCursor(2, 106);
  canvas->setTextColor(COL_DIM);
  canvas->print("Z row piano   Q row +oct");
  legend("Spc play  Tab page  ` hold exit");
}

static int instCount() {
  return 12 + instrumentBank.Count();
}

static void instLabel(int index, char *dst, int n) {
  if (index < 12) {
    snprintf(dst, n, "%02d %s", index, InstrumentBank::BuiltinName(index));
    return;
  }
  const PluginInfo &p = instrumentBank.At(index - 12);
  snprintf(dst, n, "%s %s", p.loaded ? "*" : " ", p.name[0] ? p.name : p.folder);
}

static bool rowIsTrackVoice(int idx) {
  int id = snap.trackVoice[snap.track];
  if (idx < 12) {
    return id == idx;
  }
  if (idx - 12 >= instrumentBank.Count()) {
    return false;
  }
  const PluginInfo &p = instrumentBank.At(idx - 12);
  return p.loaded && p.id == (uint8_t)id;
}

static void drawInst() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Assign to track ");
  canvas->printf("%d", snap.track + 1);
  drawTrackVoices(28);
  int total = instCount();
  if (total < 1) {
    total = 12;
  }
  int top = cursor - 2;
  if (top < 0) {
    top = 0;
  }
  for (int row = 0; row < 6; row++) {
    int idx = top + row;
    if (idx >= total) {
      break;
    }
    char line[28];
    instLabel(idx, line, (int)sizeof(line));
    int y = 42 + row * 11;
    canvas->setCursor(2, y);
    canvas->setTextColor(idx == cursor ? COL_BG : COL_TEXT);
    if (idx == cursor) {
      canvas->fillRect(0, y - 2, 240, 11, COL_AMBER);
    }
    canvas->print(line);
    if (rowIsTrackVoice(idx)) {
      canvas->print(" =");
    }
    if (idx >= 12) {
      const PluginInfo &p = instrumentBank.At(idx - 12);
      if (p.error[0] && idx == cursor) {
        canvas->print(" ");
        canvas->print(p.error);
      }
    }
  }
  legend("Ent assign  R rescan  UpDn move");
}

static const char *kMixLabel[] = {"Volume", "Mute", "Solo", "Drive", "Low pass", "Retrig", "Wobble", "Echo", "Arp", "Whoosh", "Pitch", "Envelope", "Note len"};
static const int kMixRows = 13;

static int mixValue(int row) {
  int t = snap.track;
  switch (row) {
    case 0: return snap.vol[t];
    case 1: return snap.mute[t];
    case 2: return snap.solo;
    case 3: return snap.drive[t];
    case 4: return snap.lp[t];
    case 5: return snap.rev[t];
    case 6: return snap.pha[t];
    case 7: return snap.dly[t];
    case 8: return snap.arp[t];
    case 9: return snap.whoosh[t];
    case 10: return snap.pitchFx[t];
    case 11: return snap.envNum;
    default: return snap.envLen;
  }
}

static void tweakMix(int dir) {
  int row = mixRow;
  int v = mixValue(row);
  if (row == 0) {
    audioCommand('v', clampi(v + dir, 0, 8));
  } else if (row == 1) {
    audioCommand('V', 0);
  } else if (row == 2) {
    audioCommand('V', 3);
  } else if (row == 3) {
    audioCommand('V', 2);
  } else if (row == 11) {
    audioCommand('E', clampi(v + dir, 0, 3));
  } else if (row == 12) {
    int len = clampi((int)snap.envLen + dir, 1, 4);
    audioCommand('L', len - 1);
  } else if (dir > 0) {
    if (row >= 4 && row <= 6) {
      audioCommand('A', row - 3);
    } else if (row >= 7 && row <= 10) {
      audioCommand('D', row - 7);
    }
  } else {
    for (int i = 0; i < 2; i++) {
      if (row >= 4 && row <= 6) {
        audioCommand('A', row - 3);
      } else if (row >= 7 && row <= 10) {
        audioCommand('D', row - 7);
      }
    }
  }
}

static void drawMix() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->printf("Mixer  track %d  %s", snap.track + 1, snap.inst);
  int top = mixRow - 2;
  if (top < 0) {
    top = 0;
  }
  for (int row = 0; row < 7; row++) {
    int idx = top + row;
    if (idx >= kMixRows) {
      break;
    }
    int y = 30 + row * 12;
    if (idx == mixRow) {
      canvas->fillRect(0, y - 1, 240, 12, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    canvas->print(kMixLabel[idx]);
    canvas->setCursor(120, y);
    if (idx == 11) {
      canvas->print(kEnvName[clampi(mixValue(idx), 0, 3)]);
    } else if (idx == 1 || idx == 2 || idx == 3) {
      canvas->print(mixValue(idx) ? "on" : "off");
    } else {
      canvas->printf("%d", mixValue(idx));
    }
  }
  legend("1-4 track  Lf/Rt value  Ent same");
}

static void drawSong() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print(sdCard.Mounted() ? "Song slots  /moth" : "No SD card");
  for (int s = 0; s < 4; s++) {
    bool has = false;
    SdResult st = storage.Status(s, &has);
    int y = 32 + s * 14;
    if (s == songSlot) {
      canvas->fillRect(0, y - 2, 168, 13, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    if (st == SD_NO_CARD) {
      canvas->printf("Slot %d  ---", s + 1);
    } else {
      canvas->printf("Slot %d  %s", s + 1, has ? "saved" : "empty");
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(4, 92);
  canvas->printf("Len %u  %s  Mstr %u", snap.patternLength, snap.songMode ? "song" : "patt", 2 - snap.masterVolume);
  canvas->setCursor(4, 104);
  canvas->print("S save L load X del T stat");
  legend("N new  C copy  V paste  G all");
}

static void drawLoops() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  if (!sdCard.Mounted()) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(2, 40);
    canvas->print("Insert a card for loops");
    canvas->setCursor(2, 54);
    canvas->print("/moth/loops/<name>/");
    legend("R rescan   Tab page");
    return;
  }
  canvas->printf("Loops  q:%s  trk %d", quantize == 0 ? "now" : (quantize == 1 ? "beat" : "bar"), snap.track + 1);
  int n = loopLibrary.Count();
  if (n == 0) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(2, 40);
    canvas->print("No libraries on the card");
    legend("R rescan");
    return;
  }
  loopLib = clampi(loopLib, 0, n - 1);
  const LoopLibInfo &lib = loopLibrary.At(loopLib);
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 30);
  canvas->printf("%s  %dbpm  %db", lib.name, lib.bpm, lib.bars);
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 42);
  canvas->print(lib.tags);
  if (lib.error[0]) {
    canvas->setTextColor(COL_WARN);
    canvas->setCursor(2, 54);
    canvas->print(lib.error);
  }
  for (int i = 0; i < lib.entryCount && i < 5; i++) {
    int y = 66 + i * 10;
    if (i == loopRow) {
      canvas->fillRect(0, y - 1, 220, 10, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    canvas->print(lib.entries[i].kind == LOOP_PATTERN ? "PAT " : "WAV ");
    canvas->print(lib.entries[i].file);
  }
  legend("Ent launch  A aud  Q quant  S stop");
}

static void drawMidi(BleMidi &ble) {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print(ble.Connected() ? "MIDI connected" : "MIDI advertising");
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 30);
  canvas->printf("Name %s", bleName);
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 46);
  canvas->print("Ch 1-4 = tracks");
  canvas->setCursor(2, 58);
  canvas->print("Notes 36-83  C2-B5");
  canvas->setCursor(2, 70);
  canvas->print("CC0/32 bank  CC1 low pass");
  canvas->setCursor(2, 82);
  canvas->print("CC7/39 volume  bend pitch");
  canvas->setCursor(2, 94);
  canvas->print("Keys send Note On");
  canvas->setCursor(2, 106);
  canvas->print("Bank 12-63 = SD plugins");
  legend("E edit name in Settings");
}

static void drawSettings() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Settings");
  const char *rows[] = {"Speaker", "Brightness", "BLE name", "Battery", "Memory", "Card"};
  int top = cursor > 3 ? cursor - 3 : 0;
  for (int i = 0; i < 5; i++) {
    int idx = top + i;
    if (idx > 5) {
      break;
    }
    int y = 30 + i * 12;
    if (idx == cursor && !naming) {
      canvas->fillRect(0, y - 1, 240, 12, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    canvas->print(rows[idx]);
    canvas->setCursor(100, y);
    if (idx == 0) {
      canvas->printf("%u", outVol);
    } else if (idx == 1) {
      canvas->printf("%u", bright);
    } else if (idx == 2) {
      canvas->print(naming ? edit : bleName);
    } else if (idx == 3) {
      int mv = M5.Power.getBatteryVoltage();
      int pct = M5.Power.getBatteryLevel();
      if (pct < 0) {
        canvas->print("n/a");
      } else {
        canvas->printf("%dmV %d%%", mv, pct);
      }
    } else if (idx == 4) {
      canvas->printf("%uk", (unsigned)(ESP.getFreeHeap() / 1024));
      if (psramFound()) {
        canvas->printf(" P%uk", (unsigned)(ESP.getFreePsram() / 1024));
      }
    } else {
      canvas->print(sdCard.Mounted() ? "mounted" : "none");
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(4, 108);
  canvas->printf("v%s  %s", MOTHDECK_VERSION, BOARD_NAME);
  legend(naming ? "Ent apply  Bksp  ` cancel" : "Lf/Rt change  Ent name");
}

static void drawExit() {
  if (!launcherOk) {
    canvas->setTextColor(COL_DIM);
    canvas->setCursor(2, 36);
    canvas->print("Launcher not found");
    canvas->setCursor(2, 52);
    canvas->print("Exit stays off.");
    canvas->setCursor(2, 68);
    canvas->print("Hold ` does nothing.");
    legend("` back to Play");
    return;
  }
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Exit to Launcher");
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 36);
  canvas->print("Enter rewrites the boot");
  canvas->setCursor(2, 48);
  canvas->print("selection back to Launcher");
  canvas->setCursor(2, 60);
  canvas->print("and restarts.");
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 78);
  canvas->print("Hold ` or the side button");
  canvas->setCursor(2, 90);
  canvas->print("at any time, including boot.");
  canvas->setCursor(2, 102);
  canvas->print("Then press Enter on splash.");
  legend("Ent exit   ` back to Play");
}

static void drawOverlay() {
  canvas->fillRect(40, 18, 160, 100, COL_BAR);
  canvas->drawRect(40, 18, 160, 100, COL_AMBER);
  int rowH = launcherOk ? 11 : 10;
  for (int i = 0; i < kPageCount; i++) {
    bool grey = (i == kExitPage && !launcherOk);
    canvas->setCursor(50, 22 + i * rowH);
    if (grey) {
      canvas->setTextColor(COL_DIM);
      canvas->print("  ");
      canvas->print(kPageName[i]);
      continue;
    }
    if (i == page) {
      canvas->setTextColor(COL_AMBER);
      canvas->print("> ");
    } else {
      canvas->setTextColor(COL_TEXT);
      canvas->print("  ");
    }
    canvas->print(kPageName[i]);
  }
  if (!launcherOk) {
    canvas->setTextColor(COL_DIM);
    canvas->setCursor(48, 104);
    canvas->print("Launcher not found");
  }
}

void uiDraw(BleMidi &ble) {
  if (!canvas) {
    return;
  }
  audioReadSnap(&snap);
  canvas->fillSprite(COL_BG);
  statusBar(ble);
  switch (page) {
    case 0: drawPlay(); break;
    case 1: drawInst(); break;
    case 2: drawMix(); break;
    case 3: drawSong(); break;
    case 4: drawLoops(); break;
    case 5: drawMidi(ble); break;
    case 6: drawSettings(); break;
    default: drawExit(); break;
  }
  drawToast();
  if (overlay) {
    drawOverlay();
  }
  if (canvas->getBuffer()) {
    canvas->pushSprite(0, 0);
  }
}

static void assignInstrument() {
  if (cursor < 12) {
    audioCommand('I', cursor);
    toastSet(InstrumentBank::BuiltinName(cursor));
    return;
  }
  int index = cursor - 12;
  char err[48];
  int id = instrumentBank.Load(index, err, (int)sizeof(err));
  if (id < 0) {
    toastSet(err[0] ? err : "Load failed");
    return;
  }
  audioCommand('I', id);
  toastSet(instrumentBank.At(index).name);
}

static void doSave() {
  SongData song;
  if (!audioCapture(&song)) {
    toastSet("Capture failed");
    return;
  }
  instrumentBank.FillSongRefs(&song);
  char err[48];
  SdResult r = storage.Save(songSlot, song);
  (void)err;
  toastSet(SdStorage::ResultText(r, songSlot));
}

static void doLoad() {
  SongData song;
  SdResult r = storage.Load(songSlot, &song);
  if (r != SD_LOADED) {
    toastSet(SdStorage::ResultText(r, songSlot));
    return;
  }
  char err[48];
  instrumentBank.PrepareSong(&song, err, (int)sizeof(err));
  if (err[0]) {
    toastSet(err);
  }
  char lerr[48];
  lerr[0] = 0;
  loopLibrary.PrepareSong(&song, lerr, (int)sizeof(lerr));
  audioApplySong(song);
  for (int t = 0; t < 4; t++) {
    LoopArm arm;
    if (loopLibrary.TrackArm(t, &arm)) {
      audioArmLoop(t, arm);
    }
  }
  if (!err[0]) {
    toastSet(lerr[0] ? lerr : SdStorage::ResultText(r, songSlot));
  }
}

static void launchLoop(bool audition) {
  if (loopLibrary.Count() <= 0) {
    toastSet("No libraries");
    return;
  }
  uint8_t steps[64];
  int count = 0;
  bool pattern = false;
  LoopArm arm;
  char err[48];
  if (!loopLibrary.LoadEntry(loopLib, loopRow, &arm, steps, &count, &pattern, err, (int)sizeof(err))) {
    toastSet(err[0] ? err : "Loop failed");
    return;
  }
  if (pattern) {
    audioWritePattern(snap.track, steps, count);
    toastSet("Pattern on track");
    return;
  }
  arm.quantize = (uint8_t)quantize;
  if (audition) {
    audioAudition(arm);
    toastSet("Audition");
  } else {
    audioArmLoop(snap.track, arm);
    toastSet(quantize == 0 ? "Loop" : "Loop queued");
  }
}

static void handleChar(char c, bool ctrl, bool shift, bool alt, bool opt, bool fn) {
  if (fn) {
    if (c == ';') {
      if (page == 1) {
        cursor = clampi(cursor - 1, 0, instCount() - 1);
      } else if (page == 2) {
        mixRow = clampi(mixRow - 1, 0, kMixRows - 1);
      } else if (page == 4) {
        loopRow = clampi(loopRow - 1, 0, 7);
      } else if (page == 6) {
        cursor = clampi(cursor - 1, 0, 5);
      }
      return;
    }
    if (c == '.') {
      if (page == 1) {
        cursor = clampi(cursor + 1, 0, instCount() - 1);
      } else if (page == 2) {
        mixRow = clampi(mixRow + 1, 0, kMixRows - 1);
      } else if (page == 4) {
        int n = 0;
        if (loopLibrary.Count() > 0) {
          n = loopLibrary.At(loopLib).entryCount - 1;
        }
        loopRow = clampi(loopRow + 1, 0, n < 0 ? 0 : n);
      } else if (page == 6) {
        cursor = clampi(cursor + 1, 0, 5);
      }
      return;
    }
    if (c == ',') {
      if (page == 2) {
        tweakMix(-1);
      } else if (page == 4 && loopLibrary.Count() > 0) {
        loopLib = (loopLib + loopLibrary.Count() - 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == 6 && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol - 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == 6 && cursor == 1) {
        bright = (uint8_t)clampi((int)bright - 8, 10, 255);
        M5Cardputer.Display.setBrightness(bright);
        savePrefs();
      }
      return;
    }
    if (c == '/') {
      if (page == 2) {
        tweakMix(1);
      } else if (page == 4 && loopLibrary.Count() > 0) {
        loopLib = (loopLib + 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == 6 && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol + 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == 6 && cursor == 1) {
        bright = (uint8_t)clampi((int)bright + 8, 10, 255);
        M5Cardputer.Display.setBrightness(bright);
        savePrefs();
      }
      return;
    }
    if (c == '-' || c == '=') {
      int d = c == '-' ? -12 : 12;
      outVol = (uint8_t)clampi((int)outVol + d, 0, 255);
      audioSetSpeakerVolume(outVol);
      savePrefs();
      toastSet(c == '-' ? "Quieter" : "Louder");
      return;
    }
  }

  if (ctrl && c >= '1' && c <= '4') {
    audioCommand('^', c - '1');
    return;
  }
  if (ctrl && c >= '5' && c <= '8') {
    audioCommand('#', c - '5');
    return;
  }
  if (ctrl && c == 'n') {
    audioCommand('X', lengthChoice & 3);
    toastSet("New song");
    return;
  }

  if (c >= '1' && c <= '4') {
    if (page == 3) {
      songSlot = c - '1';
      audioCommand('Z', 0);
      bool has = false;
      storage.Status(songSlot, &has);
      toastSet(has ? "Slot full" : "Slot empty");
    } else {
      audioCommand('T', c - '1');
    }
    return;
  }
  if (c >= '5' && c <= '8' && page != 3) {
    audioCommand('$', c - '5');
    return;
  }
  if (c == '9') {
    if (page == 1) {
      cursor = clampi(cursor - 1, 0, instCount() - 1);
    } else {
      audioCommand('B', (snap.bpmSlot + 3) & 3);
    }
    return;
  }
  if (c == '0') {
    if (page == 1) {
      cursor = clampi(cursor + 1, 0, instCount() - 1);
    } else {
      audioCommand('B', (snap.bpmSlot + 1) & 3);
    }
    return;
  }
  if (c == '-' || c == '=') {
    audioCommand('b', c == '=' ? 1 : -1);
    return;
  }

  if (page == 0) {
    int pitch = 0;
    int add = 0;
    if (pianoKey(c, &pitch, &add)) {
      int oct = clampi((int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0), 0, 3);
      if (oct != (int)snap.octave) {
        audioCommand('O', oct);
        audioCommand('N', pitch);
        audioCommand('O', snap.octave);
      } else {
        audioCommand('N', pitch);
      }
      return;
    }
    if (c == 'a') audioCommand('A', 1);
    else if (c == 'f') audioCommand('A', 2);
    else if (c == 'k') audioCommand('A', 3);
    else if (c == 'l') audioCommand('D', 0);
    else if (c == ';') audioCommand('L', (snap.envLen) & 3);
    else if (c == '\'') audioCommand('*', 3);
    else if (c == '\\') audioCommand('O', ((int)snap.octave + 1) & 3);
    else if (c == '.') audioCommand('*', 0);
    else if (c == '/') audioCommand('*', 1);
    return;
  }

  if (page == 1) {
    if (c == 'r') {
      instrumentBank.Scan();
      toastSet(sdCard.Mounted() ? "Rescanned" : "No SD card");
    }
    return;
  }
  if (page == 3) {
    if (c == 's') doSave();
    else if (c == 'l') doLoad();
    else if (c == 'x') toastSet(SdStorage::ResultText(storage.Delete(songSlot), songSlot));
    else if (c == 't') {
      bool has = false;
      SdResult st = storage.Status(songSlot, &has);
      if (st == SD_OK) {
        toastSet(has ? "Slot full" : "Slot empty");
      } else {
        toastSet(SdStorage::ResultText(st, songSlot));
      }
    } else if (c == 'n') {
      lengthChoice = (lengthChoice + 1) & 3;
      audioCommand('X', lengthChoice);
      toastSet("New song");
    } else if (c == 'c') audioCommand('*', 0);
    else if (c == 'v') audioCommand('*', 1);
    else if (c == 'g') audioCommand('*', 2);
    else if (c == 'm') audioCommand('C', 0);
    else if (c == 'h') audioCommand('H', 0);
    else if (c == 'b') audioCommand('B', (snap.bpmSlot + 1) & 3);
    return;
  }
  if (page == 4) {
    if (c == 'a') launchLoop(true);
    else if (c == 'q') {
      quantize = (quantize + 1) % 3;
      toastSet(quantize == 0 ? "Quantize now" : (quantize == 1 ? "Quantize beat" : "Quantize bar"));
    } else if (c == 's') audioStopLoop(snap.track);
    else if (c == 'r') {
      loopLibrary.Scan();
      toastSet("Rescanned loops");
    }
    return;
  }
  if (page == 5 && c == 'e') {
    page = 6;
    cursor = 2;
    naming = true;
    snprintf(edit, sizeof(edit), "%s", bleName);
  }
}

static KeyEvent eventFrom(const Keyboard_Class::KeysState &st) {
  KeyEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.tab = st.tab;
  ev.enter = st.enter;
  ev.del = st.del;
  ev.space = st.space;
  ev.fn = st.fn;
  ev.shift = st.shift;
  ev.ctrl = st.ctrl;
  ev.alt = st.alt;
  ev.opt = st.opt;
  for (char raw : st.word) {
    if (raw == ' ') {
      continue;
    }
    ev.ch = (char)tolower((unsigned char)raw);
    break;
  }
  return ev;
}

static void applyModal(const ModalAction &action, char typed, BleMidi &ble) {
  if (action.pageDelta) {
    page = stepPage(page, action.pageDelta);
  }
  if (action.cancelToPlay) {
    page = 0;
    overlay = false;
    naming = false;
  }
  if (action.closeMenu) {
    overlay = false;
  }
  if (action.backspace && edit[0]) {
    edit[strlen(edit) - 1] = 0;
  }
  if (action.append && typed && typed != ' ') {
    int n = (int)strlen(edit);
    if (n < 16) {
      edit[n] = typed;
      edit[n + 1] = 0;
    }
  }
  if (action.cancelName) {
    naming = false;
  }
  if (action.applyName) {
    if (edit[0]) {
      snprintf(bleName, sizeof(bleName), "%s", edit);
      savePrefs();
      ble.Restart(bleName);
      toastSet("BLE name set");
    }
    naming = false;
  }
  if (action.confirmExit) {
    requestExit();
  }
}

static void handleState(const Keyboard_Class::KeysState &st, BleMidi &ble) {
  if (activeModal(overlay, naming, page == kExitPage) != MODAL_NONE) {
    return;
  }
  if (st.tab) {
    page = stepPage(page, (st.shift || st.ctrl) ? -1 : 1);
    overlay = false;
    naming = false;
    return;
  }
  if (st.enter) {
    if (page == 1) assignInstrument();
    else if (page == 2) tweakMix(1);
    else if (page == 3) doLoad();
    else if (page == 4) launchLoop(false);
    else if (page == 6 && cursor == 2) {
      naming = true;
      snprintf(edit, sizeof(edit), "%s", bleName);
    }
    return;
  }
  if (st.del) {
    if (page == 0) {
      audioCommand('_', 0);
    } else if (page != 0) {
      page = 0;
      overlay = false;
    }
    return;
  }
  if (st.space) {
    audioCommand('P', 0);
    return;
  }
  for (char raw : st.word) {
    if (raw == ' ') {
      continue;
    }
    char c = (char)tolower((unsigned char)raw);
    handleChar(c, st.ctrl, st.shift, st.alt, st.opt, st.fn);
  }
}

void uiPoll(BleMidi &ble) {
  auto &kb = M5Cardputer.Keyboard;
  bool esc = kb.isKeyPressed('`');
  bool btn = M5Cardputer.BtnA.isPressed();
  if (esc || btn) {
    if (!escDown) {
      escSince = millis();
      escDown = true;
      escFired = false;
    } else if (!escFired && (uint32_t)(millis() - escSince) > 700) {
      escFired = true;
      if (launcherInstalled()) {
        launcherOk = true;
        requestExit();
      } else {
        launcherOk = false;
        if (page == kExitPage) {
          page = 0;
        }
        toastSet("Launcher not found");
      }
    }
  } else {
    escDown = false;
    escFired = false;
  }
  if (kb.isChange() && kb.isPressed()) {
    Keyboard_Class::KeysState st = kb.keysState();
    KeyEvent ev = eventFrom(st);
    ModalKind kind = activeModal(overlay, naming, page == kExitPage);
    if (kind != MODAL_NONE) {
      ModalAction action;
      dispatchModal(kind, ev, &action);
      applyModal(action, ev.ch, ble);
      return;
    }
    if (ev.ch == '`' && !ev.fn) {
      if (page == 0) {
        overlay = !overlay;
      } else {
        page = 0;
        overlay = false;
      }
      return;
    }
    handleState(st, ble);
  }
}
