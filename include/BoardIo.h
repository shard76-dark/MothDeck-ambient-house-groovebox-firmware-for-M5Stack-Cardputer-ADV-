#ifndef BoardIo_h
#define BoardIo_h
#include <stdint.h>

// One key event. The Cardputer copies this out of the TCA8418. The TTGO
// fills it from the serial port or the 4x4 pad.
//
// The pad is rotated so its connector is on top. A firmware row is a
// membrane column, and a firmware column is a membrane row. Key 0 is the
// top-left key as you look at it.
inline int ttgoPadKey(int row, int col) {
  if (row < 0 || row > 3 || col < 0 || col > 3) {
    return -1;
  }
  return row * 4 + (3 - col);
}
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
