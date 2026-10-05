#ifndef BleMidi_h
#define BleMidi_h
#include "MidiProtocol.h"

class BleMidi {
public:
  BleMidi();
  bool Begin(const char *name);
  bool Restart(const char *name);
  bool Connected() const;
  int Poll(MidiEvent *out, int maxOut);
  int ConsumeConnectEdge();
  void Send(const MidiEvent &event);

private:
  bool started;
};

#endif
