#ifndef diag_stats_h
#define diag_stats_h

#include <stdint.h>
#include <stdbool.h>

// Windowed counters for the once-per-interval stats line, plus the session
// figures the status, bpm, and notes commands print. BPM from MIDI timestamps
// uses the 13-bit BLE MIDI millisecond field. BPM from arrival time uses the
// microsecond clock captured when the notification was queued.

struct DiagHeld {
  int ch;
  int note;
  int vel;
  uint64_t onUs;
};

struct DiagWindow {
  uint32_t windowMs;
  uint32_t elapsedMs;
  uint32_t clocks;
  uint32_t clocksTotal;
  bool hasBpmMidi;
  double bpmMidi;
  bool hasBpmArrival;
  double bpmArrival;
  uint32_t noteOn;
  uint32_t noteOff;
  uint32_t noteOnTotal;
  uint32_t noteOffTotal;
  uint32_t hanging;
  uint32_t extraOffs;
  uint32_t parseErrors;
  uint32_t parseErrorsTotal;
  uint32_t parserMismatch;
  uint32_t parserMismatchTotal;
  uint32_t packets;
  double pps;
  uint32_t maxGapUs;
  uint32_t jitterUs;
};

struct DiagBpm {
  uint32_t clocks;
  bool hasBpmMidi;
  double bpmMidi;
  bool hasBpmArrival;
  double bpmArrival;
  bool hasArrivalJitter;
  double arrivalJitterUs;
  bool hasMidiJitter;
  double midiJitterMs;
  uint32_t midiSpanMs;
  uint64_t arrivalSpanUs;
};

class DiagMeter {
public:
  DiagMeter();
  void reset(uint64_t nowUs);
  void setWindowMs(uint32_t ms);
  uint32_t windowMs() const;

  void noteOn(int ch, int note, int vel, uint64_t us);
  void noteOff(int ch, int note);
  void allOff(int ch);
  void clock(uint64_t us, uint16_t bleTs);
  void packet(uint64_t us);
  void parseError(int n);
  void noteParserMismatch();

  // Fills out and starts a new window when the interval has elapsed.
  bool takeWindow(uint64_t nowUs, DiagWindow *out);

  uint32_t noteOnTotal() const;
  uint32_t noteOffTotal() const;
  uint32_t clockTotal() const;
  uint32_t hanging() const;
  uint32_t extraOffs() const;
  uint32_t parseErrors() const;
  uint32_t parserMismatch() const;
  int copyHeld(DiagHeld *dst, int max) const;
  void bpmSummary(DiagBpm *out) const;

private:
  struct HeldSlot {
    uint8_t ch;
    uint8_t note;
    uint8_t vel;
    uint64_t onUs;
    bool used;
  };

  void addGap(uint32_t gapUs);
  void addSessionGap(double sample, double *mean, double *m2, uint32_t *n);
  int heldCount() const;
  void clearHeldChannel(int ch);

  uint32_t windowMs_;
  uint64_t windowStartUs_;
  uint64_t lastPacketUs_;
  bool havePacket_;

  uint32_t winPackets_;
  uint32_t winClocks_;
  uint32_t winNoteOn_;
  uint32_t winNoteOff_;
  uint32_t winErrors_;
  uint32_t winMismatch_;
  uint32_t gapCount_;
  uint32_t maxGapUs_;
  double gapMean_;
  double gapM2_;

  uint64_t winClockFirstUs_;
  uint64_t winClockLastUs_;
  uint32_t winMidiSpanMs_;
  uint16_t winPrevTs_;
  bool winHaveTs_;

  uint32_t clocks_;
  uint32_t noteOn_;
  uint32_t noteOff_;
  uint32_t extraOffs_;
  uint32_t errors_;
  uint32_t mismatch_;

  uint64_t sessClockFirstUs_;
  uint64_t sessClockLastUs_;
  bool sessHaveClock_;
  uint32_t sessMidiSpanMs_;
  uint16_t sessPrevTs_;
  bool sessHaveTs_;
  uint32_t sessArrivalN_;
  double sessArrivalMean_;
  double sessArrivalM2_;
  uint32_t sessMidiN_;
  double sessMidiMean_;
  double sessMidiM2_;

  uint8_t bits_[16][16];
  HeldSlot held_[128];
};

#endif
