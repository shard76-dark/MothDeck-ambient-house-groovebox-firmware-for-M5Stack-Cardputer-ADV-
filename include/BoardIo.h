#ifndef BoardIo_h
#define BoardIo_h
#include <stdint.h>

// One key event. The Cardputer copies this out of the TCA8418. The TTGO
// fills it from the serial port or the optional button matrix.
struct DeckKeys {
  bool tab;
  bool fn;
  bool shift;
  bool ctrl;
  bool opt;
  bool alt;
  bool del;
  bool enter;
  bool space;
  char word[8];
  int wordLen;
};

void boardUpdate();
bool boardPollKeys(DeckKeys *keys);
bool boardEscHeld();
bool boardBtnHeld();

void boardPushBegin();
void boardPushEnd();
void boardPushImage(int x, int y, int w, int h, const uint16_t *pixels);
void boardSetBrightness(uint8_t level);
int boardBatteryPct();
int boardBatteryMv();
bool boardHasDisplay();

// TTGO panel bring-up. The Cardputer display is started from main.
void boardBringUpTtgo();

#endif
