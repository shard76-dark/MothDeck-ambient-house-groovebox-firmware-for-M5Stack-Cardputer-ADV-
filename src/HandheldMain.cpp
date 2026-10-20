#include "BoardConfig.h"

#if MOTHDECK_BOARD_HANDHELD

#include "AudioEngine.h"
#include "BleMidi.h"
#include "HandheldIo.h"
#include "MothBootJump.h"
#include "PcmHold.h"
#include "Ui.h"
#include "UsbMidi.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

SET_LOOP_TASK_STACK_SIZE(16384);

static BleMidi ble;

static void consoleWrite(const char *text) {
  if (!text || !text[0]) {
    return;
  }
  Serial.print(text);
  Serial.flush();
}

static void consoleLine(const char *kind) {
  char line[96];
  snprintf(line, sizeof(line), "{\"t\":\"%s\",\"name\":\"MothDeck\",\"version\":\"%s\",\"board\":\"%s\"}\n", kind,
           MOTHDECK_VERSION, BOARD_NAME);
  consoleWrite(line);
}

static bool sameWord(const char *cmd, const char *word) {
  for (size_t i = 0; word[i]; i++) {
    char c = cmd[i];
    if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
    if (c != word[i]) {
      return false;
    }
  }
  return cmd[strlen(word)] == 0;
}

static void pollConsole() {
  static char cmd[32];
  static size_t used = 0;
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) {
      return;
    }
    if (ch == '\n') {
      while (used > 0 && (cmd[used - 1] == ' ' || cmd[used - 1] == '\r')) {
        used--;
      }
      cmd[used] = 0;
      used = 0;
      if (sameWord(cmd, "status") || sameWord(cmd, "version")) {
        consoleLine(cmd[0] == 'v' || cmd[0] == 'V' ? "version" : "status");
      } else if (sameWord(cmd, "help")) {
        consoleWrite("{\"t\":\"help\",\"commands\":[\"status\",\"help\",\"version\"]}\n");
      }
      continue;
    }
    if (ch == '\r') {
      continue;
    }
    if (used + 1 < sizeof(cmd)) {
      cmd[used++] = (char)ch;
    } else {
      used = 0;
    }
  }
}

void setup() {
  mothBootGuard();
  Serial.begin(115200);
  Serial.printf("MothDeck %s %s\n", MOTHDECK_VERSION, BOARD_NAME);
  uiLoadPrefs();
  handheldBringUp();
  audioBindMidi(&ble);
  audioStart();
  audioCommand('S', uiSyncMode());
  uiBegin();
  if (uiBleWantLoad()) {
    ble.Begin(uiBleName(), uiBleEnabled());
  } else {
    ble.MarkSkipped();
  }
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  Serial.printf("HEAP: free=%u\n", (unsigned)freeB);
  uiMountStorage(false);
  mothMarkAppValid();
  consoleLine("hello");
  uiDraw(ble);
}

void loop() {
  pollConsole();
  ble.Maintain();
  uiPoll(ble);
  usbMidiPoll(ble);
  MidiEvent outgoing;
  while (audioPopMidi(&outgoing)) {
    ble.Send(outgoing);
    usbMidiSend(outgoing);
  }
  static uint32_t lastDrawMs = 0;
  uint32_t now = millis();
  if (lastDrawMs == 0 || (uint32_t)(now - lastDrawMs) >= 40) {
    uiDraw(ble);
    lastDrawMs = now;
  }
  pcmHoldService();
  delay(4);
}

#endif
