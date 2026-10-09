#!/usr/bin/env python3
"""Draw the Play piano roll the way src/Ui.cpp does, at 240x135.

Host stand-in for a Cardputer screenshot. The grid, key strip, note
blocks, playhead, and cursor use the same coordinates and RGB565 colours
as the firmware. Text is a 6x8 bitmap so the frame stays pixelated.

    python3 tools/render_piano_roll.py /opt/cursor/artifacts/piano-roll.png
"""
import struct
import sys
import zlib

W, H = 240, 135
ROLL_Y, ROLL_H, ROWS, ROW_H, GRID_X = 24, 96, 48, 2, 22

# RGB565, matching Ui.cpp.
COL_BG = 0x1082
COL_BAR = 0x2104
COL_AMBER = 0xFD20
COL_TEXT = 0xEF5D
COL_DIM = 0x6B6D
COL_PLAY = 0x07E0
COL_TRACK = 0xFD20
COL_KEY_W = 0xC618
COL_KEY_B = 0x0000
COL_LANE_W = 0x18C3
COL_LANE_B = 0x0861
COL_BEAT = 0x3186
COL_BARLINE = 0x52AA
COL_CURSOR = 0xFFFF

# 5x7 glyphs, column-major, bit 0 at the top. Drawn in a 6x8 cell.
FONT = {
    " ": [0x00, 0x00, 0x00, 0x00, 0x00],
    "0": [0x3E, 0x51, 0x49, 0x45, 0x3E],
    "1": [0x00, 0x42, 0x7F, 0x40, 0x00],
    "2": [0x42, 0x61, 0x51, 0x49, 0x46],
    "3": [0x21, 0x41, 0x45, 0x4B, 0x31],
    "4": [0x18, 0x14, 0x12, 0x7F, 0x10],
    "5": [0x27, 0x45, 0x45, 0x45, 0x39],
    "6": [0x3C, 0x4A, 0x49, 0x49, 0x30],
    "7": [0x01, 0x71, 0x09, 0x05, 0x03],
    "8": [0x36, 0x49, 0x49, 0x49, 0x36],
    "9": [0x06, 0x49, 0x49, 0x29, 0x1E],
    "A": [0x7E, 0x11, 0x11, 0x11, 0x7E],
    "B": [0x7F, 0x49, 0x49, 0x49, 0x36],
    "C": [0x3E, 0x41, 0x41, 0x41, 0x22],
    "D": [0x7F, 0x41, 0x41, 0x22, 0x1C],
    "E": [0x7F, 0x49, 0x49, 0x49, 0x41],
    "F": [0x7F, 0x09, 0x09, 0x09, 0x01],
    "G": [0x3E, 0x41, 0x49, 0x49, 0x7A],
    "H": [0x7F, 0x08, 0x08, 0x08, 0x7F],
    "I": [0x00, 0x41, 0x7F, 0x41, 0x00],
    "K": [0x7F, 0x08, 0x14, 0x22, 0x41],
    "L": [0x7F, 0x40, 0x40, 0x40, 0x40],
    "M": [0x7F, 0x02, 0x0C, 0x02, 0x7F],
    "N": [0x7F, 0x04, 0x08, 0x10, 0x7F],
    "O": [0x3E, 0x41, 0x41, 0x41, 0x3E],
    "P": [0x7F, 0x09, 0x09, 0x09, 0x06],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31],
    "T": [0x01, 0x01, 0x7F, 0x01, 0x01],
    "a": [0x20, 0x54, 0x54, 0x54, 0x78],
    "c": [0x38, 0x44, 0x44, 0x44, 0x20],
    "d": [0x38, 0x44, 0x44, 0x48, 0x7F],
    "e": [0x38, 0x54, 0x54, 0x54, 0x18],
    "h": [0x7F, 0x08, 0x04, 0x04, 0x78],
    "i": [0x00, 0x44, 0x7D, 0x40, 0x00],
    "k": [0x7F, 0x10, 0x28, 0x44, 0x00],
    "l": [0x00, 0x41, 0x7F, 0x40, 0x00],
    "m": [0x7C, 0x04, 0x18, 0x04, 0x78],
    "n": [0x7C, 0x08, 0x04, 0x04, 0x78],
    "o": [0x38, 0x44, 0x44, 0x44, 0x38],
    "p": [0x7C, 0x14, 0x14, 0x14, 0x08],
    "r": [0x7C, 0x08, 0x04, 0x04, 0x08],
    "s": [0x48, 0x54, 0x54, 0x54, 0x20],
    "t": [0x04, 0x3F, 0x44, 0x40, 0x20],
    "v": [0x1C, 0x20, 0x40, 0x20, 0x1C],
    "x": [0x44, 0x28, 0x10, 0x28, 0x44],
    "/": [0x20, 0x10, 0x08, 0x04, 0x02],
    "#": [0x14, 0x7F, 0x14, 0x7F, 0x14],
    ";": [0x00, 0x56, 0x36, 0x00, 0x00],
    "B": [0x7F, 0x49, 0x49, 0x49, 0x36],
}


