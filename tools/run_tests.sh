#!/usr/bin/env bash
# Host checks for the MIDI codec, the song file, and the plugin/loop parsers.
# No board and no SDK required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -Werror -I"$ROOT/include")

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
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  -o /tmp/mothdeck_test_modal
/tmp/mothdeck_test_modal

echo "Per-track FX and mixer volume"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_fx.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
  -o /tmp/mothdeck_test_fx
/tmp/mothdeck_test_fx

echo "Per-track instruments"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_tracks.cpp" \
  "$ROOT/src/Tracker.cpp" \
  "$ROOT/src/Voice.cpp" \
  "$ROOT/src/ToneSynth.cpp" \
  "$ROOT/src/SongFile.cpp" \
  "$ROOT/src/default_samples.cpp" \
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

echo "Plugins, loops, WAV, synth render"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_plugins.cpp" \
  "$ROOT/src/PluginFormat.cpp" \
  "$ROOT/src/LoopFormat.cpp" \
  "$ROOT/src/WavPcm.cpp" \
  "$ROOT/src/SynthRender.cpp" \
  -o /tmp/mothdeck_test_plugins
/tmp/mothdeck_test_plugins
