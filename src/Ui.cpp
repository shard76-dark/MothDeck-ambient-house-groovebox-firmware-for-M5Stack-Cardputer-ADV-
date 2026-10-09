#include "Ui.h"
#include "ModalInput.h"
#include "AudioEngine.h"
#include "InstrumentBank.h"
#include "LoopLibrary.h"
#include "DrumKit.h"
#include "SdStorage.h"
#include "SdCard.h"
#include "LauncherExit.h"
#include "BleAdvert.h"
#include "BleMidi.h"
#include "BoardConfig.h"
#include "DevLog.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <ctype.h>
#include <esp_heap_caps.h>
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

static const char *kPageName[] = {"Play", "Instrument", "FX", "Mixer", "Song", "Loops", "MIDI", "Settings", "Exit"};
static const int kPageCount = 9;
static const int kExitPage = 8;
static const int kFxPage = 2;
static const int kMixerPage = 3;
static const int kSongPage = 4;
static const int kLoopsPage = 5;
static const int kMidiPage = 6;
static const int kSettingsPage = 7;
static bool launcherOk = false;

static const char *kNoteName[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

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
// rgb332 rows are expanded here and pushed as RGB565. Keeping the strip in
// BSS means the panel update never mallocs a second full frame. An 8-bit
// pushSprite does that (LovyanGFX grows a DMA buffer toward 240*135*2) and
// that allocation reset the ADV once I2S DMA was running.
static const int kSpriteW = 240;
static const int kSpriteH = 135;
static const int kBlitRows = 8;
static uint16_t blitBuf[kSpriteW * kBlitRows];
static void pushCanvas();
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
  pushCanvas();
  delay(500);
  if (!exitToLauncher()) {
    launcherOk = false;
    toastSet("Launcher not found");
  }
}

const char *uiBleName() {
  return bleName;
}

static bool prefsLoaded = false;

void uiLoadPrefs() {
  if (prefsLoaded) {
    return;
  }
  prefs.begin("mothdeck", false);
  outVol = prefs.getUChar("vol", 160);
  bright = prefs.getUChar("bri", 180);
  String stored = prefs.getString("ble", MOTHDECK_BLE_NAME_DEFAULT);
  snprintf(bleName, sizeof(bleName), "%s", stored.c_str());
  if (!bleName[0]) {
    snprintf(bleName, sizeof(bleName), "%s", MOTHDECK_BLE_NAME_DEFAULT);
  }
  prefsLoaded = true;
}

