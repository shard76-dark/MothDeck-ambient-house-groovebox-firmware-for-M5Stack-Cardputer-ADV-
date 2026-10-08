#include <Arduino.h>
#include <M5Cardputer.h>
#include "AudioEngine.h"
#include "BleMidi.h"
#include "BoardConfig.h"
#include "LauncherExit.h"
#include "PcmHold.h"
#include "SplashMoth.h"
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
  if (!launcherInstalled()) {
    M5Cardputer.Display.fillScreen(0x1082);
    M5Cardputer.Display.setTextColor(0x6B6D);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(8, 56);
    M5Cardputer.Display.println("Launcher not found");
    delay(700);
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
    M5Cardputer.Display.println("Launcher not found");
    delay(900);
  }
}

// M5.begin saves brightness before the panel exists (so the value is 0),
// clears the ST7789, then writes that 0 back. The backlight stays off and
// the panel stays black until something draws. External-display probes are
// left off so begin() cannot sit on the SPI bus the panel needs.
// internal_spk stays on: that is what installs the ADV I2S pins and the
// ES8311 enable callback. The codec itself is not started until audioStart(),
// which runs after the splash and the Play frame.
static void bringUpDisplay() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_spk = true;
  cfg.external_display_value = 0;
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  cfg.clear_display = true;
  M5Cardputer.begin(cfg, true);

  // M5.begin drives GPIO46 high before the board is known. On the ADV that
  // pin is the codec's ADC data line, not a power hold.
  pinMode(PIN_I2S_DIN, INPUT);

  // Stamp-S3A gates the backlight on GPIO38. PWM from a detected panel
  // owns the pin when autodetect worked; a plain high covers a missed detect.
  if (M5Cardputer.Display.width() < 200 || M5Cardputer.Display.height() < 120) {
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);
  }
  M5Cardputer.Display.setBrightness(200);
}

// One-second moth mark. The wait is bounded; it does not scan the card or
// start the speaker, and it returns even if no key is pressed.
static void showSplash() {
  uint16_t line[kSplashW];
  const uint16_t bg = 0x1082;
  const uint16_t ink = 0xFD20;
  const int rowBytes = kSplashW / 8;
  // pushImage of a uint16_t buffer is treated as already byte-swapped unless
  // this is set. Amber 0xFD20 then leaves the chip as 0x20FD, which this
  // panel shows as blue. The grey background barely changes when swapped.
  bool prevSwap = M5Cardputer.Display.getSwapBytes();
  M5Cardputer.Display.setSwapBytes(true);
  for (int y = 0; y < kSplashH; y++) {
    const uint8_t *row = kSplashMoth + y * rowBytes;
    for (int x = 0; x < kSplashW; x++) {
      bool on = row[x >> 3] & (uint8_t)(0x80 >> (x & 7));
      line[x] = on ? ink : bg;
    }
    M5Cardputer.Display.pushImage(0, y, kSplashW, 1, line);
  }
  M5Cardputer.Display.setSwapBytes(prevSwap);
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 1000) {
    delay(20);
  }
}

void setup() {
  // USB CDC is on from boot. The name is in NVS, which Arduino has already
  // opened. BLE has to come up before the 64KB sprite and the I2S DMA:
  // NimBLE takes its controller memory from internal SRAM, and this module
  // has no PSRAM. After those allocations the largest free block is often
  // too small, init returns false, and nothing goes on the air.
  Serial.begin(115200);
  Serial.println("MothDeck BLE boot");
  uiLoadPrefs();
  ble.Begin(uiBleName());
  bringUpDisplay();
  showSplash();
  uiBegin();
  uiDraw(ble);
  bootExitChord();
  uiMountStorage();
  audioStart();
  ble.RecommitAfter("audio");
}

void loop() {
  M5Cardputer.update();
  ble.Maintain();
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
  pcmHoldService();
  delay(16);
}
