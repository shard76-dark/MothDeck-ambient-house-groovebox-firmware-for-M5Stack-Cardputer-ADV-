#include "SongFile.h"
#include <string.h>
#include <ctype.h>

static void writeU16(uint8_t *dst, int &i, uint16_t value) {
  dst[i++] = (uint8_t)(value & 0xFF);
  dst[i++] = (uint8_t)((value >> 8) & 0xFF);
}

static uint16_t readU16(const uint8_t *src, int &i) {
  uint16_t value = (uint16_t)src[i] | ((uint16_t)src[i + 1] << 8);
  i += 2;
  return value;
}

static uint16_t checksum(const uint8_t *data, int len) {
  uint16_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum = (uint16_t)(sum + data[i]);
  }
  return sum;
}

static void writeVoice(uint8_t *dst, int &i, const SongVoice &voice) {
  dst[i++] = voice.volume;
  dst[i++] = voice.mute;
  dst[i++] = voice.samplerMode;
  dst[i++] = voice.overdrive;
  dst[i++] = (uint8_t)voice.octave;
  dst[i++] = voice.envelopeNum;
  dst[i++] = voice.envelopeLength;
  dst[i++] = voice.phaserMult;
  dst[i++] = voice.lowPassMult;
  dst[i++] = voice.reverbMult;
  dst[i++] = voice.chordMult;
  dst[i++] = voice.pitchMult;
  dst[i++] = voice.delayMult;
  dst[i++] = voice.whooshMult;
  writeU16(dst, i, voice.bend14);
}

static void readVoice(const uint8_t *src, int &i, SongVoice *voice) {
  voice->volume = src[i++];
  voice->mute = src[i++];
  voice->samplerMode = src[i++];
  voice->overdrive = src[i++];
  voice->octave = (int8_t)src[i++];
  voice->envelopeNum = src[i++];
  voice->envelopeLength = src[i++];
  voice->phaserMult = src[i++];
  voice->lowPassMult = src[i++];
  voice->reverbMult = src[i++];
  voice->chordMult = src[i++];
  voice->pitchMult = src[i++];
  voice->delayMult = src[i++];
  voice->whooshMult = src[i++];
  voice->bend14 = readU16(src, i);
}

static bool nameOk(const char *s) {
  bool ended = false;
  int dots = 0;
  for (int i = 0; i < kPluginNameLen; i++) {
    unsigned char c = (unsigned char)s[i];
    if (ended) {
      if (c != 0) {
        return false;
      }
      continue;
    }
    if (c == 0) {
      ended = true;
      continue;
    }
    if (c == '.') {
      dots++;
      if (dots > 1) {
        return false;
      }
    } else {
      dots = 0;
    }
    if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  if (!ended) {
    return false;
  }
  if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) {
    return false;
  }
  return true;
}

bool songNeedsV2(const SongData &song) {
  if (song.pluginCount > 0 || song.currentVoice > 11) {
    return true;
  }
  for (int t = 0; t < kSongTracks; t++) {
    if (song.loops[t].enabled) {
      return true;
    }
    for (int s = 0; s < kSongSteps; s++) {
      if (song.instruments[t][s] > 11) {
        return true;
      }
    }
  }
  return false;
}

static int v2Payload(const SongData &song) {
  int count = song.pluginCount;
  if (count < 0) {
    count = 0;
  } else if (count > kSongPluginSlots) {
    count = kSongPluginSlots;
  }
  // pluginCount byte + refs + 4 loop records
  return kSongV1Payload + 1 + count * (1 + kPluginNameLen) + kSongTracks * (2 + kPluginNameLen * 2);
}

int songEncodedSize(const SongData &song) {
  if (!songNeedsV2(song)) {
    return kSongFileBytes;
  }
  return v2Payload(song) + 2;
}

static int writeBody(const SongData &song, uint8_t *dst, uint8_t version) {
  int i = 0;
  dst[i++] = 'M';
  dst[i++] = 'O';
  dst[i++] = 'T';
  dst[i++] = 'H';
  dst[i++] = version;
  writeU16(dst, i, song.patternLength);
  dst[i++] = song.masterVolume;
  dst[i++] = song.bpmSlot;
  for (int b = 0; b < 4; b++) {
    dst[i++] = song.bpms[b];
  }
  dst[i++] = song.currentVoice;
  dst[i++] = song.selectedTrack;
  dst[i++] = song.currentPattern;
  dst[i++] = song.allPatternPlay;
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      dst[i++] = song.tracks[t][s];
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      dst[i++] = (uint8_t)song.octaves[t][s];
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      dst[i++] = song.instruments[t][s];
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    writeVoice(dst, i, song.voices[t]);
  }
  return i;
}

