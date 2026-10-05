#include <Arduino.h>
#include <M5Cardputer.h>
#include "AudioEngine.h"
#include "BleMidi.h"
#include "BoardConfig.h"
#include "LauncherExit.h"
#include "Ui.h"

// The UI reads the SD card and draws a full frame. The default loop stack
// is too small once those paths run.
SET_LOOP_TASK_STACK_SIZE(16384);

static BleMidi ble;

static bool exitChordHeld() {
  if (M5Cardputer.BtnA.isPressed()) {
    return true;
  }
  if (digitalRead(PIN_BTN_A) == LOW) {
    return true;
  }
  return M5Cardputer.Keyboard.isKeyPressed('`');
}

// Hold Esc (the ` key) or the front button through reset to leave before
// the audio task starts. A short tap still boots MothDeck.
static void bootExitChord() {
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  uint32_t start = millis();
  bool held = false;
  while ((uint32_t)(millis() - start) < 650) {
    M5Cardputer.update();
    if (exitChordHeld()) {
      held = true;
      break;
    }
    delay(15);
  }
  if (!held) {
    return;
  }
  uint32_t confirm = millis();
  while ((uint32_t)(millis() - confirm) < 350) {
    M5Cardputer.update();
    if (!exitChordHeld()) {
      return;
    }
    delay(15);
  }
  M5Cardputer.Display.fillScreen(0x1082);
  M5Cardputer.Display.setTextColor(0xEF5D);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setCursor(8, 48);
  M5Cardputer.Display.println("Exit to Launcher");
  M5Cardputer.Display.setCursor(8, 64);
  M5Cardputer.Display.println("Press Enter on the splash");
  delay(400);
  if (!exitToLauncher()) {
    M5Cardputer.Display.setCursor(8, 80);
    M5Cardputer.Display.println("No Launcher partition");
    delay(900);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_spk = true;
  M5Cardputer.begin(cfg, true);

  bootExitChord();
  uiBegin();
  audioStart();
  ble.Begin(uiBleName());
}

void loop() {
  M5Cardputer.update();
  uiPoll(ble);

  MidiEvent incoming[8];
  int count = ble.Poll(incoming, 8);
  for (int i = 0; i < count; i++) {
    audioMidi(incoming[i]);
  }

  MidiEvent outgoing;
  while (audioPopMidi(&outgoing)) {
    ble.Send(outgoing);
  }

  uiDraw(ble);
  delay(16);
}
