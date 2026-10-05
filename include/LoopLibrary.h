#ifndef LoopLibrary_h
#define LoopLibrary_h
#include "SongFile.h"
#include "Snap.h"
#include "LoopFormat.h"

struct LoopLibInfo {
  char folder[24];
  char name[32];
  int bpm;
  int bars;
  char tags[48];
  int entryCount;
  LoopEntry entries[8];
  char error[40];
};

class LoopLibrary {
public:
  static const int kMaxLibs = 8;

  void Scan();
  int Count() const { return count; }
  const LoopLibInfo &At(int index) const { return libs[index]; }
  // Fills arm from a library entry. Pattern files set pattern=true and steps.
  bool LoadEntry(int libIndex, int entryIndex, LoopArm *arm, uint8_t *steps, int *stepCount, bool *pattern, char *err, int errLen);
  bool PrepareSong(SongData *song, char *err, int errLen);
  // After PrepareSong, pointers for enabled tracks are ready for ArmLoop.
  bool TrackArm(int track, LoopArm *arm) const;

private:
  LoopLibInfo libs[kMaxLibs];
  int count;
  LoopArm loaded[kSongTracks];
  uint8_t loadedOk[kSongTracks];
};

extern LoopLibrary loopLibrary;

#endif
