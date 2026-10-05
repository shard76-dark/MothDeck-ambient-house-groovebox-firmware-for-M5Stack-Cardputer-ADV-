#ifndef DefaultSamples_h
#define DefaultSamples_h
#include <stdint.h>
// Unsigned 8-bit mono at 22050 Hz. 128 is silence. See tools/gen_samples.py.
// Playback expands each byte to 16-bit and stretches 22050 to the mix rate.

extern const uint8_t kick1[];
extern const int kick1Length;
extern const uint8_t snare1[];
extern const int snare1Length;
extern const uint8_t special1[];
extern const int special1Length;
extern const uint8_t hihat1[];
extern const int hihat1Length;
extern const uint8_t kick2[];
extern const int kick2Length;
extern const uint8_t snare2[];
extern const int snare2Length;
extern const uint8_t special2[];
extern const int special2Length;
extern const uint8_t hihat2[];
extern const int hihat2Length;
extern const uint8_t kick3[];
extern const int kick3Length;
extern const uint8_t snare3[];
extern const int snare3Length;
extern const uint8_t special3[];
extern const int special3Length;
extern const uint8_t hihat3[];
extern const int hihat3Length;
extern const uint8_t sfx1[];
extern const int sfx1Length;
extern const uint8_t sfx2[];
extern const int sfx2Length;
extern const uint8_t sfx3[];
extern const int sfx3Length;
extern const uint8_t sfx4[];
extern const int sfx4Length;
extern const uint8_t sfx5[];
extern const int sfx5Length;
extern const uint8_t sfx6[];
extern const int sfx6Length;
extern const uint8_t sfx7[];
extern const int sfx7Length;
extern const uint8_t sfx8[];
extern const int sfx8Length;
extern const uint8_t sfx9[];
extern const int sfx9Length;
extern const uint8_t sfx10[];
extern const int sfx10Length;
extern const uint8_t sfx11[];
extern const int sfx11Length;
extern const uint8_t sfx12[];
extern const int sfx12Length;

#endif
