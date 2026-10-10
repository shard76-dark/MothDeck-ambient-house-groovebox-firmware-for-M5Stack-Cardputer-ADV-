#ifndef SpeakerOut_h
#define SpeakerOut_h
#include <stdint.h>

// Cardputer: ES8311 through M5.Speaker. TTGO: PCM5102A I2S, or the
// internal 8-bit DAC on GPIO25 when MOTHDECK_INTERNAL_DAC is set.
bool speakerStart(uint8_t volume);
void speakerSetVolume(uint8_t volume);
// How many blocks the DMA still holds. The TTGO drivers block in the
// write, so this stays 0 and the audio task paces on the write itself.
int speakerQueued();
bool speakerWrite(const int16_t *mono, int count);

#endif
