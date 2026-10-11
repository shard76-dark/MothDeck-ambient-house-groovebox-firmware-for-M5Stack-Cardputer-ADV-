#!/usr/bin/env bash
# Host checks for the MIDI codec, the song file, and the plugin/loop parsers.
# No board and no SDK required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -Werror -I"$ROOT/include")

echo "DSP interpolation and dither"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_dsp.cpp" \
  -o /tmp/mothdeck_test_dsp
/tmp/mothdeck_test_dsp

echo "BLE MIDI advertising payload"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_ble_advert.cpp" \
  "$ROOT/src/BleAdvert.cpp" \
  -o /tmp/mothdeck_test_ble_advert
/tmp/mothdeck_test_ble_advert

echo "MIDI and song file"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_midi_song.cpp" \
  "$ROOT/src/MidiProtocol.cpp" \
  "$ROOT/src/SongFile.cpp" \
  -o /tmp/mothdeck_test_midi
/tmp/mothdeck_test_midi

echo "Launcher detection"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_launcher.cpp" \
  "$ROOT/src/LauncherDetect.cpp" \
  -o /tmp/mothdeck_test_launcher
/tmp/mothdeck_test_launcher

echo "Modal input"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_modal.cpp" \
  "$ROOT/src/ModalInput.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  "$ROOT/test/pcm_hold_stub.cpp" \
  -o /tmp/mothdeck_test_modal
/tmp/mothdeck_test_modal

echo "Per-track FX and mixer volume"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_fx.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  "$ROOT/test/pcm_hold_stub.cpp" \
  -o /tmp/mothdeck_test_fx
/tmp/mothdeck_test_fx

echo "TTGO keypad index"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_pad.cpp" \
  -o /tmp/mothdeck_test_pad
/tmp/mothdeck_test_pad

echo "TTGO chords"
"$CXX" "${FLAGS[@]}" -DMOTHDECK_CHORDS=1 \
  "$ROOT/test/test_chords.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  "$ROOT/test/pcm_hold_stub.cpp" \
  -o /tmp/mothdeck_test_chords
/tmp/mothdeck_test_chords

echo "Per-track instruments"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_tracks.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/MidiProtocol.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  "$ROOT/test/pcm_hold_stub.cpp" \
  -o /tmp/mothdeck_test_tracks
/tmp/mothdeck_test_tracks

echo "Drum kits"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_kit.cpp" \
  "$ROOT/src/KitFormat.cpp" \
  "$ROOT/src/PluginFormat.cpp" \
  -o /tmp/mothdeck_test_kit
/tmp/mothdeck_test_kit

echo "Voices"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_voices.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/default_samples.cpp" \
  -o /tmp/mothdeck_test_voices
/tmp/mothdeck_test_voices

echo "Card effects are 16-bit 44100 Hz"
python3 - "$ROOT" << 'PY'
import os, sys, wave
root = sys.argv[1]
names = ("riser", "downlifter", "sweep", "impact", "siren", "drop")
base = os.path.join(root, "sd-card-example", "moth", "sfx")
for name in names:
    path = os.path.join(base, name + ".wav")
    with wave.open(path, "r") as handle:
        assert handle.getnchannels() == 1, name
        assert handle.getsampwidth() == 2, name
        assert handle.getframerate() == 44100, name
        assert handle.getnframes() > 4000, name
        raw = handle.readframes(handle.getnframes())
    low = sum(1 for i in range(0, min(len(raw), 4000), 2) if raw[i] != 0)
    assert low > 100, name
print("card sfx ok")
PY

echo "Sampler take"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_sampler.cpp" \
  "$ROOT/src/MixCapture.cpp" \
  -o /tmp/mothdeck_test_sampler
/tmp/mothdeck_test_sampler

echo "Scenes and chain"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_chain.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/MidiProtocol.cpp" \
  "$ROOT/src/default_samples.cpp" \
  "$ROOT/test/pcm_hold_stub.cpp" \
  -o /tmp/mothdeck_test_chain
/tmp/mothdeck_test_chain

echo "Plugins, loops, WAV, synth render"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_plugins.cpp" \
  "$ROOT/src/PluginFormat.cpp" \
  "$ROOT/src/PatchBlocks.cpp" \
  "$ROOT/src/LoopFormat.cpp" \
  "$ROOT/src/WavPcm.cpp" \
  "$ROOT/src/SynthRender.cpp" \
  -o /tmp/mothdeck_test_plugins
/tmp/mothdeck_test_plugins