static uint16_t rgb332to565(uint8_t c) {
  uint32_t r3 = c >> 5;
  uint32_t g3 = (c >> 2) & 7;
  uint32_t b2 = c & 3;
  uint32_t r5 = (r3 << 2) | (r3 >> 1);
  uint32_t g6 = (g3 << 3) | g3;
  uint32_t b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void pushCanvas() {
  if (!canvas || !canvas->getBuffer()) {
    return;
  }
  const uint8_t *src = static_cast<const uint8_t *>(canvas->getBuffer());
  bool prev = M5Cardputer.Display.getSwapBytes();
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.startWrite();
  for (int y = 0; y < kSpriteH; y += kBlitRows) {
    int rows = kSpriteH - y;
    if (rows > kBlitRows) {
      rows = kBlitRows;
    }
    for (int row = 0; row < rows; row++) {
      const uint8_t *in = src + (size_t)(y + row) * kSpriteW;
      uint16_t *out = blitBuf + row * kSpriteW;
      for (int x = 0; x < kSpriteW; x++) {
        out[x] = rgb332to565(in[x]);
      }
    }
    M5Cardputer.Display.pushImage(0, y, kSpriteW, rows, blitBuf);
  }
  M5Cardputer.Display.endWrite();
  M5Cardputer.Display.setSwapBytes(prev);
}

static void logUiHeap(const char *tag) {
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  DEV_LOGF("HEAP: %s free=%u largest=%u\n", tag, (unsigned)freeB, (unsigned)largest);
}

void uiBegin() {
  uiLoadPrefs();
  logUiHeap("before sprite");
  canvas = new M5Canvas(&M5Cardputer.Display);
  // rgb332, 240*135 = 32400 bytes, half of the RGB565 canvas. The strip
  // blit above is what makes that safe next to I2S: pushSprite would
  // allocate another ~32KB DMA buffer to expand the frame.
  canvas->setColorDepth(static_cast<uint8_t>(8));
  void *buf = canvas->createSprite(kSpriteW, kSpriteH);
  if (!buf) {
    Serial.println("UI: sprite alloc failed");
  } else {
    DEV_LOGF("UI: sprite 8-bit bytes=%u\n", (unsigned)(kSpriteW * kSpriteH));
  }
  logUiHeap("after sprite");
  canvas->setTextSize(1);
  canvas->setTextColor(COL_TEXT);
  M5Cardputer.Display.setBrightness(bright);
  audioSetSpeakerVolume(outVol);
  launcherOk = launcherInstalled();
}

void uiMountStorage() {
  instrumentBank.Scan();
  loopLibrary.Scan();
  drumKit.Scan();
  loopLibrary.PreloadInstrument(nullptr, 0);
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
  if ((id >= 0 && id <= 11) || id == kLoopsVoice) {
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
  unsigned bars = snap.barCount ? snap.barCount : 1;
  unsigned bar = snap.barIndex ? snap.barIndex : 1;
  canvas->printf("B%u/%u  P%u  Oct %u  %s", bar, bars, (unsigned)snap.pattern + 1, snap.octave,
                 snap.songMode ? "Song" : "Patt");
  canvas->setCursor(2, 94);
  canvas->setTextColor(COL_TEXT);
  canvas->print(snap.hint);
  canvas->setCursor(2, 106);
  canvas->setTextColor(COL_DIM);
  canvas->print("Z row piano   Q row +oct");
  if (audioFaultText()[0] && strcmp(audioFaultText(), "audio ok") != 0) {
    legend(audioFaultText());
  } else {
    legend("Spc play  Fn ,/ bar  Tab page");
  }
}

static int instCount() {
  return 13 + instrumentBank.Count();
}

static void instLabel(int index, char *dst, int n) {
  if (index < 12) {
    snprintf(dst, n, "%02d %s", index, InstrumentBank::BuiltinName(index));
    return;
  }
  if (index == 12) {
    snprintf(dst, n, "12 Loops");
    return;
  }
  const PluginInfo &p = instrumentBank.At(index - 13);
  snprintf(dst, n, "%s %s", p.loaded ? "*" : " ", p.name[0] ? p.name : p.folder);
}

static bool rowIsTrackVoice(int idx) {
  int id = snap.trackVoice[snap.track];
  if (idx < 12) {
    return id == idx;
  }
  if (idx == 12) {
    return id == kLoopsVoice;
  }
  if (idx - 13 >= instrumentBank.Count()) {
    return false;
  }
  const PluginInfo &p = instrumentBank.At(idx - 13);
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
  for (int row = 0; row < 5; row++) {
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
    if (idx >= 13) {
      const PluginInfo &p = instrumentBank.At(idx - 13);
      if (p.error[0] && idx == cursor) {
        canvas->print(" ");
        canvas->print(p.error);
      }
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 100);
  if (cursor == 12) {
    canvas->print("Notes play SD loops, synced");
  } else {
    canvas->printf("Kit %.14s   , /", drumKit.Name(drumKit.Selected()));
  }
  legend(", / load kit   Ent assign   R scan");
}

static const char *kFxLabel[] = {
  "Filter", "Cutoff", "Res", "Delay", "Feedback", "Mix", "Reverb", "Crush", "Drive", "Chorus", "Tremolo"
};
static const int kFxRows = 11;

static int packFx(int row, int dir) {
  return (row & 0xFF) | ((dir & 0xFF) << 8);
}

static int fxValue(int row) {
  switch (row) {
    case 0: return snap.fxFilter;
    case 1: return snap.fxCutoff;
    case 2: return snap.fxRes;
    case 3: return snap.fxDelay;
    case 4: return snap.fxFb;
    case 5: return snap.fxMix;
    case 6: return snap.fxRev;
    case 7: return snap.fxCrush;
    case 8: return snap.fxDrive;
    case 9: return snap.fxChorus;
    default: return snap.fxTrem;
  }
}

static void tweakFx(int dir) {
  audioCommand('f', packFx(mixRow, dir));
}

static void fxText(int row, char *dst, int n) {
  int v = fxValue(row);
  if (row == 0) {
    snprintf(dst, n, "%s", v == 1 ? "low pass" : (v == 2 ? "high pass" : "off"));
  } else if (row == 3) {
    snprintf(dst, n, "%s", v == 1 ? "1/32" : (v == 2 ? "1/16" : (v == 3 ? "1/8" : "off")));
  } else {
    snprintf(dst, n, "%d", v);
  }
}

static void drawFx() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->printf("FX  track %d  %s", snap.track + 1, snap.inst);
  int top = mixRow - 2;
  if (top < 0) {
    top = 0;
  }
  for (int row = 0; row < 7; row++) {
    int idx = top + row;
    if (idx >= kFxRows) {
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
    canvas->print(kFxLabel[idx]);
    char value[12];
    fxText(idx, value, (int)sizeof(value));
    canvas->setCursor(120, y);
    canvas->print(value);
  }
  legend("1-4 track  Fn ,/ value  Ent +");
}

static void drawMixer() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Mixer");
  if (snap.solo) {
    canvas->print("  solo");
  }
  for (int t = 0; t < 4; t++) {
    int x = 6 + t * 58;
    bool sel = t == (int)snap.track;
    if (sel) {
      canvas->fillRect(x, 28, 56, 90, COL_BAR);
      canvas->drawRect(x, 28, 56, 90, COL_TRACK[t]);
    }
    char name[10];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    canvas->setTextColor(sel ? COL_TRACK[t] : COL_TEXT);
    canvas->setCursor(x + 4, 32);
    canvas->printf("%d %.5s", t + 1, name);
    int fy = 46;
    int fh = 52;
    canvas->drawRect(x + 22, fy, 12, fh, COL_DIM);
    int vol = snap.vol[t];
    if (vol < 0) {
      vol = 0;
    } else if (vol > 8) {
      vol = 8;
    }
    int h = vol * (fh - 2) / 8;
    canvas->fillRect(x + 23, fy + fh - 1 - h, 10, h, sel ? COL_AMBER : COL_TRACK[t]);
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(x + 4, 104);
    canvas->printf("vol %d", vol);
    if (snap.mute[t]) {
      canvas->setTextColor(COL_WARN);
      canvas->print(" M");
    }
  }
  legend("Fn ;/. vol   Fn ,/ track   M S");
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
  canvas->printf("Bars %u/8  %s  Mstr %u", snap.barCount ? snap.barCount : 1, snap.songMode ? "song" : "patt",
                 2 - snap.masterVolume);
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
  bool onAir = ble.Advertising();
  if (ble.Connected()) {
    canvas->setTextColor(COL_PLAY);
  } else if (onAir) {
    canvas->setTextColor(COL_AMBER);
  } else {
    canvas->setTextColor(COL_WARN);
  }
  canvas->setCursor(2, 16);
  canvas->print(ble.StatusLine());
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 28);
  canvas->print(ble.DiagLine());
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 40);
  BleAdvertPackets advert;
  buildBleMidiAdvert(bleName, MOTHDECK_BLE_NAME_DEFAULT, &advert);
  if (advert.nameShortened) {
    canvas->printf("Name %s (%s)", bleName, advert.advName);
  } else {
    canvas->printf("Name %s", bleName);
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 54);
  canvas->print("Ch 1-4 = tracks");
  canvas->setCursor(2, 66);
  canvas->print("Notes 36-83  C2-B5");
  canvas->setCursor(2, 78);
  canvas->print("CC0/32 bank  CC1 low pass");
  canvas->setCursor(2, 90);
  canvas->print("CC7/39 volume  bend pitch");
  canvas->setCursor(2, 102);
  MidiCounters ctr;
  ble.CopyCounters(&ctr);
  canvas->printf("pk %lu  msg %lu  ovf %lu", (unsigned long)ctr.packets, (unsigned long)ctr.messages, (unsigned long)ctr.overflows);
  canvas->setCursor(2, 114);
  canvas->printf("clk %lu  note %lu", (unsigned long)ctr.clocks, (unsigned long)ctr.notes);
  legend("E edit name in Settings");
}

