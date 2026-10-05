#include "SdStorage.h"
#include "SdCard.h"
#include "DevLog.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static uint8_t fileBuf[kSongFileBytesMax];

static void slotPath(int slot, char *out, int outLen) {
  snprintf(out, outLen, "/moth/slot%d.mos", slot + 1);
}

SdResult SdStorage::Save(int slot, const SongData &song) {
  if (slot < 0 || slot >= kSongSlots) {
    return SD_FAIL;
  }
  if (!sdCard.Ensure()) {
    return SD_NO_CARD;
  }
  sdCard.Mkdir("/moth");
  int n = songEncode(song, fileBuf, kSongFileBytesMax);
  if (n <= 0) {
    return SD_FAIL;
  }
  char path[32];
  slotPath(slot, path, (int)sizeof(path));
  if (!sdCard.WriteAll(path, fileBuf, n)) {
    return SD_FAIL;
  }
  DEV_LOGF("SD: saved %s (%d)\n", path, n);
  return SD_SAVED;
}

SdResult SdStorage::Load(int slot, SongData *song) {
  if (!song || slot < 0 || slot >= kSongSlots) {
    return SD_FAIL;
  }
  if (!sdCard.Ensure()) {
    return SD_NO_CARD;
  }
  char path[32];
  slotPath(slot, path, (int)sizeof(path));
  int n = 0;
  if (!sdCard.ReadAll(path, fileBuf, kSongFileBytesMax, &n)) {
    if (!sdCard.Exists(path)) {
      return SD_EMPTY;
    }
    return SD_BAD_FILE;
  }
  if (!songDecode(fileBuf, n, song)) {
    return SD_BAD_FILE;
  }
  DEV_LOGF("SD: loaded %s\n", path);
  return SD_LOADED;
}

SdResult SdStorage::Delete(int slot) {
  if (slot < 0 || slot >= kSongSlots) {
    return SD_FAIL;
  }
  if (!sdCard.Ensure()) {
    return SD_NO_CARD;
  }
  char path[32];
  slotPath(slot, path, (int)sizeof(path));
  if (!sdCard.Exists(path)) {
    return SD_EMPTY;
  }
  if (!sdCard.Remove(path)) {
    return SD_FAIL;
  }
  return SD_DELETED;
}

SdResult SdStorage::Status(int slot, bool *hasFile) {
  if (hasFile) {
    *hasFile = false;
  }
  if (slot < 0 || slot >= kSongSlots) {
    return SD_FAIL;
  }
  if (!sdCard.Ensure()) {
    return SD_NO_CARD;
  }
  char path[32];
  slotPath(slot, path, (int)sizeof(path));
  if (hasFile) {
    *hasFile = sdCard.Exists(path);
  }
  return SD_OK;
}

const char *SdStorage::ResultText(SdResult result, int slot) {
  static char buf[20];
  int shown = slot + 1;
  switch (result) {
    case SD_SAVED:
      snprintf(buf, sizeof(buf), "Saved S%d", shown);
      break;
    case SD_LOADED:
      snprintf(buf, sizeof(buf), "Loaded S%d", shown);
      break;
    case SD_DELETED:
      snprintf(buf, sizeof(buf), "Deleted S%d", shown);
      break;
    case SD_EMPTY:
      snprintf(buf, sizeof(buf), "Empty S%d", shown);
      break;
    case SD_NO_CARD:
      snprintf(buf, sizeof(buf), "No SD card");
      break;
    case SD_BAD_FILE:
      snprintf(buf, sizeof(buf), "Bad file");
      break;
    case SD_OK:
      snprintf(buf, sizeof(buf), "SD ok S%d", shown);
      break;
    default:
      snprintf(buf, sizeof(buf), "SD failed");
      break;
  }
  return buf;
}
