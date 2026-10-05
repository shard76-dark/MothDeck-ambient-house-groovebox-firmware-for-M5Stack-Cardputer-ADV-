#ifndef AudioEngine_h
#define AudioEngine_h
#include "MidiProtocol.h"
#include "SongFile.h"
#include "Snap.h"

void audioStart();
bool audioRunning();
void audioCommand(char kind, int val);
void audioMidi(const MidiEvent &event);
bool audioCapture(SongData *song);
void audioApplySong(const SongData &song);
void audioArmLoop(int track, const LoopArm &arm);
void audioStopLoop(int track);
void audioAudition(const LoopArm &arm);
void audioStopAudition();
void audioWritePattern(int track, const uint8_t *steps, int count);
bool audioPopMidi(MidiEvent *event);
void audioReadSnap(Snap *out);
void audioSetSpeakerVolume(uint8_t volume);

#endif
