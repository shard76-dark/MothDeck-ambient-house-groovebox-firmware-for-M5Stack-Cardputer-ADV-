#ifndef AudioEngine_h
#define AudioEngine_h
#include "MidiProtocol.h"
#include "SongFile.h"
#include "Snap.h"
#include "PluginFormat.h"

class BleMidi;

void audioStart();
void audioBindMidi(BleMidi *ble);
bool audioRunning();
// "audio ok", or "audio off: no queue|no task|no speaker".
const char *audioFaultText();
void audioCommand(char kind, int val);
// Copied onto the selected track when the audio task handles command 'J'.
// The caller fills it before audioCommand, on the UI task.
void audioStagePatch(const PatchAssign &in);
void audioCopyStaged(PatchAssign *out);
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
