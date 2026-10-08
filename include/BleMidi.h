#ifndef BleMidi_h
#define BleMidi_h
#include "MidiProtocol.h"

class BleMidi {
public:
  BleMidi();
  bool Begin(const char *name);
  bool Restart(const char *name);
  // Stop and push the advertising payload again. Called after the large
  // allocations so a host reset during those cannot leave an empty advert.
  void RecommitAfter(const char *where);
  // If advertising has stopped, start it again. Rate limited.
  void Maintain();
  bool Connected() const;
  bool Advertising();
  // Fits a 40-column screen. "MIDI advertising" only while the controller
  // is actually advertising. Otherwise "BLE off: ..." with the reason.
  const char *StatusLine();
  // Free internal heap and largest block measured just before BLE init,
  // plus whether advertising is active now.
  const char *DiagLine();
  int Poll(MidiEvent *out, int maxOut);
  int ConsumeConnectEdge();
  void Send(const MidiEvent &event);

private:
  bool started;
};

#endif
