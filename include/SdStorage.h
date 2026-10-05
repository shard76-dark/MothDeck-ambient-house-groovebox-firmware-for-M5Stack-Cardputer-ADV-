#ifndef SdStorage_h
#define SdStorage_h
#include "SongFile.h"

enum SdResult {
  SD_OK = 0,
  SD_NO_CARD,
  SD_FAIL,
  SD_EMPTY,
  SD_SAVED,
  SD_LOADED,
  SD_DELETED,
  SD_BAD_FILE
};

class SdStorage {
public:
  SdResult Save(int slot, const SongData &song);
  SdResult Load(int slot, SongData *song);
  SdResult Delete(int slot);
  SdResult Status(int slot, bool *hasFile);
  static const char *ResultText(SdResult result, int slot);
};

#endif
