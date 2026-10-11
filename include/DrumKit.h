#ifndef DrumKit_h
#define DrumKit_h
#include <stdint.h>

// One drum hit. rate 0 means 44100. The audio task only reads a published
// view; loading from the card happens on the UI thread.
struct DrumHitView {
  const int16_t *data;
  int length;
  int rate;
  // 1 when the pad is read from the card through the kit window.
  // data is null. The 16 KB cache is the window, not a copy of the kit.
  uint8_t streamed;
};

// Audio-thread read of a streamed pad. The host build uses the weak stub.
int drumStreamAt(int track, int pad, int frame);

// True when an SD kit is selected and this pad has PCM. False uses the
// built-in Ambient House tables.
bool drumKitHit(int note, DrumHitView *out);

class DrumKit {
public:
  static const int kMaxKits = 6;

  void Scan();
  // 1 + SD kits. Index 0 is the built-in Ambient House kit.
  int Count() const { return count; }
  const char *Name(int index) const;
  int Selected() const { return selected; }
  bool Select(int index, char *err, int errLen);

private:
  char folders[kMaxKits][24];
  char names[kMaxKits][32];
  int sdCount;
  int count;
  int selected;
};

extern DrumKit drumKit;

#endif
