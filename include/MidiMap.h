#ifndef MidiMap_h
#define MidiMap_h
#include <stdint.h>
#include "DspHot.h"

// Synth notes are 4 octaves of 12 pitches, anchored at MIDI C2 (note 36).

inline void midiNoteToSynth(int midiNote, int &pitch, int &octave) {
  if (midiNote < 36) {
    midiNote = 36;
  } else if (midiNote > 83) {
    midiNote = 83;
  }
  int rel = midiNote - 36;
  pitch = rel % 12;
  octave = rel / 12;
}

inline int synthToMidiNote(int pitch, int octave) {
  if (pitch < 0) {
    pitch = 0;
  } else if (pitch > 11) {
    pitch = 11;
  }
  if (octave < 0) {
    octave = 0;
  } else if (octave > 3) {
    octave = 3;
  }
  return 36 + octave * 12 + pitch;
}

// bend14 is the MIDI pitch-wheel value, center 8192.
inline int scaleByBend(int value, int bend14) {
  if (value < 1) {
    return 1;
  }
  if (bend14 < 0) {
    bend14 = 0;
  } else if (bend14 > 16383) {
    bend14 = 16383;
  }
  if (bend14 == 8192) {
    return value;
  }
  int centered = bend14 - 8192;
  // value is a step or a note increment, well under 40000, so the product fits MULL.
  int scaled = dspMulQ(value, 32768 + centered, 15);
  if (scaled < 1) {
    return 1;
  }
  return scaled;
}

// 14-bit volume 0..16383 maps onto the voice multiplier 0..8.
inline int volumeFrom14(int value14) {
  if (value14 < 0) {
    value14 = 0;
  } else if (value14 > 16383) {
    value14 = 16383;
  }
  return (value14 * 8 + 8191) / 16383;
}

inline int volumeTo14(int volume) {
  if (volume < 0) {
    volume = 0;
  } else if (volume > 8) {
    volume = 8;
  }
  return (volume * 16383) / 8;
}

// Bank MSB alone selects instrument 0..11 (MothOS) and, on MothDeck,
// 12..63 for SD plugins. Above 63 clamps to the last built-in.
// A non-zero LSB still spreads the 14-bit bank value across the 12
// built-in instruments, same as MothOS.
inline int instrumentFromBank(int msb, int lsb, bool lsbArrived) {
  if (msb < 0) {
    msb = 0;
  } else if (msb > 127) {
    msb = 127;
  }
  if (!lsbArrived || lsb == 0) {
    if (msb > 63) {
      return 11;
    }
    return msb;
  }
  if (lsb < 0) {
    lsb = 0;
  } else if (lsb > 127) {
    lsb = 127;
  }
  int value14 = (msb << 7) | lsb;
  int inst = (value14 * 12) / 16384;
  if (inst > 11) {
    inst = 11;
  }
  return inst;
}

#endif