def rgb565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return bytes((r, g, b))


def black_semi(pitch):
    return (pitch % 12) in (1, 3, 6, 8, 10)


class Frame:
    def __init__(self):
        self.px = bytearray(rgb565(COL_BG) * (W * H))

    def fill(self, x, y, w, h, color):
        if w <= 0 or h <= 0:
            return
        if x < 0:
            w += x
            x = 0
        if y < 0:
            h += y
            y = 0
        if x + w > W:
            w = W - x
        if y + h > H:
            h = H - y
        if w <= 0 or h <= 0:
            return
        pix = rgb565(color)
        for row in range(y, y + h):
            start = (row * W + x) * 3
            self.px[start : start + w * 3] = pix * w

    def text(self, x, y, s, color):
        pix = rgb565(color)
        cx = x
        for ch in s:
            glyph = FONT.get(ch, FONT.get(ch.upper(), FONT[" "]))
            for col, bits in enumerate(glyph):
                for row in range(7):
                    if bits & (1 << row):
                        px = cx + col
                        py = y + row
                        if 0 <= px < W and 0 <= py < H:
                            i = (py * W + px) * 3
                            self.px[i : i + 3] = pix
            cx += 6

    def png(self, scale=1):
        w, h = W * scale, H * scale
        if scale == 1:
            raw = self.px
        else:
            raw = bytearray(w * h * 3)
            for y in range(H):
                row = self.px[y * W * 3 : (y + 1) * W * 3]
                wide = bytearray()
                for x in range(W):
                    wide += row[x * 3 : x * 3 + 3] * scale
                for sy in range(scale):
                    dst = ((y * scale + sy) * w) * 3
                    raw[dst : dst + w * 3] = wide

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

        lines = b"".join(b"\x00" + bytes(raw[y * w * 3 : (y + 1) * w * 3]) for y in range(h))
        ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(lines, 9)) + chunk(b"IEND", b"")


def step_px(pat_len):
    grid = W - GRID_X
    if pat_len * 8 <= grid:
        return 8
    if pat_len * 6 <= grid:
        return 6
    return 4


