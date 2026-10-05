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

echo "Plugins, loops, WAV, synth render"
"$CXX" "${FLAGS[@]}" \
  "$ROOT/test/test_plugins.cpp" \
  "$ROOT/src/PluginFormat.cpp" \
  "$ROOT/src/LoopFormat.cpp" \
  "$ROOT/src/WavPcm.cpp" \
  "$ROOT/src/SynthRender.cpp" \
  -o /tmp/mothdeck_test_plugins
/tmp/mothdeck_test_plugins
