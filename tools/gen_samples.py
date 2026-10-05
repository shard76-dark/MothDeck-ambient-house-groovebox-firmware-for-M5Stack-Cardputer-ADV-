#!/usr/bin/env python3
"""Bake the built-in kit and the twelve sound effects.

Tables are unsigned 8-bit mono at 22050 Hz (128 is silence). Tonal
instruments are rendered in real time by src/ToneSynth.cpp.
Regenerate with: python3 tools/gen_samples.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import to_u8  # noqa: E402
from voices import DRUM_NAMES, SFX_NAMES, ambient_kit, sfx_bank  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "DefaultSamples.h")
OUT_C = os.path.join(ROOT, "src", "default_samples.cpp")
RATE = 22050

# Symbol order matches the keyboard: see the comment in src/Voice.cpp.
DRUM_SYMBOLS = (
    "kick1", "snare1", "special1", "hihat1",
    "kick2", "snare2", "special2", "hihat2",
    "kick3", "snare3", "special3", "hihat3",
)


def emit(tables):
    lines = [
        "#ifndef DefaultSamples_h",
        "#define DefaultSamples_h",
        "#include <stdint.h>",
        "// Unsigned 8-bit mono at 22050 Hz. 128 is silence. See tools/gen_samples.py.",
        "// Playback expands each byte to 16-bit and stretches 22050 to the mix rate.",
        "",
    ]
    body = ['#include "DefaultSamples.h"', ""]
    for name, data in tables:
        lines.append("extern const uint8_t %s[];" % name)
        lines.append("extern const int %sLength;" % name)
        body.append("const uint8_t %s[] = {" % name)
        row = []
        for sample in data:
            row.append(str(to_u8(sample)))
            if len(row) == 16:
                body.append("  " + ",".join(row) + ",")
                row = []
        if row:
            body.append("  " + ",".join(row))
        body.append("};")
        body.append("const int %sLength = %d;" % (name, len(data)))
        body.append("")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    with open(OUT_H, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    with open(OUT_C, "w", encoding="utf-8") as handle:
        handle.write("\n".join(body))


def main():
    tables = []
    drums = ambient_kit(RATE)
    sfx = sfx_bank(RATE)
    for symbol, data in zip(DRUM_SYMBOLS, drums):
        tables.append((symbol, data))
    for i, data in enumerate(sfx, start=1):
        tables.append(("sfx%d" % i, data))
    emit(tables)
    print("wrote %d tables, drums %s" % (len(tables), ",".join(str(len(d)) for d in drums)))
    print("sfx", ",".join("%s:%d" % (n, len(d)) for n, d in zip(SFX_NAMES, sfx)))


if __name__ == "__main__":
    main()