static void drawSettings(BleMidi &ble) {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Settings");
  const char *rows[] = {"Speaker", "Brightness", "BLE name", "Battery", "Free RAM", "Card", "Bars"};
  int top = cursor > 3 ? cursor - 3 : 0;
  for (int i = 0; i < 5; i++) {
    int idx = top + i;
    if (idx > 6) {
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
      unsigned freeKb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
      unsigned blkKb = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
      const char *bleWord = ble.Connected() ? "conn" : (ble.Advertising() ? "adv" : "off");
      canvas->printf("%uk blk %uk %s", freeKb, blkKb, bleWord);
    } else if (idx == 5) {
      canvas->print(sdCard.Mounted() ? "mounted" : "none");
    } else {
      canvas->printf("%u", snap.barCount ? snap.barCount : 1);
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(4, 96);
  canvas->print(ble.CounterLine());
  canvas->setCursor(4, 108);
  canvas->printf("v%s  %s", MOTHDECK_VERSION, BOARD_NAME);
  if (!naming && cursor == 4) {
    legend(ble.StatusLine());
  } else {
    legend(naming ? "Ent apply  Bksp  ` cancel" : (cursor == 6 ? "Fn ,/ bars" : "Lf/Rt change  Ent name"));
  }
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
  int rowH = launcherOk ? 10 : 9;
  for (int i = 0; i < kPageCount; i++) {
    bool grey = (i == kExitPage && !launcherOk);
    canvas->setCursor(48, 21 + i * rowH);
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
    canvas->setCursor(48, 21 + kPageCount * rowH);
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
    case kFxPage: drawFx(); break;
    case kMixerPage: drawMixer(); break;
    case kSongPage: drawSong(); break;
    case kLoopsPage: drawLoops(); break;
    case kMidiPage: drawMidi(ble); break;
    case kSettingsPage: drawSettings(ble); break;
    default: drawExit(); break;
  }
  drawToast();
  if (overlay) {
    drawOverlay();
  }
  if (canvas->getBuffer()) {
    pushCanvas();
  }
}

static void assignInstrument() {
  if (cursor < 12) {
    audioCommand('I', cursor);
    toastSet(InstrumentBank::BuiltinName(cursor));
    return;
  }
  if (cursor == 12) {
    char err[48];
    err[0] = 0;
    int n = loopLibrary.PreloadInstrument(err, (int)sizeof(err));
    audioCommand('I', kLoopsVoice);
    if (n <= 0) {
      toastSet(err[0] ? err : "No loop samples");
    } else {
      toastSet("Loops");
    }
    return;
  }
  int index = cursor - 13;
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

static void cycleKit(int dir) {
  int n = drumKit.Count();
  if (n < 1) {
    n = 1;
  }
  int s = drumKit.Selected();
  s = (s + dir + n) % n;
  char err[48];
  if (!drumKit.Select(s, err, (int)sizeof(err))) {
    toastSet(err[0] ? err : "Kit failed");
    return;
  }
  if (n < 2) {
    toastSet("No SD kits");
    return;
  }
  toastSet(drumKit.Name(s));
}

static void handleChar(char c, bool ctrl, bool shift, bool alt, bool opt, bool fn) {
  if (fn) {
    if (c == ';') {
      if (page == 1) {
        cursor = clampi(cursor - 1, 0, instCount() - 1);
      } else if (page == kFxPage) {
        mixRow = clampi(mixRow - 1, 0, kFxRows - 1);
      } else if (page == kMixerPage) {
        audioCommand('v', clampi((int)snap.vol[snap.track] + 1, 0, 8));
      } else if (page == kLoopsPage) {
        loopRow = clampi(loopRow - 1, 0, 7);
      } else if (page == kSettingsPage) {
        cursor = clampi(cursor - 1, 0, 6);
      }
      return;
    }
    if (c == '.') {
      if (page == 1) {
        cursor = clampi(cursor + 1, 0, instCount() - 1);
      } else if (page == kFxPage) {
        mixRow = clampi(mixRow + 1, 0, kFxRows - 1);
      } else if (page == kMixerPage) {
        audioCommand('v', clampi((int)snap.vol[snap.track] - 1, 0, 8));
      } else if (page == kLoopsPage) {
        int n = 0;
        if (loopLibrary.Count() > 0) {
          n = loopLibrary.At(loopLib).entryCount - 1;
        }
        loopRow = clampi(loopRow + 1, 0, n < 0 ? 0 : n);
      } else if (page == kSettingsPage) {
        cursor = clampi(cursor + 1, 0, 6);
      }
      return;
    }
    if (c == ',') {
      if (page == 1) {
        cycleKit(-1);
      } else if (page == kFxPage) {
        tweakFx(-1);
      } else if (page == kMixerPage) {
        audioCommand('T', (snap.track + 3) & 3);
      } else if (page == kLoopsPage && loopLibrary.Count() > 0) {
        loopLib = (loopLib + loopLibrary.Count() - 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == kSettingsPage && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol - 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 1) {
        bright = (uint8_t)clampi((int)bright - 8, 10, 255);
        M5Cardputer.Display.setBrightness(bright);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 6) {
        int bars = snap.barCount ? (int)snap.barCount : 1;
        audioCommand('Y', clampi(bars - 1, 1, 8));
      } else if (page == 0) {
        audioCommand('W', -1);
      }
      return;
    }
    if (c == '/') {
      if (page == 1) {
        cycleKit(1);
      } else if (page == kFxPage) {
        tweakFx(1);
      } else if (page == kMixerPage) {
        audioCommand('T', (snap.track + 1) & 3);
      } else if (page == kLoopsPage && loopLibrary.Count() > 0) {
        loopLib = (loopLib + 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == kSettingsPage && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol + 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 1) {
        bright = (uint8_t)clampi((int)bright + 8, 10, 255);
        M5Cardputer.Display.setBrightness(bright);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 6) {
        int bars = snap.barCount ? (int)snap.barCount : 1;
        audioCommand('Y', clampi(bars + 1, 1, 8));
      } else if (page == 0) {
        audioCommand('W', 1);
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
    int bars = snap.barCount ? (int)snap.barCount : 1;
    audioCommand('X', bars - 1);
    toastSet("New song");
    return;
  }

  if (c >= '1' && c <= '4') {
    if (page == kSongPage) {
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
  if (c >= '5' && c <= '8' && page != kSongPage) {
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
      drumKit.Scan();
      loopLibrary.Scan();
      loopLibrary.PreloadInstrument(nullptr, 0);
      toastSet(sdCard.Mounted() ? "Rescanned" : "No SD card");
    } else if (c == ',') {
      cycleKit(-1);
    } else if (c == '/') {
      cycleKit(1);
    }
    return;
  }
  if (page == kSongPage) {
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
      int bars = snap.barCount ? (int)snap.barCount : 1;
      bars = bars >= 8 ? 1 : bars + 1;
      audioCommand('X', bars - 1);
      toastSet("New song");
    } else if (c == 'c') audioCommand('*', 0);
    else if (c == 'v') audioCommand('*', 1);
    else if (c == 'g') audioCommand('*', 2);
    else if (c == 'm') audioCommand('C', 0);
    else if (c == 'h') audioCommand('H', 0);
    else if (c == 'b') audioCommand('B', (snap.bpmSlot + 1) & 3);
    return;
  }
  if (page == kMixerPage) {
    if (c == 'm') audioCommand('V', 0);
    else if (c == 's') audioCommand('V', 3);
    return;
  }
  if (page == kLoopsPage) {
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
  if (page == kMidiPage && c == 'e') {
    page = kSettingsPage;
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
    else if (page == kFxPage) tweakFx(1);
    else if (page == kSongPage) doLoad();
    else if (page == kLoopsPage) launchLoop(false);
    else if (page == kSettingsPage && cursor == 2) {
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
    if (!audioRunning()) {
      toastSet(audioFaultText());
    }
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
