#ifndef MothCrc_h
#define MothCrc_h

#include <stddef.h>
#include <stdint.h>

// IEEE CRC-32, the same value as Python binascii.crc32 / zlib.crc32.
// Pass crc 0 to start. Pass the previous return value to continue.
uint32_t mothCrc32Update(uint32_t crc, const uint8_t *data, size_t len);

#endif
