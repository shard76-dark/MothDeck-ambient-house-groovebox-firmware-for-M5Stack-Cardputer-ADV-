#include "WavPcm.h"
#include <string.h>

static uint16_t ru16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ru32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wavErr(WavInfo *out, const char *msg) {
  if (!out) {
    return;
  }
  out->rate = 0;
  out->frames = 0;
  strncpy(out->error, msg, sizeof(out->error) - 1);
  out->error[sizeof(out->error) - 1] = 0;
}

bool parseWavHeader(const uint8_t *data, int len, WavInfo *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!data || len < 44) {
    wavErr(out, "WAV too small");
    return false;
  }
  if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
    wavErr(out, "Not a WAV");
    return false;
  }
  int pos = 12;
  bool haveFmt = false;
  int audioFormat = 0;
  int channels = 0;
  int rate = 0;
  int bits = 0;
  int dataOff = 0;
  int dataBytes = 0;
  while (pos + 8 <= len) {
    uint32_t chunkSize = ru32(data + pos + 4);
    if (chunkSize > 0x10000000u || pos + 8 + (int)chunkSize > len) {
      wavErr(out, "WAV chunk overrun");
      return false;
    }
    if (memcmp(data + pos, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        wavErr(out, "WAV fmt short");
        return false;
      }
      audioFormat = ru16(data + pos + 8);
      channels = ru16(data + pos + 10);
      rate = (int)ru32(data + pos + 12);
      bits = ru16(data + pos + 22);
      haveFmt = true;
    } else if (memcmp(data + pos, "data", 4) == 0) {
      dataOff = pos + 8;
      dataBytes = (int)chunkSize;
      break;
    }
    pos += 8 + (int)chunkSize + ((chunkSize & 1) ? 1 : 0);
  }
  if (!haveFmt || dataOff == 0) {
    wavErr(out, "WAV missing fmt or data");
    return false;
  }
  if (audioFormat != 1) {
    wavErr(out, "WAV is not PCM");
    return false;
  }
  if (!(channels == 1 || channels == 2) || !(bits == 8 || bits == 16)) {
    wavErr(out, "WAV must be 8/16-bit mono/stereo");
    return false;
  }
  if (rate < 1000 || rate > 96000) {
    wavErr(out, "WAV rate out of range");
    return false;
  }
  int frameBytes = channels * (bits / 8);
  if (frameBytes <= 0 || dataBytes < frameBytes || (dataBytes % frameBytes) != 0) {
    wavErr(out, "WAV data size mismatch");
    return false;
  }
  if (dataOff + dataBytes > len) {
    wavErr(out, "WAV truncated");
    return false;
  }
  out->rate = rate;
  out->channels = channels;
  out->bits = bits;
  out->frames = dataBytes / frameBytes;
  out->dataOffset = dataOff;
  return true;
}

bool parseWavPrefix(const uint8_t *data, int len, WavInfo *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!data || len < 44) {
    wavErr(out, "WAV too small");
    return false;
  }
  if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
    wavErr(out, "Not a WAV");
    return false;
  }
  int pos = 12;
  bool haveFmt = false;
  int audioFormat = 0;
  int channels = 0;
  int rate = 0;
  int bits = 0;
  int dataOff = 0;
  int dataBytes = 0;
  while (pos + 8 <= len) {
    uint32_t chunkSize = ru32(data + pos + 4);
    if (chunkSize > 0x10000000u) {
      wavErr(out, "WAV chunk overrun");
      return false;
    }
    if (memcmp(data + pos, "fmt ", 4) == 0) {
      if (chunkSize < 16 || pos + 8 + 16 > len) {
        wavErr(out, "WAV fmt short");
        return false;
      }
      audioFormat = ru16(data + pos + 8);
      channels = ru16(data + pos + 10);
      rate = (int)ru32(data + pos + 12);
      bits = ru16(data + pos + 22);
      haveFmt = true;
    } else if (memcmp(data + pos, "data", 4) == 0) {
      dataOff = pos + 8;
      dataBytes = (int)chunkSize;
      break;
    }
    int step = 8 + (int)chunkSize + ((chunkSize & 1) ? 1 : 0);
    if (pos + step > len) {
      wavErr(out, "WAV header truncated");
      return false;
    }
    pos += step;
  }
  if (!haveFmt || dataOff == 0) {
    wavErr(out, "WAV missing fmt or data");
    return false;
  }
  if (audioFormat != 1) {
    wavErr(out, "WAV is not PCM");
    return false;
  }
  if (!(channels == 1 || channels == 2) || !(bits == 8 || bits == 16)) {
    wavErr(out, "WAV must be 8/16-bit mono/stereo");
    return false;
  }
  if (rate < 1000 || rate > 96000) {
    wavErr(out, "WAV rate out of range");
    return false;
  }
  int frameBytes = channels * (bits / 8);
  if (frameBytes <= 0 || dataBytes < frameBytes) {
    wavErr(out, "WAV data size mismatch");
    return false;
  }
  out->rate = rate;
  out->channels = channels;
  out->bits = bits;
  out->frames = dataBytes / frameBytes;
  out->dataOffset = dataOff;
  return true;
}

int decodeWavMono(const uint8_t *data, int len, int16_t *dst, int dstFrames, WavInfo *out) {
  WavInfo info;
  WavInfo *w = out ? out : &info;
  if (!parseWavHeader(data, len, w)) {
    return -1;
  }
  if (!dst || dstFrames <= 0) {
    wavErr(w, "No decode buffer");
    return -1;
  }
  int frames = w->frames;
  if (frames > dstFrames) {
    wavErr(w, "WAV longer than cache");
    return -1;
  }
  const uint8_t *p = data + w->dataOffset;
  for (int i = 0; i < frames; i++) {
    int32_t acc = 0;
    for (int c = 0; c < w->channels; c++) {
      int32_t s;
      if (w->bits == 8) {
        s = ((int32_t)(*p++) - 128) << 8;
      } else {
        s = (int16_t)ru16(p);
        p += 2;
      }
      acc += s;
    }
    if (w->channels == 2) {
      acc /= 2;
    }
    if (acc > 32767) {
      acc = 32767;
    } else if (acc < -32768) {
      acc = -32768;
    }
    dst[i] = (int16_t)acc;
  }
  return frames;
}
