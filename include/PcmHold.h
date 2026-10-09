#ifndef PcmHold_h
#define PcmHold_h
#include <stdint.h>

// Sliding window over a WAV on the card. The audio task only reads the
// window. The main loop calls pcmHoldService(), which refills it.
// Ids are 1..kPcmHolds. 0 means "not a stream".

static const int kPcmHolds = 5;

// One block for every stream window. Prefer this before BLE, and only keep
// it when the largest free block is still at least 36KB. Otherwise release
// it, let BLE take its block, then pcmHoldReserveFit() sizes the windows
// to whatever contiguous RAM is left.
bool pcmHoldReservePreferred();
void pcmHoldReleaseReserve();
bool pcmHoldReserved();
void pcmHoldReserveFit();
int pcmHoldSlots();

int pcmHoldOpen(const char *path, int *frames, int *rate, char *err, int errLen);
void pcmHoldClose(int id);
void pcmHoldService();
void pcmHoldWant(int id, int frame);
int16_t pcmHoldAt(int id, int frame);

#endif
