#ifndef SfxCard_h
#define SfxCard_h
#include <stdint.h>

// Flash length of built-in effect `note` (0..11). Zero means the table is
// a placeholder and the PCM is /moth/sfx/<name>.wav.
int sfxFlashLength(int note);

// Copies card effects into PSRAM. A missing card leaves those effects
// silent. Drums, loops, and BLE are not unloaded.
void sfxCardScan();

// True when this effect was loaded from the card. Host builds use a stub.
bool sfxCardHit(int note, const int16_t **data, int *length);

#endif
