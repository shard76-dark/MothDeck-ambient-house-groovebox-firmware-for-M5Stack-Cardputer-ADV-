#ifndef CardFs_h
#define CardFs_h
#include "BoardConfig.h"

// The mounted card. TTGO uses the SDMMC host. Cardputer uses SPI SD.
#if MOTHDECK_BOARD_TTGO
#include <SD_MMC.h>
#define MOTH_FS SD_MMC
#else
#include <SD.h>
#define MOTH_FS SD
#endif

#endif
