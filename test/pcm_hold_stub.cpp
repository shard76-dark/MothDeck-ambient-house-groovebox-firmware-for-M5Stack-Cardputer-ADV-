#include "PcmHold.h"

// Host tests link Tracker, which reads streamed loops. The firmware
// implementation talks to the SD card, so the tests get silence.
int16_t pcmHoldAt(int id, int frame) {
  (void)id;
  (void)frame;
  return 0;
}
