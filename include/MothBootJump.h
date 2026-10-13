#ifndef MothBootJump_h
#define MothBootJump_h

#include <stdbool.h>

// Count boots that do not confirm. The third one returns to MothBoot.
void mothBootGuard();

// Confirm this OTA app so a failed boot can fall back to MothBoot.
void mothMarkAppValid();

// Select the factory partition and restart. Returns false when this
// image was not built with a factory slot.
bool mothRebootToLauncher();

#endif
