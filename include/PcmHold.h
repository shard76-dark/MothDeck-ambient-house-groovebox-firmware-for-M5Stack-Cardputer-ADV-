#ifndef PcmHold_h
#define PcmHold_h
#include <stdint.h>

// Sliding window over a WAV on the card. The audio task only reads the
// window. The main loop calls pcmHoldService(), which refills it.
// Ids are 1..kPcmHolds. 0 means "not a stream".

static const int kPcmHolds = 5;

// One block for every stream window. Call this after NimBLE is resident.
// The windows are whatever contiguous RAM is left beside the stack.
// pcmHoldReserveFit() shrinks the windows when the preferred pool does not fit.
bool pcmHoldReservePreferred();
void pcmHoldReleaseReserve();
// Close every stream and free the pool, including slots that are open.
void pcmHoldDropAll();
bool pcmHoldReserved();
void pcmHoldReserveFit();
int pcmHoldSlots();

int pcmHoldOpen(const char *path, int *frames, int *rate, char *err, int errLen);
void pcmHoldClose(int id);
void pcmHoldService();
void pcmHoldWant(int id, int frame);
int16_t pcmHoldAt(int id, int frame);

#endif