def draw(notes, pat_len, origin, head, cursor_step, cursor_pitch, info, hint):
    """notes: list of (step, pitch 0-47, length)."""
    fr = Frame()
    fr.fill(0, 0, W, 12, COL_BAR)
    fr.text(2, 2, "MothDeck", COL_AMBER)
    fr.text(50, 2, " T1 120 P1", COL_TEXT)
    fr.text(116, 2, " STOP", COL_DIM)
    fr.text(146, 2, " midi", COL_DIM)

    name, rest = info.split(" ", 1)
    fr.text(2, 14, name, COL_TRACK)
    fr.text(2 + 6 * len(name), 14, " " + rest, COL_DIM)
    if hint:
        fr.text(168, 14, hint[:8], COL_AMBER)

    step_w = step_px(pat_len)
    vis = (W - GRID_X) // step_w
    for p in range(ROWS):
        y = ROLL_Y + (ROWS - 1 - p) * ROW_H
        black = black_semi(p)
        fr.fill(GRID_X, y, W - GRID_X, ROW_H, COL_LANE_B if black else COL_LANE_W)
        if black:
            fr.fill(8, y, 8, ROW_H, COL_KEY_B)
        else:
            fr.fill(8, y, 13, ROW_H, COL_KEY_W)
    # Octave digits are the same 3x5 the firmware draws, so they fit a 24px octave.
    digits = {
        2: ("111", "001", "111", "100", "111"),
        3: ("111", "001", "111", "001", "111"),
        4: ("101", "101", "111", "001", "001"),
        5: ("111", "100", "111", "001", "111"),
    }
    for octv in range(4):
        top = octv * 12 + 11
        y = ROLL_Y + (ROWS - 1 - top) * ROW_H + 10
        glyph = digits[octv + 2]
        for dy, row in enumerate(glyph):
            for dx, bit in enumerate(row):
                if bit == "1":
                    fr.fill(1 + dx, y + dy, 1, 1, COL_TEXT)
        cy = ROLL_Y + (ROWS - 1 - octv * 12) * ROW_H
        fr.fill(19, cy, 2, ROW_H, COL_AMBER)

    for i in range(vis + 1):
        step = origin + i
        if step > pat_len or step % 4:
            continue
        x = GRID_X + i * step_w
        if x >= W:
            break
        fr.fill(x, ROLL_Y, 1, ROLL_H, COL_BARLINE if step % 16 == 0 else COL_BEAT)

    for step, pitch, length in notes:
        if step + length <= origin or step >= origin + vis:
            continue
        x0 = GRID_X + (step - origin) * step_w
        x1 = x0 + length * step_w - 1
        if x0 < GRID_X:
            x0 = GRID_X
        if x1 >= W:
            x1 = W - 1
        y = ROLL_Y + (ROWS - 1 - pitch) * ROW_H
        fr.fill(x0, y, x1 - x0 + 1, ROW_H, COL_TRACK)

    if origin <= head < origin + vis and head < pat_len:
        fr.fill(GRID_X + (head - origin) * step_w, ROLL_Y, 1, ROLL_H, COL_PLAY)
    if origin <= cursor_step < origin + vis:
        x = GRID_X + (cursor_step - origin) * step_w
        y = ROLL_Y + (ROWS - 1 - cursor_pitch) * ROW_H - 1
        h = ROW_H + 2
        if y < ROLL_Y:
            h -= ROLL_Y - y
            y = ROLL_Y
        if y + h > ROLL_Y + ROLL_H:
            h = ROLL_Y + ROLL_H - y
        # Box, not a fill, matching drawRect.
        fr.fill(x, y, step_w, 1, COL_CURSOR)
        fr.fill(x, y + h - 1, step_w, 1, COL_CURSOR)
        fr.fill(x, y, 1, h, COL_CURSOR)
        fr.fill(x + step_w - 1, y, 1, h, COL_CURSOR)

    fr.fill(0, 122, W, 13, COL_BAR)
    fr.text(2, 124, "Fn move  ; hold  Ent steps", COL_DIM)
    return fr, step_w


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/piano-roll.png"
    # 4 bars, 4px per step. The window starts at step 8 so a bar line and
    # the off-screen tail both exist: this is the scrolled long-pattern view.
    pat_len = 64
    notes = [
        (0, 0, 4),
        (4, 4, 2),
        (6, 6, 2),
        (8, 7, 1),
        (12, 12, 4),
        (16, 12, 4),
        (20, 16, 3),
        (24, 19, 2),
        (28, 23, 1),
        (32, 24, 4),
        (36, 27, 3),
        (40, 28, 2),
        (48, 31, 4),
        (56, 36, 1),
        (60, 40, 4),
    ]
    origin, head, cursor, pitch = 8, 20, 24, 19
    fr, step_w = draw(
        notes,
        pat_len,
        origin,
        head,
        cursor,
        pitch,
        "T1 G3 x2  B2/4",
        "Hold 2",
    )
    png = fr.png(1)
    with open(out, "wb") as handle:
        handle.write(png)
    zoom = out.rsplit(".", 1)[0] + "-3x.png"
    with open(zoom, "wb") as handle:
        handle.write(fr.png(3))
    print("%s %d bytes step=%d" % (out, len(png), step_w))
    print(zoom)


if __name__ == "__main__":
    main()
