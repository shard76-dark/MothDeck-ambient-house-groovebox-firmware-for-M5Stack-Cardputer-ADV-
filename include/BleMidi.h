#ifndef BleMidi_h
#define BleMidi_h
#include "MidiProtocol.h"

struct MidiCounters {
  uint32_t packets;
  uint32_t messages;
  uint32_t clocks;
  uint32_t notes;
  uint32_t overflows;
};

class BleMidi {
public:
  BleMidi();
  // advertise false still initialises the stack. It does not advertise.
  bool Begin(const char *name, bool advertise = true);
  bool Restart(const char *name);
  // Radio only. The stack stays resident, so this cannot fail for memory.
  void SetUserEnabled(bool on);
  bool UserEnabled() const;
  bool Resident() const;
  // Boot skipped BLE, or init never succeeded.
  void MarkSkipped();
  // Stack was not started. The switch reads Off, not unloaded.
  void MarkIdle();
  // Negotiated connection interval, or "link --" while disconnected.
  const char *LinkLine();
  // Unload is stored for the next boot. The stack is not torn down now.
  void SetPendingUnload(bool pending);
  // "On adv", "On conn", "Off", "unloaded", "reboot unload", or "no mem".
  const char *SwitchLine();
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
  // Live free internal heap, largest free block, and whether advertising
  // is active. Fits on the MIDI page and does not need a serial cable.
  const char *DiagLine();
  void CopyCounters(MidiCounters *out) const;
  // One screen line: packets, messages, clocks, notes, overflows.
  const char *CounterLine();
  int Poll(MidiEvent *out, int maxOut);
  int ConsumeConnectEdge();
  void Send(const MidiEvent &event);

private:
  bool started;
};

#endif
