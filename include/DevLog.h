#ifndef DevLog_h
#define DevLog_h

#include "BoardConfig.h"

// MOTHOS_DEV_LOG compiles serial logs for the SD card and the speaker.
// At 0 the Serial calls are removed from the translation unit.
#if MOTHOS_DEV_LOG
#include <Arduino.h>
#define DEV_LOG(msg) Serial.println(msg)
#define DEV_LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define DEV_LOG(msg) do { } while (0)
#define DEV_LOGF(...) do { } while (0)
#endif

#endif
