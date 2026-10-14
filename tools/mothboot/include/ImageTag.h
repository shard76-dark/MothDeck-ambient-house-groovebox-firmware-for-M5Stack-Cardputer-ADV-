#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Windows overlap by more than a version string, so a tag that crosses
// the end of one read is completed on the next read.
static const size_t kMothImageWindow = 512;
static const size_t kMothImageStep = 480;

// Copy the version after a complete "MOTHIMG:" tag. A tag with no
// version, or a version that runs off the end of this buffer, is skipped.
inline bool mothImageVersion(const uint8_t *image, size_t len, char *dst, size_t dstLen) {
  if (!image || !dst || dstLen < 2) {
    return false;
  }
  const char *key = "MOTHIMG:";
  const size_t keyLen = 8;
  for (size_t i = 0; i + keyLen < len; i++) {
    if (memcmp(image + i, key, keyLen) != 0) {
      continue;
    }
    size_t k = 0;
    bool ended = false;
    for (size_t j = i + keyLen; j < len && k + 1 < dstLen; j++) {
      unsigned char c = image[j];
      if (c <= 32 || c > 126) {
        ended = true;
        break;
      }
      dst[k++] = (char)c;
    }
    if (!ended || k == 0) {
      continue;
    }
    dst[k] = 0;
    return true;
  }
  return false;
}

// Scan a whole image the same way the launcher scans a partition.
inline bool mothImageVersionScan(const uint8_t *image, size_t len, char *dst, size_t dstLen) {
  if (!image || !dst || dstLen < 2) {
    return false;
  }
  dst[0] = 0;
  for (size_t off = 0; off < len;) {
    size_t nread = len - off;
    if (nread > kMothImageWindow) {
      nread = kMothImageWindow;
    }
    if (mothImageVersion(image + off, nread, dst, dstLen)) {
      return true;
    }
    if (off + nread >= len) {
      break;
    }
    off += kMothImageStep;
  }
  return false;
}
