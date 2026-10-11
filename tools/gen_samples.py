#!/usr/bin/env python3
"""Bake the built-in kit and the twelve sound effects.

Tables are signed 16-bit mono at 44100 Hz, rendered by tools/voices.py
at that rate. They are not an expansion of the old 8-bit tables.
Tonal instruments are rendered in real time by src/ToneSynth.cpp.
Regenerate with: python3 tools/gen_samples.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import clamp16, write_wav  # noqa: E402
from voices import SFX_NAMES, TTGO_CARD_SFX, ambient_kit, sfx_bank  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "DefaultSamples.h")
OUT_C = os.path.join(ROOT, "src", "default_samples.cpp")
RATE = 44100

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
        "// Signed 16-bit mono at 44100 Hz. Rendered by tools/gen_samples.py.",
        "// These tables are the mix rate, so a drum plays one frame per output sample.",
        "// A length of 0 is a placeholder. That effect is /moth/sfx/<name>.wav.",
        "",
    ]
    body = ['#include "DefaultSamples.h"', ""]
    for name, data, frames in tables:
        lines.append("extern const int16_t %s[];" % name)
        lines.append("extern const int %sLength;" % name)
        body.append("const int16_t %s[] = {" % name)
        row = []
        for sample in data:
            row.append(str(clamp16(sample)))
            if len(row) == 16:
                body.append("  " + ",".join(row) + ",")
                row = []
        if row:
            body.append("  " + ",".join(row))
        body.append("};")
        body.append("const int %sLength = %d;" % (name, frames))
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
    card = set(TTGO_CARD_SFX)
    for symbol, data in zip(DRUM_SYMBOLS, drums):
        tables.append((symbol, data, len(data)))
    sfx_dir = os.path.join(ROOT, "sd-card-example", "moth", "sfx")
    os.makedirs(sfx_dir, exist_ok=True)
    listed = ["mothdeck-sfx 1", "rate=%d" % RATE]
    for i, data in enumerate(sfx):
        name = "sfx%d" % (i + 1)
        if i in card:
            tables.append((name, [0], 0))
            filename = "%s.wav" % SFX_NAMES[i]
            write_wav(os.path.join(sfx_dir, filename), data, RATE)
            listed.append("sfx=%s" % filename)
        else:
            tables.append((name, data, len(data)))
    with open(os.path.join(sfx_dir, "manifest.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(listed) + "\n")
    emit(tables)
    print("wrote %d tables, drums %s" % (len(tables), ",".join(str(len(d)) for d in drums)))
    print("sfx", ",".join("%s:%d" % (n, 0 if i in card else len(d)) for i, (n, d) in enumerate(zip(SFX_NAMES, sfx))))


if __name__ == "__main__":
    main()
