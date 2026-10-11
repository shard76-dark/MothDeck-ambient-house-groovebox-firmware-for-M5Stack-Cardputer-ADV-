#ifndef MixCapture_h
#define MixCapture_h
#include <stdint.h>

// A short take of the master mix, kept in a fixed buffer. It does not
// allocate, so it does not take the loop windows, a kit cache, or BLE.
static const int kCaptureFrames = 8192;

void mixCaptureArm();
void mixCaptureFeed(int sample);
void mixCapturePlay(int stepNum, int stepDen);
int mixCaptureMix();
bool mixCaptureReady();
int mixCaptureLength();

#endif
