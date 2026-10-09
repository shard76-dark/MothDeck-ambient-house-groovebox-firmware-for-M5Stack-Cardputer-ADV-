#ifndef LoopLibrary_h
#define LoopLibrary_h
#include "SongFile.h"
#include "Snap.h"
#include "LoopFormat.h"
#include "LoopInstrument.h"

struct LoopLibInfo {
  char folder[24];
  char name[32];
  int bpm;
  int bars;
  char tags[48];
  int entryCount;
  LoopEntry entries[kLoopEntryMax];
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
  // Loads audio loops into the stream cache for the Loops instrument.
  // Returns how many are ready. If some were skipped, LimitLine() says why.
  int PreloadInstrument(char *err, int errLen);
  // Close cached streams and the Loops instrument hits. The pool stays
  // until pcmHoldDropAll().
  void DropAudio();
  int ReadyCount() const { return ready; }
  int AudioCount() const { return audio; }
  const char *LimitLine() const { return limitMsg; }

private:
  LoopLibInfo libs[kMaxLibs];
  int count;
  LoopArm loaded[kSongTracks];
  uint8_t loadedOk[kSongTracks];
  int ready;
  int audio;
  char limitMsg[48];
};

extern LoopLibrary loopLibrary;

#endif
