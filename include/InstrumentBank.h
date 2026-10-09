#ifndef InstrumentBank_h
#define InstrumentBank_h
#include <stdint.h>
#include "SongFile.h"
#include "PluginFormat.h"

// A sample the audio task may read. instrumentView is safe to call from
// the audio task: the UI publishes `data` only after the buffer is filled
// and clears `ready` before freeing it.
struct ExtSampleView {
  const int16_t *data;
  int length;
  int rate;
  int rootMidi;
  int loopStart;
  int loopEnd;
  uint8_t oneshot;
};

bool instrumentView(int id, ExtSampleView *out);

struct PluginInfo {
  char folder[24];
  char name[32];
  uint8_t id;
  uint8_t kind;
  uint8_t loaded;
  uint8_t isPatch;
  char error[40];
};

class InstrumentBank {
public:
  static const int kMaxPlugins = 12;

  void Scan();
  int Count() const { return count; }
  const PluginInfo &At(int index) const { return items[index]; }
  // Built-in names for ids 0..11. Empty string if unknown.
  static const char *BuiltinName(int id);
  // Loads the plugin if needed and returns its id (12..63), or -1.
  int Load(int index, char *err, int errLen, PatchAssign *assign = nullptr);
  int LoadFolder(const char *folder, char *err, int errLen, PatchAssign *assign = nullptr);
  void FillSongRefs(SongData *song) const;
  // Loads referenced plugins. Missing ones are remapped to drums (0).
  bool PrepareSong(SongData *song, char *err, int errLen);

private:
  PluginInfo items[kMaxPlugins];
  int count;
};

extern InstrumentBank instrumentBank;

#endif
