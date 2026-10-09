#!/usr/bin/env python3
"""Draw the MothDeck key maps from the Cardputer ADV 4x14 matrix.

Physical order is _key_value_map in M5Cardputer Keyboard.h (4 rows, 14 columns).
Actions are what src/Ui.cpp does with that key, including modifier quirks:
Ctrl or Shift substitutes value_second before the UI sees the character.

Run from the repo root: python3 tools/make_keymap.py
Writes docs/keymap-*.svg and docs/keymap-*.png.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

# fill, text
GROUP = {
    "piano": ("#f4efe6", "#1b1814"),
    "black": ("#2b3344", "#f4efe6"),
    "fx": ("#d86a28", "#1a0e06"),
    "track": ("#e2aa22", "#1c1403"),
    "pat": ("#6c5ce7", "#f6f3ff"),
    "tempo": ("#148f86", "#f2fffd"),
    "play": ("#2b9d55", "#f3fff7"),
    "nav": ("#3a78d4", "#f4f8ff"),
    "exit": ("#c44747", "#fff6f6"),
    "act": ("#c44784", "#fff5fa"),
    "mod": ("#4c5562", "#eef2f6"),
    "fn": ("#1a6a82", "#e8f7fb"),
    "idle": ("#262b32", "#8b939c"),
}

LEGEND = [
    ("piano", "White note"),
    ("black", "Black note"),
    ("fx", "Effect / edit"),
    ("track", "Track or slot"),
    ("pat", "Pattern"),
    ("tempo", "Tempo"),
    ("play", "Transport"),
    ("nav", "Page"),
    ("exit", "Exit"),
    ("act", "This page"),
    ("fn", "Fn layer"),
    ("mod", "Modifier"),
    ("idle", "No action"),
]

PHYS = [
    ["`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Del"],
    ["Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\"],
    ["Fn", "Shift", "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "Enter"],
    ["Ctrl", "Opt", "Alt", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "Space"],
]


def cell(action="Idle", group="idle", sub=""):
    return {"action": action, "group": group, "sub": sub}


def grid():
    return [[cell() for _ in row] for row in PHYS]


def put(g, r, c, action, group, sub=""):
    g[r][c] = cell(action, group, sub)


def apply_common(g, page):
    if page == "play":
        put(g, 0, 0, "Page list", "nav", "hold: Exit")
        put(g, 0, 13, "Clear step", "fx")
    else:
        put(g, 0, 0, "To Play", "nav", "hold: Exit")
        put(g, 0, 13, "To Play", "nav", "naming: delete")
    if page == "song":
        for i in range(4):
            put(g, 0, 1 + i, "Slot %d" % (i + 1), "track")
        for i in range(4):
            put(g, 0, 5 + i, "Idle", "idle", "not on Song")
    else:
        for i in range(4):
            put(g, 0, 1 + i, "Track %d" % (i + 1), "track")
        for i in range(4):
            put(g, 0, 5 + i, "Pattern %d" % (i + 1), "pat")
    if page == "inst":
        put(g, 0, 9, "Inst prev", "act")
        put(g, 0, 10, "Inst next", "act")
    else:
        put(g, 0, 9, "BPM slot −", "tempo")
        put(g, 0, 10, "BPM slot +", "tempo")
    put(g, 0, 11, "BPM −1", "tempo", "Fn: quieter")
    put(g, 0, 12, "BPM +1", "tempo", "Fn: louder")
    put(g, 1, 0, "Next page", "nav", "Shift/Ctrl: prev")
    put(g, 2, 0, "Fn layer", "fn")
    put(g, 3, 0, "Ctrl", "mod", "N: new song")
    put(g, 3, 13, "Play / Stop", "play")
    if page == "play":
        put(g, 2, 1, "+1 octave", "mod", "letter notes")
        put(g, 3, 1, "−1 octave", "mod", "letter notes")
        put(g, 3, 2, "+1 octave", "mod", "with Shift")
    else:
        put(g, 2, 1, "Shift", "mod", "with Tab: prev")
        put(g, 3, 1, "Opt", "mod")
        put(g, 3, 2, "Alt", "mod")


def paint_play(g):
    white = {
        (1, 1): "C",
        (1, 3): "D",
        (1, 5): "E",
        (1, 6): "F",
        (1, 8): "G",
        (1, 10): "A",
        (1, 12): "B",
        (3, 3): "C",
        (3, 4): "D",
        (3, 5): "E",
        (3, 6): "F",
        (3, 7): "G",
        (3, 8): "A",
        (3, 9): "B",
        (3, 10): "C",
    }
    black = {
        (1, 2): "C#",
        (1, 4): "D#",
        (1, 7): "F#",
        (1, 9): "G#",
        (1, 11): "A#",
        (2, 3): "C#",
        (2, 4): "D#",
        (2, 6): "F#",
        (2, 7): "G#",
        (2, 8): "A#",
    }
    for (r, c), name in white.items():
        extra = " +oct" if r == 1 or (r, c) == (3, 10) else ""
        put(g, r, c, name + extra, "piano")
    for (r, c), name in black.items():
        extra = " +oct" if r == 1 else ""
        put(g, r, c, name + extra, "black")
    put(g, 1, 13, "Octave", "fx", "cycle 0–3")
    put(g, 2, 2, "Low pass", "fx")
    put(g, 2, 5, "Retrig", "fx")
    put(g, 2, 9, "Wobble", "fx")
    put(g, 2, 10, "Echo", "fx")
    put(g, 2, 11, "Hold / len", "fx", "Fn: pitch +")
    put(g, 2, 12, "Sampler", "fx")
    put(g, 2, 13, "Roll", "act", "toggle steps")
    put(g, 3, 10, "C +oct", "piano", "Fn: step −")
    put(g, 3, 11, "Copy pat", "fx", "Fn: pitch −")
    put(g, 3, 12, "Paste pat", "fx", "Fn: step +")


def paint_inst(g):
    put(g, 1, 4, "Rescan", "act")
    put(g, 2, 11, "Fn: cursor −", "fn", "hold Fn")
    put(g, 2, 12, "—", "idle")
    put(g, 2, 13, "Assign", "act", "loads plugin")
    put(g, 3, 11, "Prev kit", "act", "Fn: cursor +")
    put(g, 3, 12, "Next kit", "act")


def paint_fx(g):
    put(g, 2, 11, "Fn: row −", "fn", "hold Fn")
    put(g, 2, 13, "Value +", "act")
    put(g, 3, 10, "Fn: value −", "fn", "hold Fn")
    put(g, 3, 11, "Fn: row +", "fn", "hold Fn")
    put(g, 3, 12, "Fn: value +", "fn", "hold Fn")


def paint_mixer(g):
    put(g, 2, 3, "Solo", "act")
    put(g, 2, 11, "Fn: vol +", "fn", "hold Fn")
    put(g, 3, 9, "Mute", "act")
    put(g, 3, 10, "Fn: track −", "fn", "hold Fn")
    put(g, 3, 11, "Fn: vol −", "fn", "hold Fn")
    put(g, 3, 12, "Fn: track +", "fn", "hold Fn")


def paint_song(g):
    put(g, 1, 5, "Status", "act")
    put(g, 2, 3, "Save", "act")
    put(g, 2, 6, "Paste all", "act")
    put(g, 2, 7, "Master", "act")
    put(g, 2, 10, "Load", "act")
    put(g, 2, 13, "Load slot", "act")
    put(g, 3, 4, "Delete", "act")
    put(g, 3, 5, "Copy", "act")
    put(g, 3, 6, "Paste", "act")
    put(g, 3, 7, "BPM slot +", "tempo")
    put(g, 3, 8, "New song", "act", "length steps")
    put(g, 3, 9, "Song / pat", "act")


def paint_loops(g):
    put(g, 1, 1, "Quantize", "act")
    put(g, 1, 4, "Rescan", "act")
    put(g, 2, 2, "Audition", "act")
    put(g, 2, 3, "Stop loop", "act")
    put(g, 2, 11, "Fn: row −", "fn", "hold Fn")
    put(g, 2, 13, "Launch", "act")
    put(g, 3, 10, "Fn: library −", "fn", "hold Fn")
    put(g, 3, 11, "Fn: row +", "fn", "hold Fn")
    put(g, 3, 12, "Fn: library +", "fn", "hold Fn")


def paint_midi(g):
    put(g, 1, 3, "Edit name", "act", "to Settings")
    put(g, 2, 13, "Idle", "idle", "Enter unused")


def paint_settings(g):
    put(g, 2, 11, "Fn: row −", "fn", "hold Fn")
    put(g, 2, 13, "BLE name", "act", "Enter applies")
    put(g, 3, 10, "Fn: value −", "fn", "vol / light")
    put(g, 3, 11, "Fn: row +", "fn", "hold Fn")
    put(g, 3, 12, "Fn: value +", "fn", "vol or light")


def paint_exit(g):
    put(g, 2, 13, "Exit", "exit", "then restart")


PAGES = [
    (
        "play",
        "Play / Tracker",
        paint_play,
        [
            "Bottom letters are the current octave. Q through ] is the next octave, chromatic. Comma is the C above that row.",
            "Shift or Alt adds an octave on letter notes. Opt subtracts one. They stack and clamp to MIDI C2–B5.",
            "On the piano roll, Fn+; and Fn+. move the cursor by a semitone, and Fn+, and Fn+/ move it by a step. On the 16-step strip, Fn+, and Fn+/ page the bar. Other Fn chords still do the key's normal action.",
            "Fn+− and Fn+= change speaker volume on every page (step 12). Hold ` or the front button about 0.7s to exit when Launcher is present.",
            "While the page list is open it takes every key. Fn+; , Fn+. , and Tab move the highlight. Enter stays there. ` or Backspace returns to Play. Notes, space, and track keys do nothing.",
            "On the roll, ; cycles the hold of the note under the cursor through 1–4 steps. On an empty cell it sends note-length L, and the voice stores 4 minus that value. Enter toggles the 16-step strip.",
            "Ctrl or Shift sends the shifted glyph, then the UI lowercases it. Digits and punctuation no longer match, so Ctrl+1–8 (!@#$%^&*) does not clear a track or pattern, and Shift+, is < rather than high C. Ctrl+N still starts a new song.",
        ],
    ),
    (
        "inst",
        "Instrument",
        paint_inst,
        [
            "9 / 0 and Fn+; / Fn+. move the instrument list. Enter assigns it to the selected track and loads an SD plugin.",
            ", and / load the previous or next drum kit onto the Drums instrument (Fn+, and Fn+/ do the same). Kits live in /moth/drums. R rescans the card.",
            "1–4 still select the track, 5–8 the pattern.",
        ],
    ),
    (
        "fx",
        "FX",
        paint_fx,
        [
            "Inserts on the selected track only: Filter, Cutoff, Res, Delay, Feedback, Mix, Reverb, Crush, Drive, Chorus, Tremolo.",
            "Fn+; and Fn+. move the row. Fn+, lowers the value. Fn+/ and Enter raise it. 1–4 select the track.",
            "Filter cycles off, low pass, high pass. Delay is off, 1/32, 1/16, or 1/8, and the time follows the BPM. A 1/8 note is shortened when it would be longer than the history buffer.",
            "Each track keeps its own settings. They are stored in the song. Play-page A, F, K, and L still cycle the older 0–2 effects.",
        ],
    ),
    (
        "mixer",
        "Mixer",
        paint_mixer,
        [
            "Four vertical faders, one per track, with the instrument name. The selected track is highlighted.",
            "Fn+; raises that track's volume. Fn+. lowers it. Volume is 0–8 and is saved with the song.",
            "Fn+, selects the previous track. Fn+/ selects the next. 1–4 still select a track directly.",
            "M mutes the selected track. S solos it. ` or Backspace returns to Play.",
        ],
    ),
    (
        "song",
        "Song / Files",
        paint_song,
        [
            "1–4 select a slot and report full or empty. 5–8 do nothing on this page.",
            "S save, L load, X delete, T status. N starts a new song and steps the length 32 / 64 / 96 / 128.",
            "C copy, V paste, G paste all, M song or pattern, H master, B next BPM slot. Enter loads the slot.",
            "9 and 0 still move the BPM slot. Ctrl+N starts a new song without stepping the length.",
        ],
    ),
    (
        "loops",
        "Loops",
        paint_loops,
        [
            "Enter launches the row on the selected track. A auditions. Q cycles quantize: now, beat, bar.",
            "S stops the loop on the track. R rescans /moth/loops.",
            "Fn+; moves the row up and will not pass row 8. Fn+. moves down to the last entry. Fn+, and Fn+/ change library.",
        ],
    ),
    (
        "midi",
        "MIDI",
        paint_midi,
        [
            "E jumps to Settings and starts editing the BLE name. Enter does nothing on this page.",
            "The page is a status view: connection, advertised name, and the channel map.",
        ],
    ),
    (
        "settings",
        "Settings",
        paint_settings,
        [
            "Rows: Speaker, Brightness, BLE name, Battery, Memory, Card. Fn+; and Fn+. move the row.",
            "Fn+, and Fn+/ change speaker volume or brightness when that row is selected.",
            "Enter on the BLE name row starts typing. Enter again applies the name and restarts advertising. On any other row, Enter does nothing.",
            "While the name editor is open it takes every key. Glyphs are lowercased and appended (16 max). Backspace deletes one character. ` cancels. Space does not play and is not typed.",
        ],
    ),
    (
        "exit",
        "Exit",
        paint_exit,
        [
            "Exit is offered when the APP_TEST slot holds a valid ESP32-S3 image (magic E9). Enter then clears the boot selection.",
            "Without that image the menu entry is grey and says Launcher not found. Hold ` does not erase the boot selection.",
            "` or Backspace returns to Play. Hold ` or the front button 0.7s to exit from any page when Launcher is present.",
        ],
    ),
]


def xml(text):
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def wrap(text, limit, max_lines=3):
    if len(text) <= limit:
        return [text]
    words = text.split(" ")
    lines = []
    cur = ""
    for word in words:
        trial = word if not cur else cur + " " + word
        if len(trial) <= limit:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    if max_lines and len(lines) > max_lines:
        lines = lines[:max_lines]
        if not lines[-1].endswith("…"):
            lines[-1] = lines[-1].rstrip(".") + "…"
    return lines


def render_svg(slug, title, g, notes):
    width = 1600
    margin = 28
    gap = 7
    cols = 14
    key_w = (width - margin * 2 - gap * (cols - 1)) / cols
    key_h = 112
    rows = 4
    title_h = 78
    legend_h = 78
    note_line = 20
    wrapped_notes = []
    for note in notes:
        wrapped_notes.extend(wrap(note, 112, max_lines=4))
    note_h = 28 + note_line * len(wrapped_notes)
    height = int(title_h + rows * (key_h + gap) + 18 + legend_h + note_h + margin)
    parts = []
    parts.append(
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
        % (width, height, width, height)
    )
    parts.append(
        "<style>text { font-family: 'DejaVu Sans', 'Liberation Sans', sans-serif; }</style>"
    )
    parts.append('<rect width="100%%" height="100%%" fill="#121418"/>' % ())
    parts.append(
        '<text x="%d" y="40" fill="#f2efe8" font-size="28" font-weight="700">MothDeck  %s</text>'
        % (margin, xml(title))
    )
    parts.append(
        '<text x="%d" y="66" fill="#9aa3ad" font-size="15">Cardputer ADV  4×14  TCA8418 order from M5Cardputer Keyboard.h</text>'
        % margin
    )
    y0 = title_h
    for r in range(rows):
        for c in range(cols):
            x = margin + c * (key_w + gap)
            y = y0 + r * (key_h + gap)
            item = g[r][c]
            fill, fg = GROUP[item["group"]]
            parts.append(
                '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="8" fill="%s"/>'
                % (x, y, key_w, key_h, fill)
            )
            phys = PHYS[r][c]
            parts.append(
                '<text x="%.1f" y="%.1f" fill="%s" font-size="13" opacity="0.72">%s</text>'
                % (x + 8, y + 18, fg, xml(phys))
            )
            lines = wrap(item["action"], 13)
            base = y + 46
            for i, line in enumerate(lines):
                parts.append(
                    '<text x="%.1f" y="%.1f" fill="%s" font-size="14" font-weight="700">%s</text>'
                    % (x + 8, base + i * 16, fg, xml(line))
                )
            if item["sub"]:
                parts.append(
                    '<text x="%.1f" y="%.1f" fill="%s" font-size="11" opacity="0.9">%s</text>'
                    % (x + 8, y + key_h - 10, fg, xml(item["sub"]))
                )
    ly = y0 + rows * (key_h + gap) + 8
    parts.append(
        '<text x="%d" y="%d" fill="#c5ccd3" font-size="13" font-weight="700">Legend</text>'
        % (margin, ly + 16)
    )
    lx = margin
    ly_box = ly + 26
    for gid, label in LEGEND:
        fill, fg = GROUP[gid]
        box_w = 18 + 7 * len(label)
        parts.append(
            '<rect x="%d" y="%d" width="%d" height="26" rx="5" fill="%s"/>'
            % (lx, ly_box, box_w, fill)
        )
        parts.append(
            '<text x="%d" y="%d" fill="%s" font-size="12">%s</text>'
            % (lx + 8, ly_box + 17, fg, xml(label))
        )
        lx += box_w + 8
        if lx > width - 180:
            lx = margin
            ly_box += 32
    ny = ly_box + 42
    for i, note in enumerate(wrapped_notes):
        parts.append(
            '<text x="%d" y="%d" fill="#c5ccd3" font-size="13">%s</text>'
            % (margin, ny + i * note_line, xml(note))
        )
    parts.append("</svg>")
    path = os.path.join(DOCS, "keymap-%s.svg" % slug)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts) + "\n")
    return path


def raster(svg_path):
    import cairosvg

    png_path = os.path.splitext(svg_path)[0] + ".png"
    cairosvg.svg2png(url=svg_path, write_to=png_path, output_width=1600)
    return png_path


def main():
    os.makedirs(DOCS, exist_ok=True)
    written = []
    for slug, title, painter, notes in PAGES:
        g = grid()
        apply_common(g, slug)
        painter(g)
        svg_path = render_svg(slug, title, g, notes)
        png_path = raster(svg_path)
        written.append((svg_path, png_path))
        print(png_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