int songEncode(const SongData &song, uint8_t *dst, int dstLen) {
  if (!dst) {
    return 0;
  }
  bool v2 = songNeedsV2(song);
  int need = v2 ? songEncodedSize(song) : kSongFileBytes;
  if (dstLen < need) {
    return 0;
  }
  int i = writeBody(song, dst, v2 ? kSongVersionV2 : kSongVersion);
  if (!v2) {
    if (i != kSongFileBytes - 2) {
      return 0;
    }
    writeU16(dst, i, checksum(dst, i));
    return i == kSongFileBytes ? i : 0;
  }
  int count = song.pluginCount;
  if (count > kSongPluginSlots) {
    count = kSongPluginSlots;
  }
  dst[i++] = (uint8_t)count;
  for (int p = 0; p < count; p++) {
    if (song.plugins[p].id != 0 && (song.plugins[p].id < 12 || song.plugins[p].id > 63)) {
      return 0;
    }
    if (!nameOk(song.plugins[p].name)) {
      return 0;
    }
    dst[i++] = song.plugins[p].id;
    memcpy(dst + i, song.plugins[p].name, kPluginNameLen);
    i += kPluginNameLen;
  }
  for (int t = 0; t < kSongTracks; t++) {
    if (song.loops[t].quantize > 2) {
      return 0;
    }
    if (!nameOk(song.loops[t].library) || !nameOk(song.loops[t].name)) {
      return 0;
    }
    dst[i++] = song.loops[t].enabled ? 1 : 0;
    dst[i++] = song.loops[t].quantize;
    memcpy(dst + i, song.loops[t].library, kPluginNameLen);
    i += kPluginNameLen;
    memcpy(dst + i, song.loops[t].name, kPluginNameLen);
    i += kPluginNameLen;
  }
  if (i != need - 2) {
    return 0;
  }
  writeU16(dst, i, checksum(dst, i));
  return i == need ? i : 0;
}

static bool readBody(const uint8_t *src, int &i, SongData *song, bool v2) {
  song->patternLength = readU16(src, i);
  song->masterVolume = src[i++];
  song->bpmSlot = src[i++];
  for (int b = 0; b < 4; b++) {
    song->bpms[b] = src[i++];
  }
  song->currentVoice = src[i++];
  song->selectedTrack = src[i++];
  song->currentPattern = src[i++];
  song->allPatternPlay = src[i++];
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      song->tracks[t][s] = src[i++];
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      song->octaves[t][s] = (int8_t)src[i++];
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    for (int s = 0; s < kSongSteps; s++) {
      song->instruments[t][s] = src[i++];
      if (v2 && song->instruments[t][s] > 63) {
        return false;
      }
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    readVoice(src, i, &song->voices[t]);
  }
  return true;
}

bool songDecode(const uint8_t *src, int srcLen, SongData *song) {
  if (!src || !song || srcLen < kSongFileBytes) {
    return false;
  }
  if (src[0] != 'M' || src[1] != 'O' || src[2] != 'T' || src[3] != 'H') {
    return false;
  }
  uint8_t version = src[4];
  if (version != kSongVersion && version != kSongVersionV2) {
    return false;
  }
  memset(song, 0, sizeof(*song));
  if (version == kSongVersion) {
    if (srcLen != kSongFileBytes) {
      return false;
    }
    int payload = kSongFileBytes - 2;
    int checkIndex = payload;
    uint16_t stored = readU16(src, checkIndex);
    if (stored != checksum(src, payload)) {
      return false;
    }
    int i = 5;
    if (!readBody(src, i, song, false)) {
      return false;
    }
    if (i != payload) {
      return false;
    }
    if (song->patternLength == 0 || (uint32_t)song->patternLength * 4 > (uint32_t)kSongSteps) {
      return false;
    }
    if (song->bpmSlot > 3 || song->selectedTrack > 3 || song->currentPattern > 3 || song->currentVoice > 11) {
      return false;
    }
    return true;
  }

  if (srcLen > kSongFileBytesMax || srcLen < kSongV1Payload + 1 + kSongTracks * (2 + kPluginNameLen * 2) + 2) {
    return false;
  }
  int payload = srcLen - 2;
  int checkIndex = payload;
  uint16_t stored = readU16(src, checkIndex);
  if (stored != checksum(src, payload)) {
    return false;
  }
  int i = 5;
  if (!readBody(src, i, song, true)) {
    return false;
  }
  if (i != kSongV1Payload) {
    return false;
  }
  uint8_t count = src[i++];
  if (count > kSongPluginSlots) {
    return false;
  }
  int expect = kSongV1Payload + 1 + (int)count * (1 + kPluginNameLen) + kSongTracks * (2 + kPluginNameLen * 2);
  if (expect != payload) {
    return false;
  }
  song->pluginCount = count;
  for (int p = 0; p < count; p++) {
    song->plugins[p].id = src[i++];
    memcpy(song->plugins[p].name, src + i, kPluginNameLen);
    i += kPluginNameLen;
    if (song->plugins[p].id != 0 && (song->plugins[p].id < 12 || song->plugins[p].id > 63)) {
      return false;
    }
    if (!nameOk(song->plugins[p].name)) {
      return false;
    }
  }
  for (int t = 0; t < kSongTracks; t++) {
    song->loops[t].enabled = src[i++] ? 1 : 0;
    song->loops[t].quantize = src[i++];
    if (song->loops[t].quantize > 2) {
      return false;
    }
    memcpy(song->loops[t].library, src + i, kPluginNameLen);
    i += kPluginNameLen;
    memcpy(song->loops[t].name, src + i, kPluginNameLen);
    i += kPluginNameLen;
    if (!nameOk(song->loops[t].library) || !nameOk(song->loops[t].name)) {
      return false;
    }
  }
  if (i != payload) {
    return false;
  }
  if (song->patternLength == 0 || (uint32_t)song->patternLength * 4 > (uint32_t)kSongSteps) {
    return false;
  }
  if (song->bpmSlot > 3 || song->selectedTrack > 3 || song->currentPattern > 3 || song->currentVoice > 63) {
    return false;
  }
  return true;
}
