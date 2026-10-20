#!/usr/bin/env python3
"""Generate the MothDeck handheld schematic, carrier PCB, and fabrication outputs.

Board is 66 x 78 mm. Y in this script increases downward, matching KiCad.
The DevKit antenna is toward y=0 and its USB socket toward the bottom edge.
"""

import array
from collections import deque
import heapq
import math
import os
import shutil
import subprocess
import uuid
import zipfile

import pcbnew

ROOT = os.path.dirname(os.path.abspath(__file__))
PCB_W = 66.0
PCB_H = 78.0
# 2.54 mm / 20. A coarser grid has no cell that sits in the 0.84 mm gap
# between 1.7 mm pads and still clears 0.2 mm on both sides.
GRID = 2.54 / 20.0
CLEAR = 0.20
TRACK = 0.25
# Header pins are 1.7 mm on a 2.54 mm pitch, so a 0.5 mm track fails the
# 0.2 mm clearance in the only channel between them. 1 oz 0.25 mm copper
# carries the ~300 mA radio peak with margin, so power uses the same width.
TRACK_PWR = 0.25
VIA_OD = 0.70
VIA_DRILL = 0.40


def mm(v):
    return pcbnew.FromMM(float(v))


def uid():
    return str(uuid.uuid4())


# --- schematic --------------------------------------------------------------

def sch_symbol(name, ref_prefix, w, h, pins):
    """pins: list of (number, name, side) side is 'L' or 'R'."""
    left = [p for p in pins if p[2] == "L"]
    right = [p for p in pins if p[2] == "R"]
    body = []
    body.append(
        f'    (symbol "{name}_0_1"\n'
        f'      (rectangle (start {-w/2:.2f} {h/2:.2f}) (end {w/2:.2f} {-h/2:.2f})\n'
        f'        (stroke (width 0.254) (type default)) (fill (type background)))\n'
        f'    )'
    )
    pin_gfx = ['    (symbol "%s_1_1"' % name]
    for side, group in (("L", left), ("R", right)):
        n = max(1, len(group))
        span = max(h - 2.54, 2.54)
        for i, (num, pname, _s) in enumerate(group):
            if n == 1:
                y = 0
            else:
                y = span / 2 - i * (span / (n - 1))
            x = -w / 2 if side == "L" else w / 2
            ang = 0 if side == "L" else 180
            length = 2.54
            pin_gfx.append(
                f'      (pin passive line (at {x:.2f} {y:.2f} {ang}) (length {length:.2f})\n'
                f'        (name "{pname}" (effects (font (size 1.016 1.016))))\n'
                f'        (number "{num}" (effects (font (size 1.016 1.016)))))'
            )
    pin_gfx.append("    )")
    props = (
        f'    (property "Reference" "{ref_prefix}" (at 0 {h/2+1.5:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (property "Value" "{name}" (at 0 {-h/2-1.5:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
        f'    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
    )
    return (
        f'  (symbol "{name}"\n'
        f'    (pin_names (offset 0.254))\n'
        f'    (in_bom yes) (on_board yes)\n'
        f"{props}"
        + "\n".join(body)
        + "\n"
        + "\n".join(pin_gfx)
        + "\n  )"
    )


def sch_place(lib, x, y, ref, value, pins_xy):
    """pins_xy: list of (number, abs_x, abs_y) for no-connect or labels."""
    pin_entries = "\n".join(
        f'    (pin "{num}" (uuid "{uid()}"))' for num, _px, _py in pins_xy
    )
    return (
        f'  (symbol (lib_id "{lib}") (at {x:.2f} {y:.2f} 0) (unit 1)\n'
        f'    (in_bom yes) (on_board yes) (dnp no)\n'
        f'    (uuid "{uid()}")\n'
        f'    (property "Reference" "{ref}" (at {x:.2f} {y+2:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (property "Value" "{value}" (at {x:.2f} {y-2:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (property "Footprint" "" (at {x:.2f} {y:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27)) hide))\n'
        f'    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0)\n'
        f'      (effects (font (size 1.27 1.27)) hide))\n'
        f"{pin_entries}\n"
        f"  )"
    )


def pin_abs(origin_x, origin_y, w, h, pins, number):
    left = [p for p in pins if p[2] == "L"]
    right = [p for p in pins if p[2] == "R"]
    for side, group in (("L", left), ("R", right)):
        n = max(1, len(group))
        span = max(h - 2.54, 2.54)
        for i, (num, _name, _s) in enumerate(group):
            if num != number:
                continue
            y = 0 if n == 1 else span / 2 - i * (span / (n - 1))
            x = -w / 2 if side == "L" else w / 2
            # pin end is length 2.54 outside the body
            if side == "L":
                x -= 2.54
            else:
                x += 2.54
            return origin_x + x, origin_y + y
    raise KeyError(number)


def label_at(text, x, y):
    return (
        f'  (label "{text}" (at {x:.2f} {y:.2f} 0)\n'
        f'    (effects (font (size 1.016 1.016)) (justify left bottom))\n'
        f'    (uuid "{uid()}"))'
    )


def wire(x1, y1, x2, y2):
    return (
        f'  (wire (pts (xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f}))\n'
        f'    (stroke (width 0) (type default)) (uuid "{uid()}"))'
    )


def write_schematic(path):
    # Compact but complete: each connector is a box, pins carry net labels.
    mods = []

    def add_mod(name, prefix, pins):
        h = max(12.7, 2.54 * (max(len([p for p in pins if p[2] == "L"]), len([p for p in pins if p[2] == "R"])) + 1))
        w = 18.0
        mods.append((name, prefix, w, h, pins))

    dev_l = [("1", "3V3", "R"), ("2", "EN", "R"), ("3", "IO36", "R"), ("4", "IO39", "R"),
             ("5", "IO34", "R"), ("6", "IO35", "R"), ("7", "IO32", "R"), ("8", "IO33", "R"),
             ("9", "IO25", "R"), ("10", "IO26", "R"), ("11", "IO27", "R"), ("12", "IO14", "R"),
             ("13", "IO12", "R"), ("14", "GND", "R"), ("15", "IO13", "R")]
    dev_r = [("1", "VIN", "L"), ("2", "GND", "L"), ("3", "IO23", "L"), ("4", "IO22", "L"),
             ("5", "TX0", "L"), ("6", "RX0", "L"), ("7", "IO21", "L"), ("8", "GND", "L"),
             ("9", "IO19", "L"), ("10", "IO18", "L"), ("11", "IO5", "L"), ("12", "IO17", "L"),
             ("13", "IO16", "L"), ("14", "IO4", "L"), ("15", "IO0", "L")]
    add_mod("DevKit_L", "J", dev_l)
    add_mod("DevKit_R", "J", dev_r)
    add_mod("PCM5102", "J", [("1", "SCK", "L"), ("2", "BCK", "L"), ("3", "DIN", "L"),
                             ("4", "LCK", "L"), ("5", "GND", "L"), ("6", "VIN", "L")])
    add_mod("SSD1306", "J", [("1", "GND", "L"), ("2", "VCC", "L"), ("3", "SCL", "L"), ("4", "SDA", "L")])
    add_mod("HT16K33", "J", [("1", "GND", "L"), ("2", "VCC", "L"), ("3", "SDA", "L"), ("4", "SCL", "L")])
    add_mod("KEYPAD", "J", [("1", "R1", "R"), ("2", "R2", "R"), ("3", "R3", "R"), ("4", "R4", "R"),
                            ("5", "C1", "R"), ("6", "C2", "R"), ("7", "C3", "R"), ("8", "C4", "R")])
    add_mod("TP4056", "J", [("1", "OUT+", "R"), ("2", "OUT-", "R"), ("3", "B+", "R"), ("4", "B-", "R")])
    add_mod("AP2112", "U", [("1", "VIN", "L"), ("2", "GND", "L"), ("3", "EN", "L"), ("5", "VOUT", "R"), ("4", "BP", "R")])
    add_mod("JST", "J", [("1", "BAT+", "R"), ("2", "BAT-", "R")])
    add_mod("SW", "SW", [("1", "A", "L"), ("2", "COM", "R"), ("3", "B", "R")])

    # Two-pin passives drawn as tiny boxes.
    add_mod("R", "R", [("1", "1", "L"), ("2", "2", "R")])
    add_mod("C", "C", [("1", "1", "L"), ("2", "2", "R")])
    add_mod("LED", "D", [("1", "A", "L"), ("2", "K", "R")])

    lib = "\n".join(sch_symbol(n, p, w, h, pins) for n, p, w, h, pins in mods)
    by_name = {n: (w, h, pins) for n, _p, w, h, pins in mods}

    placed = []
    labels = []
    wires = []
    notes = []

    def place(name, x, y, ref, value, net_by_pin):
        w, h, pins = by_name[name]
        pin_xy = []
        for num, _pname, _side in pins:
            ax, ay = pin_abs(x, y, w, h, pins, num)
            pin_xy.append((num, ax, ay))
            net = net_by_pin.get(num)
            if net:
                # short stub already ends at the pin; put the label on the pin
                labels.append(label_at(net, ax, ay))
            elif net is None and num not in net_by_pin:
                pass
        placed.append(sch_place(name, x, y, ref, value, pin_xy))

    # Power column
    place("JST", 40, 30, "J2", "603450 cell", {"1": "BAT+", "2": "GND"})
    place("TP4056", 80, 40, "J8", "TP4056 USB-C", {"1": "VBAT", "2": "GND", "3": "BAT+", "4": "GND"})
    place("SW", 130, 30, "SW1", "SS12D00", {"1": "VBAT", "2": "VSW", "3": "NC"})
    place("AP2112", 180, 40, "U1", "AP2112K-3.3", {"1": "VSW", "2": "GND", "3": "VSW", "5": "+3V3", "4": "BP"})
    place("C", 180, 70, "C1", "10uF", {"1": "VSW", "2": "GND"})
    place("C", 210, 70, "C2", "10uF", {"1": "+3V3", "2": "GND"})
    place("C", 180, 90, "C3", "22nF", {"1": "BP", "2": "GND"})
    place("R", 40, 80, "R1", "100k", {"1": "BAT+", "2": "ADC"})
    place("R", 70, 80, "R2", "100k", {"1": "ADC", "2": "GND"})
    place("C", 100, 80, "C4", "100nF", {"1": "ADC", "2": "GND"})

    # DevKit. Net names are the signal names. NC pins are labelled NC so the
    # sheet shows they are deliberately open (strapping and flash-voltage).
    place("DevKit_L", 280, 90, "J3", "DevKit left", {
        "1": "+3V3", "2": "NC_EN", "3": "KP_C2", "4": "KP_C3", "5": "KP_C0", "6": "KP_C1",
        "7": "NC_IO32", "8": "ADC", "9": "I2S_WS", "10": "I2S_BCK", "11": "I2S_DIN",
        "12": "LED_REC", "13": "NC_IO12", "14": "GND", "15": "LED_PLAY",
    })
    place("DevKit_R", 360, 90, "J4", "DevKit right", {
        "1": "NC_VIN", "2": "GND", "3": "NC_IO23", "4": "I2C_SCL", "5": "NC_TX", "6": "NC_RX",
        "7": "I2C_SDA", "8": "GND", "9": "KP_R3", "10": "KP_R2", "11": "LED_BEAT",
        "12": "KP_R1", "13": "KP_R0", "14": "LED_STEP", "15": "NC_IO0",
    })

    place("KEYPAD", 450, 40, "J1", "4x4 membrane", {
        "1": "KP_R0", "2": "KP_R1", "3": "KP_R2", "4": "KP_R3",
        "5": "KP_C0", "6": "KP_C1", "7": "KP_C2", "8": "KP_C3",
    })
    place("R", 500, 20, "R5", "10k", {"1": "+3V3", "2": "KP_C0"})
    place("R", 530, 20, "R6", "10k", {"1": "+3V3", "2": "KP_C1"})
    place("R", 500, 40, "R7", "10k", {"1": "+3V3", "2": "KP_C2"})
    place("R", 530, 40, "R8", "10k", {"1": "+3V3", "2": "KP_C3"})

    place("SSD1306", 450, 100, "J5", "SSD1306 0x3C", {"1": "GND", "2": "+3V3", "3": "I2C_SCL", "4": "I2C_SDA"})
    place("HT16K33", 450, 140, "J6", "HT16K33 0x70", {"1": "GND", "2": "+3V3", "3": "I2C_SDA", "4": "I2C_SCL"})
    place("R", 520, 110, "R3", "4.7k", {"1": "+3V3", "2": "I2C_SDA"})
    place("R", 520, 130, "R4", "4.7k", {"1": "+3V3", "2": "I2C_SCL"})

    place("PCM5102", 450, 180, "J7", "PCM5102A", {
        "1": "GND", "2": "I2S_BCK", "3": "I2S_DIN", "4": "I2S_WS", "5": "GND", "6": "+3V3",
    })

    for i, net in enumerate(("LED_STEP", "LED_BEAT", "LED_PLAY", "LED_REC")):
        place("R", 300 + i * 30, 200, f"R{9+i}", "470", {"1": net, "2": f"LED{i+1}_A"})
        place("LED", 300 + i * 30, 220, f"D{i+1}", "3mm", {"1": f"LED{i+1}_A", "2": "GND"})

    notes.append(
        '  (text "MothDeck handheld carrier. DOIT ESP32 DevKit V1, 30-pin, ESP32-D0WD.\\n'
        'GPIO0, GPIO2, GPIO12 and GPIO15 stay open (strapping). GPIO6-11 are inside the module.\\n'
        'PCM5102 SCK is tied to GND so the DAC PLL runs without a master clock.\\n'
        'Keypad columns are input-only and have 10k pull-ups. The membrane is rotated 90 degrees clockwise;\\n'
        'firmware remaps membrane row/col to visual key order. See the README pin table.\\n'
        'Power: cell to TP4056 B+/B-. Protected OUT+ through SW1 into the AP2112. 3V3 feeds the DevKit 3V3 pin.\\n'
        'Do not use the DevKit VIN pin. The onboard AMS1117 cannot regulate a single Li-ion cell."\n'
        '    (at 25 250 0) (effects (font (size 1.6 1.6))) (uuid "%s"))' % uid()
    )

    sch = f'''(kicad_sch (version 20230121) (generator mothdeck_handheld)
  (uuid "{uid()}")
  (paper "A2")
  (title_block
    (title "MothDeck ESP32 DevKit handheld")
    (date "2026-10-20")
    (rev "1")
    (comment 1 "66 x 78 mm carrier, 2 layer")
    (comment 2 "PCM5102A, SSD1306, HT16K33, 4x4 keypad, 4 LEDs")
  )
  (lib_symbols
{lib}
  )
{os.linesep.join(placed)}
{os.linesep.join(labels)}
{os.linesep.join(wires)}
{os.linesep.join(notes)}
)
'''
    with open(path, "w", encoding="utf-8") as f:
        f.write(sch)


# --- PCB --------------------------------------------------------------------

class BoardBuilder:
    def __init__(self):
        self.board = pcbnew.BOARD()
        self.nets = {}
        self.pads = []  # (x_mm, y_mm, radius_mm, netname or None, kind)
        self.net_id = 1

    def net(self, name):
        if not name or name.startswith("NC"):
            return None
        if name not in self.nets:
            item = pcbnew.NETINFO_ITEM(self.board, name, self.net_id)
            self.net_id += 1
            self.board.Add(item)
            self.nets[name] = item
        return self.nets[name]

    def add_fp(self, ref, value, x, y):
        fp = pcbnew.FOOTPRINT(self.board)
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        fp.Reference().SetPosition(pcbnew.VECTOR2I(mm(x), mm(y - 1.6)))
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7)))
        fp.Reference().SetTextThickness(mm(0.12))
        # Silk references sit on the pads of these tight headers. The
        # intentional legend is drawn separately.
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        self.board.Add(fp)
        return fp

    def add_pad(self, fp, number, netname, x, y, drill, size, shape="circle", plated=True):
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(number))
        pad.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        # Pos0 is relative to the footprint anchor.
        ax, ay = fp.GetPosition().x, fp.GetPosition().y
        pad.SetPos0(pcbnew.VECTOR2I(int(mm(x) - ax), int(mm(y) - ay)))
        pad.SetSize(pcbnew.VECTOR2I(mm(size), mm(size if shape == "circle" else size)))
        if shape == "rect":
            pad.SetShape(pcbnew.PAD_SHAPE_RECT)
            pad.SetSize(pcbnew.VECTOR2I(mm(size), mm(size * 0.7)))
        else:
            pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        if plated:
            pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
            pad.SetDrillSize(pcbnew.VECTOR2I(mm(drill), mm(drill)))
            pad.SetLayerSet(pad.PTHMask())
        else:
            pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
            pad.SetDrillSize(pcbnew.VECTOR2I(mm(drill), mm(drill)))
            pad.SetLayerSet(pad.UnplatedHoleMask())
        nitem = self.net(netname) if plated else None
        if nitem is not None:
            pad.SetNet(nitem)
        fp.Add(pad)
        if plated and netname and not str(netname).startswith("NC"):
            self.pads.append((x, y, size / 2.0, netname))
        elif plated:
            self.pads.append((x, y, size / 2.0, None))
        return pad

    def header(self, ref, value, x, y, pitch, names, vertical=True):
        fp = self.add_fp(ref, value, x, y)
        for i, name in enumerate(names):
            px = x if vertical else x + i * pitch
            py = y + i * pitch if vertical else y
            self.add_pad(fp, i + 1, name, px, py, 1.0, 1.7)
        return fp

    def smd(self, ref, value, x, y, net_a, net_b, span=2.2, vertical=False):
        fp = self.add_fp(ref, value, x, y)
        offsets = ((0, -span / 2), (0, span / 2)) if vertical else ((-span / 2, 0), (span / 2, 0))
        for i, ((dx, dy), net) in enumerate(zip(offsets, (net_a, net_b))):
            pad = pcbnew.PAD(fp)
            pad.SetNumber(str(i + 1))
            pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
            pad.SetShape(pcbnew.PAD_SHAPE_ROUNDRECT)
            pad.SetSize(pcbnew.VECTOR2I(mm(0.9), mm(1.0)))
            pad.SetRoundRectRadiusRatio(0.2)
            pad.SetLayerSet(pad.SMDMask())
            posx, posy = x + dx, y + dy
            pad.SetPosition(pcbnew.VECTOR2I(mm(posx), mm(posy)))
            ax, ay = fp.GetPosition().x, fp.GetPosition().y
            pad.SetPos0(pcbnew.VECTOR2I(int(mm(posx) - ax), int(mm(posy) - ay)))
            nitem = self.net(net)
            if nitem is not None:
                pad.SetNet(nitem)
            fp.Add(pad)
            self.pads.append((posx, posy, 0.55, net if net and not str(net).startswith("NC") else None))
        return fp

    def led(self, ref, x, y, net_a):
        # 3 mm LED, pads 2.54 mm apart, anode toward the resistor net.
        return self.header(ref, "LED", x, y, 2.54, [net_a, "GND"], vertical=False)

    def silk(self, text, x, y, size=1.0):
        t = pcbnew.PCB_TEXT(self.board)
        t.SetText(text)
        t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        t.SetLayer(pcbnew.F_SilkS)
        size = max(size, 1.0)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.15))
        self.board.Add(t)

    def edge(self):
        # Four segments. A single rectangle primitive is rejected as a
        # malformed outline by the DRC checker in KiCad 7.
        pts = [(0, 0), (PCB_W, 0), (PCB_W, PCB_H), (0, PCB_H)]
        for i in range(4):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % 4]
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT)
            s.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
            s.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(mm(0.05))
            self.board.Add(s)

    def hole(self, ref, x, y):
        fp = self.add_fp(ref, "M2", x, y)
        self.add_pad(fp, "", None, x, y, 2.2, 2.2, plated=False)


def build_pcb():
    b = BoardBuilder()
    b.edge()
    # Mounting holes, inset so they clear the 2 mm edge rule.
    for i, (x, y) in enumerate(((4, 4), (62, 4), (4, 74), (62, 74)), start=1):
        b.hole(f"H{i}", x, y)

    # DevKit V1, USB toward the bottom edge. Pin 1 is the antenna end.
    # Left header top-to-bottom matches the common 30-pin silkscreen.
    y0 = 30.70
    lx, rx = 16.67, 39.53
    left_nets = [
        "+3V3", "NC_EN", "KP_C2", "KP_C3", "KP_C0", "KP_C1", "NC_IO32", "ADC",
        "I2S_WS", "I2S_BCK", "I2S_DIN", "LED_REC", "NC_IO12", "GND", "LED_PLAY",
    ]
    right_nets = [
        "NC_VIN", "GND", "NC_IO23", "I2C_SCL", "NC_TX", "NC_RX", "I2C_SDA", "GND",
        "KP_R3", "KP_R2", "LED_BEAT", "KP_R1", "KP_R0", "LED_STEP", "NC_IO0",
    ]
    b.header("J3", "DevKit_L", lx, y0, 2.54, left_nets, vertical=True)
    b.header("J4", "DevKit_R", rx, y0, 2.54, right_nets, vertical=True)
    b.silk("ANTENNA", 28, 25.6, 1.0)
    b.silk("DEVKIT USB", 50.0, 74.6, 1.0)

    # Keypad ribbon, membrane order, above the DevKit on the left.
    kp = ["KP_R0", "KP_R1", "KP_R2", "KP_R3", "KP_C0", "KP_C1", "KP_C2", "KP_C3"]
    b.header("J1", "KEYPAD", 6.0, 6.0, 2.54, kp, vertical=True)
    b.silk("KP", 9.5, 27.2, 1.0)

    # Battery jack sits low, next to the DevKit ground pin, so the return
    # has a short path and does not cross the keypad escape.
    b.header("J2", "BAT", 8.0, 60.0, 2.00, ["BAT+", "GND"], vertical=True)
    b.silk("BAT", 4.2, 58.2, 1.0)
    b.header("SW1", "PWR", 5.0, 42.0, 2.54, ["VBAT", "VSW", "NC"], vertical=True)
    b.silk("PWR", 10.2, 42.0, 1.0)

    # Displays along the top-right. Jacks and the charger use the edges.
    b.header("J5", "OLED", 50.0, 6.0, 2.54, ["GND", "+3V3", "I2C_SCL", "I2C_SDA"], vertical=True)
    b.silk("OLED", 63.2, 16.4, 1.0)
    b.header("J6", "7SEG", 58.0, 6.0, 2.54, ["GND", "+3V3", "I2C_SDA", "I2C_SCL"], vertical=True)
    b.silk("BPM", 63.2, 20.2, 1.0)

    b.header("J7", "PCM5102", 50.0, 28.0, 2.54, ["GND", "I2S_BCK", "I2S_DIN", "I2S_WS", "GND", "+3V3"], vertical=True)
    b.silk("PHONES", 60.5, 44.2, 1.0)

    # LEDs in a row under the DAC header. The 470 ohm resistor sits between
    # the GPIO net and the LED anode; the cathode is GND.
    led_nets = ["LED_STEP", "LED_BEAT", "LED_PLAY", "LED_REC"]
    led_xy = []
    for i, net in enumerate(led_nets):
        x = 42.0 + i * 6.0
        y = 50.5
        anode = f"LED{i+1}_A"
        b.led(f"D{i+1}", x, y, anode)
        led_xy.append((x, y, anode))
        b.smd(f"R{9+i}", "470", x, y - 2.4, net, anode, span=1.6)
    b.silk("STEP BEAT PLAY REC", 54, 57.6, 1.0)

    # Charger along the bottom edge. USB-C faces down, off the board.
    b.header("J8", "TP4056", 48.0, 66.0, 2.54, ["VBAT", "GND", "BAT+", "GND"], vertical=False)
    b.silk("USB-C", 48, 70.8, 1.0)

    # Regulator under the DevKit, between the two headers.
    # SOT-23-5 drawn as three SMD pairs is awkward; use a 5-pad custom fp.
    # Ground pin faces the DevKit ground pin one pitch to the left.
    fp = b.add_fp("U1", "AP2112K-3.3", 20.2, 63.72)
    sot = [
        (1, "VSW", -1.0, -0.95),
        (2, "GND", -1.0, 0.0),
        (3, "VSW", -1.0, 0.95),
        (4, "BP", 1.0, 0.475),
        (5, "+3V3", 1.0, -0.475),
    ]
    for num, net, dx, dy in sot:
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(num))
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetShape(pcbnew.PAD_SHAPE_ROUNDRECT)
        pad.SetSize(pcbnew.VECTOR2I(mm(0.7), mm(0.45)))
        pad.SetRoundRectRadiusRatio(0.2)
        pad.SetLayerSet(pad.SMDMask())
        px, py = 20.2 + dx, 63.72 + dy
        pad.SetPosition(pcbnew.VECTOR2I(mm(px), mm(py)))
        ax, ay = fp.GetPosition().x, fp.GetPosition().y
        pad.SetPos0(pcbnew.VECTOR2I(int(mm(px) - ax), int(mm(py) - ay)))
        nitem = b.net(net)
        if nitem is not None:
            pad.SetNet(nitem)
        fp.Add(pad)
        b.pads.append((px, py, 0.4, net))
    b.silk("LDO", 28.0, 53.6, 1.0)
    b.smd("C1", "10u", 22.5, 55.2, "VSW", "GND")
    b.smd("C2", "10u", 34.0, 55.5, "+3V3", "GND")
    b.smd("C3", "22n", 34.0, 65.0, "BP", "GND")

    # Divider next to GPIO33 (left header index 7).
    b.smd("R1", "100k", 11.5, 46.5, "BAT+", "ADC", span=2.0)
    b.smd("R2", "100k", 24.0, 68.0, "ADC", "GND", span=2.0, vertical=True)
    b.smd("C4", "100n", 22.0, 72.0, "ADC", "GND", span=3.0)

    # Pull-ups.
    b.smd("R3", "4k7", 46.0, 18.0, "+3V3", "I2C_SDA")
    b.smd("R4", "4k7", 46.0, 21.0, "+3V3", "I2C_SCL")
    # Column pull-ups sit above the DevKit, clear of the keypad escape.
    for i, net in enumerate(("KP_C0", "KP_C1", "KP_C2", "KP_C3")):
        b.smd(f"R{5+i}", "10k", 24.0, 8.0 + i * 3.2, "+3V3", net, span=2.0)

    b.silk("MothDeck HH  66x78", 33, 2.4, 1.0)
    route(b)
    stitch_gnd(b)
    escape_gnd(b)
    print("assigning footprint ids", flush=True)
    for fp in b.board.GetFootprints():
        fp.SetFPID(pcbnew.LIB_ID("MothDeck", fp.GetReference()))
    print("saving", flush=True)
    ds = b.board.GetDesignSettings()
    ds.m_MinClearance = mm(CLEAR)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_ViasMinSize = mm(VIA_OD)
    ds.m_ViasMinDrill = mm(VIA_DRILL)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleClearance = mm(0.2)
    ds.m_HoleToHoleMin = mm(0.25)
    ds.m_SilkClearance = mm(0.0)
    ds.m_SolderMaskExpansion = 0
    ds.m_SolderMaskMinWidth = 0
    path = os.path.join(ROOT, "mothdeck-handheld.kicad_pcb")
    b.board.Save(path)
    return path, b


def _pad_already_connected(b, x, y, net):
    """True when existing copper of this net already overlaps the pad."""
    rad = 0.5
    for px, py, pr, n in b.pads:
        if n == net and abs(px - x) < 0.02 and abs(py - y) < 0.02:
            rad = pr
            break
    smd = rad < 0.7
    for tr in b.board.GetTracks():
        if (tr.GetNetname() or "") != net:
            continue
        if tr.Type() == pcbnew.PCB_VIA_T:
            if smd:
                continue
            vx = pcbnew.ToMM(tr.GetPosition().x)
            vy = pcbnew.ToMM(tr.GetPosition().y)
            if math.hypot(vx - x, vy - y) <= rad + VIA_OD / 2.0:
                return True
            continue
        if smd and tr.GetLayer() != pcbnew.F_Cu:
            continue
        a, c = tr.GetStart(), tr.GetEnd()
        dist = _seg_dist(
            x, y,
            pcbnew.ToMM(a.x), pcbnew.ToMM(a.y),
            pcbnew.ToMM(c.x), pcbnew.ToMM(c.y),
        )
        if dist <= rad + pcbnew.ToMM(tr.GetWidth()) / 2.0:
            return True
    return False


def route(b):
    """Grid router. GND is left for the pour. NC nets are not routed."""
    groups = {}
    for x, y, _r, net in b.pads:
        if not net or net.startswith("NC"):
            continue
        groups.setdefault(net, []).append((x, y))
    # Wider tracks for power. Ground is routed, not poured: KiCad 7's zone
    # filler crashes here, and a hand-built fill is not tied into connectivity.
    power = {"+3V3", "VBAT", "VSW", "BAT+", "GND"}
    order = sorted(groups, key=lambda n: (1 if n == "GND" else 0, _span(groups[n])))
    grid_w = int(PCB_W / GRID) + 2
    grid_h = int(PCB_H / GRID) + 2
    # Pad keep-outs and routed copper are separate. Opening a pad must not
    # also open a cell that already holds another net's track.
    static_b = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    # 0 is empty. Any other value is the net id that already owns the cell.
    # The owner mask is the fat clearance corridor. Same-net routes may only
    # walk the centerline, or a later pass redraws a track on the corridor
    # edge and into the neighbouring net.
    owner = [array.array("H", bytes(grid_w * grid_h * 2)) for _ in (0, 1)]
    spine = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    pad_open = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    pad_core = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    net_ids = {name: i + 1 for i, name in enumerate(groups)}
    # A via is wider than a track. Cells a track may use are not all legal
    # places to drop a via, so via sites have their own mask.
    via_blocked = bytearray(grid_w * grid_h)

    def idx(gx, gy):
        return gy * grid_w + gx

    def mark_into(mask, layer, cx, cy, radius):
        r = int(math.ceil(radius / GRID)) + 1
        gcx, gcy = _cell(cx, cy)
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if (gx - gcx) ** 2 + (gy - gcy) ** 2 <= (radius / GRID) ** 2:
                    mask[layer][idx(gx, gy)] = 1

    def mark_disk(layer, cx, cy, radius, nid):
        r = int(math.ceil(radius / GRID)) + 1
        gcx, gcy = _cell(cx, cy)
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if (gx - gcx) ** 2 + (gy - gcy) ** 2 <= (radius / GRID) ** 2:
                    i = idx(gx, gy)
                    if owner[layer][i] == 0:
                        owner[layer][i] = nid
        if 0 <= gcx < grid_w and 0 <= gcy < grid_h:
            spine[layer][idx(gcx, gcy)] = 1

    def mark_via(cx, cy, radius):
        r = int(math.ceil(radius / GRID)) + 1
        gcx, gcy = _cell(cx, cy)
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if (gx - gcx) ** 2 + (gy - gcy) ** 2 <= (radius / GRID) ** 2:
                    via_blocked[idx(gx, gy)] = 1

    def make_occupied(nid):
        def occupied(layer, x, y):
            i = idx(x, y)
            if static_b[layer][i]:
                return True
            own = owner[layer][i]
            return own != 0 and own != nid
        return occupied

    # Board edge keep-in. Via keep-out is wider than the track keep-in.
    margin = 0.6
    via_margin = 0.4 + VIA_OD / 2
    for layer in (0, 1):
        for gy in range(grid_h):
            for gx in range(grid_w):
                x = gx * GRID
                y = gy * GRID
                if x < margin or y < margin or x > PCB_W - margin or y > PCB_H - margin:
                    static_b[layer][idx(gx, gy)] = 1
                if x < via_margin or y < via_margin or x > PCB_W - via_margin or y > PCB_H - via_margin:
                    via_blocked[idx(gx, gy)] = 1
                # Module antenna. No copper here on either layer.
                if 14.0 <= x <= 43.0 and 22.0 <= y <= 29.2:
                    static_b[layer][idx(gx, gy)] = 1
                    via_blocked[idx(gx, gy)] = 1

    for x, y, rad, _net in b.pads:
        for layer in (0, 1):
            mark_into(static_b, layer, x, y, rad + CLEAR + TRACK / 2 + 0.08)
        mark_via(x, y, rad + CLEAR + VIA_OD / 2 + 0.10)
    for hx, hy in ((4.0, 4.0), (62.0, 4.0), (4.0, 74.0), (62.0, 74.0)):
        for layer in (0, 1):
            mark_into(static_b, layer, hx, hy, 1.1 + CLEAR + TRACK / 2)
        mark_via(hx, hy, 1.1 + 0.25 + VIA_OD / 2)

    def via_ok(gx, gy):
        if gx < 0 or gy < 0 or gx >= grid_w or gy >= grid_h:
            return False
        return via_blocked[idx(gx, gy)] == 0

    def clear_net_pads(net, width):
        # Open this net's pads so a trace can start on the copper, but never
        # open a cell that sits inside another pad's clearance.
        others = [(x, y, rad) for x, y, rad, n in b.pads if n and n != net and not str(n).startswith("NC")]
        freed = []
        opened = []
        for x, y, rad, n in b.pads:
            if n != net:
                continue
            r = rad + CLEAR + width / 2 + GRID
            gcx, gcy = _cell(x, y)
            for layer in (0, 1):
                pad_core[layer][idx(gcx, gcy)] = 1
            rr = int(math.ceil(r / GRID)) + 1
            for layer in (0, 1):
                for gy in range(max(0, gcy - rr), min(grid_h, gcy + rr + 1)):
                    for gx in range(max(0, gcx - rr), min(grid_w, gcx + rr + 1)):
                        if (gx - gcx) ** 2 + (gy - gcy) ** 2 > (r / GRID) ** 2:
                            continue
                        cx, cy = gx * GRID, gy * GRID
                        too_close = False
                        for ox, oy, orad in others:
                            need = orad + CLEAR + width / 2 + 0.08
                            if (cx - ox) ** 2 + (cy - oy) ** 2 < need * need:
                                too_close = True
                                break
                        if too_close:
                            continue
                        i = idx(gx, gy)
                        opened.append((layer, i))
                        pad_open[layer][i] = 1
                        if static_b[layer][i]:
                            static_b[layer][i] = 0
                            freed.append((layer, i))
        return freed, opened

    def restore(freed, opened):
        for layer, i in freed:
            static_b[layer][i] = 1
        for layer, i in opened:
            pad_open[layer][i] = 0
        for layer in (0, 1):
            pad_core[layer][:] = bytearray(grid_w * grid_h)

    def connect(net, src, target):
        width = TRACK_PWR if net in power else TRACK
        freed, opened = clear_net_pads(net, width)
        nid = net_ids[net]
        path = astar(make_occupied(nid), grid_w, grid_h, src, target, width, via_ok)
        restore(freed, opened)
        if path is None:
            return False
        paint(
            b, None, grid_w, path, net, width,
            lambda layer, cx, cy, radius, nid=nid: mark_disk(layer, cx, cy, radius, nid),
            idx, mark_via,
        )
        return True

    def gnd_at(x, y):
        best = None
        for px, py in groups["GND"]:
            d = (px - x) ** 2 + (py - y) ** 2
            if best is None or d < best[0]:
                best = (d, (px, py))
        return best[1]

    failed = []
    for net in order:
        pts = groups[net]
        width = TRACK_PWR if net in power else TRACK
        # Pads already touched by a priority track stay in the tree so the
        # maze does not draw a second track along the clearance edge.
        linked = [p for p in pts if _pad_already_connected(b, p[0], p[1], net)]
        if linked:
            tree = linked
            remaining = [p for p in pts if p not in linked]
        else:
            tree = [pts[0]]
            remaining = pts[1:]
        while remaining:
            best = None
            for target in remaining:
                for src in tree:
                    d = (src[0] - target[0]) ** 2 + (src[1] - target[1]) ** 2
                    if best is None or d < best[0]:
                        best = (d, src, target)
            _d, src, target = best
            freed, opened = clear_net_pads(net, width)
            nid = net_ids[net]
            path = astar(make_occupied(nid), grid_w, grid_h, src, target, width, via_ok)
            restore(freed, opened)
            if path is None:
                failed.append(net)
                # Drop this pad and keep stitching the rest of the net.
                remaining.remove(target)
                continue
            paint(
                b, None, grid_w, path, net, width,
                lambda layer, cx, cy, radius, nid=nid: mark_disk(layer, cx, cy, radius, nid),
                idx, mark_via,
            )
            tree.append(target)
            remaining.remove(target)
    if failed:
        print("UNROUTED", ", ".join(dict.fromkeys(failed)), flush=True)
    else:
        print("all signal nets routed", flush=True)


def _cell(x, y):
    return int(round(x / GRID)), int(round(y / GRID))


def _span(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (max(xs) - min(xs)) + (max(ys) - min(ys))


def astar(occupied, grid_w, grid_h, src, dst, width, via_ok):
    sx, sy = _cell(*src)
    dx, dy = _cell(*dst)
    sx = min(max(sx, 0), grid_w - 1)
    sy = min(max(sy, 0), grid_h - 1)
    dx = min(max(dx, 0), grid_w - 1)
    dy = min(max(dy, 0), grid_h - 1)

    def h(gx, gy):
        return abs(gx - dx) + abs(gy - dy)

    # state: layer, x, y
    start = (0, sx, sy)
    openq = [(h(sx, sy), 0, start)]
    came = {start: None}
    gscore = {start: 0}
    seen = 0
    limit = 1200000
    while openq and seen < limit:
        _f, g, cur = heapq.heappop(openq)
        seen += 1
        layer, x, y = cur
        # Land on the front copper. SMD pads exist only on F.Cu, and a
        # one-cell miss leaves the track outside a SOT-23 pad.
        if layer == 0 and x == dx and y == dy:
            return _rebuild(came, cur, cur)
        for nl, nx, ny, step in _neighbours(layer, x, y, grid_w, grid_h):
            if step > 1 and not via_ok(nx, ny):
                continue
            if occupied(nl, nx, ny):
                continue
            nxt = (nl, nx, ny)
            ng = g + step
            if ng < gscore.get(nxt, 1e18):
                gscore[nxt] = ng
                came[nxt] = cur
                heapq.heappush(openq, (ng + h(nx, ny), ng, nxt))
    goal = (0, dx, dy)
    if goal in came:
        return _rebuild(came, goal, goal)
    print(
        "  astar fail goal", dx, dy,
        "occ", occupied(0, dx, dy), occupied(1, dx, dy),
        "expanded", seen, "reached", len(came),
        flush=True,
    )
    return None


def _neighbours(layer, x, y, grid_w, grid_h):
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        nx, ny = x + dx, y + dy
        if 0 <= nx < grid_w and 0 <= ny < grid_h:
            yield layer, nx, ny, 1
    # Via onto the other layer. Cost keeps vias rare.
    yield 1 - layer, x, y, 6


def _rebuild(came, cur, _goal):
    path = [cur]
    while came[cur] is not None:
        cur = came[cur]
        path.append(cur)
    path.reverse()
    return path


def paint(b, blocked, grid_w, path, net, width, mark_disk, idx, mark_via):
    nitem = b.net(net)
    # Collapse colinear runs on one layer into segments. A layer change is a via.
    def point(cell):
        _layer, x, y = cell
        return x * GRID, y * GRID

    i = 0
    while i < len(path) - 1:
        layer = path[i][0]
        j = i + 1
        if path[j][0] != layer:
            # via at path[i]
            vx, vy = point(path[i])
            via = pcbnew.PCB_VIA(b.board)
            via.SetPosition(pcbnew.VECTOR2I(mm(vx), mm(vy)))
            via.SetWidth(mm(VIA_OD))
            via.SetDrill(mm(VIA_DRILL))
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetNet(nitem)
            b.board.Add(via)
            for lay in (0, 1):
                # Next track centre must clear the via copper plus its own half-width.
                mark_disk(lay, vx, vy, VIA_OD / 2 + CLEAR + width / 2)
            mark_via(vx, vy, VIA_OD + CLEAR)
            i = j
            continue
        # extend while direction stays constant and layer stays
        dirx = path[j][1] - path[i][1]
        diry = path[j][2] - path[i][2]
        k = j
        while k + 1 < len(path) and path[k + 1][0] == layer:
            ndx = path[k + 1][1] - path[k][1]
            ndy = path[k + 1][2] - path[k][2]
            if ndx != dirx or ndy != diry:
                break
            k += 1
        x1, y1 = point(path[i])
        x2, y2 = point(path[k])
        if (x1, y1) != (x2, y2):
            tr = pcbnew.PCB_TRACK(b.board)
            tr.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
            tr.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
            tr.SetWidth(mm(width))
            tr.SetLayer(pcbnew.F_Cu if layer == 0 else pcbnew.B_Cu)
            tr.SetNet(nitem)
            b.board.Add(tr)
            # Block a corridor so the next net stays clear.
            steps = max(abs(path[k][1] - path[i][1]), abs(path[k][2] - path[i][2]), 1)
            for s in range(steps + 1):
                gx = path[i][1] + (path[k][1] - path[i][1]) * s // steps
                gy = path[i][2] + (path[k][2] - path[i][2]) * s // steps
                # Centre-to-centre keep-out is one track plus the clearance.
                mark_disk(layer, gx * GRID, gy * GRID, width + CLEAR + 0.08)
                mark_via(gx * GRID, gy * GRID, width / 2 + CLEAR + VIA_OD / 2)
        i = k


def _seg_dist(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    l2 = dx * dx + dy * dy
    if l2 < 1e-12:
        return math.hypot(px - x1, py - y1)
    t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / l2))
    return math.hypot(px - (x1 + t * dx), py - (y1 + t * dy))


def nudge_vias(b):
    """Move a via off a pad only when the new site also clears other copper."""
    board = b.board
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            pos = pad.GetPosition()
            sz = pad.GetSize()
            pads.append((
                pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                max(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)) / 2.0,
                pad.GetNetname() or "",
            ))
    items = list(board.GetTracks())

    def clear_of(nx, ny, vnet, ignore):
        for px, py, prad, pnet in pads:
            if pnet == vnet:
                continue
            if math.hypot(nx - px, ny - py) < prad + VIA_OD / 2 + CLEAR + 0.02:
                return False
        for tr in items:
            if tr is ignore or (tr.GetNetname() or "") == vnet:
                continue
            if tr.Type() == pcbnew.PCB_VIA_T:
                ox = pcbnew.ToMM(tr.GetPosition().x)
                oy = pcbnew.ToMM(tr.GetPosition().y)
                if math.hypot(nx - ox, ny - oy) < VIA_OD + CLEAR + 0.02:
                    return False
                continue
            a, c = tr.GetStart(), tr.GetEnd()
            dist = _seg_dist(
                nx, ny,
                pcbnew.ToMM(a.x), pcbnew.ToMM(a.y),
                pcbnew.ToMM(c.x), pcbnew.ToMM(c.y),
            )
            half = pcbnew.ToMM(tr.GetWidth()) / 2
            if dist < half + VIA_OD / 2 + CLEAR + 0.02:
                return False
        return True

    moved = 0
    for item in items:
        if item.Type() != pcbnew.PCB_VIA_T:
            continue
        vx = pcbnew.ToMM(item.GetPosition().x)
        vy = pcbnew.ToMM(item.GetPosition().y)
        vnet = item.GetNetname() or ""
        if clear_of(vx, vy, vnet, item):
            continue
        best = None
        for ang in range(0, 360, 15):
            rad = math.radians(ang)
            for dist in (0.35, 0.55, 0.8, 1.1, 1.5):
                nx = min(max(vx + math.cos(rad) * dist, 1.2), PCB_W - 1.2)
                ny = min(max(vy + math.sin(rad) * dist, 1.2), PCB_H - 1.2)
                if clear_of(nx, ny, vnet, item):
                    best = (nx, ny)
                    break
            if best:
                break
        if best is None:
            continue
        nx, ny = best
        old = item.GetPosition()
        item.SetPosition(pcbnew.VECTOR2I(mm(nx), mm(ny)))
        new = item.GetPosition()
        for tr in items:
            if tr.Type() == pcbnew.PCB_VIA_T or (tr.GetNetname() or "") != vnet:
                continue
            if tr.GetStart() == old:
                tr.SetStart(new)
            if tr.GetEnd() == old:
                tr.SetEnd(new)
        moved += 1
    print("nudged vias", moved, flush=True)


def stitch_gnd(b):
    """Join ground pads the maze could not reach with a short L to the nearest ground track."""
    board = b.board
    items = list(board.GetTracks())
    gnd_items = [t for t in items if (t.GetNetname() or "") == "GND"]
    pads = []
    gnd_pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            pos = pad.GetPosition()
            sz = pad.GetSize()
            rec = (
                pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                pcbnew.ToMM(sz.x) / 2.0, pcbnew.ToMM(sz.y) / 2.0,
                pad.GetNetname() or "",
                pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD,
            )
            pads.append(rec)
            if rec[4] == "GND":
                gnd_pads.append((rec, fp.GetReference()))

    def hit_gnd(px, py, hx, hy, smd):
        for tr in gnd_items:
            if tr.Type() == pcbnew.PCB_VIA_T:
                d = math.hypot(px - pcbnew.ToMM(tr.GetPosition().x), py - pcbnew.ToMM(tr.GetPosition().y))
                if d <= max(hx, hy) + VIA_OD / 2:
                    return True
                continue
            if smd and tr.GetLayer() != pcbnew.F_Cu:
                continue
            a, c = tr.GetStart(), tr.GetEnd()
            dist = _seg_dist(px, py, pcbnew.ToMM(a.x), pcbnew.ToMM(a.y), pcbnew.ToMM(c.x), pcbnew.ToMM(c.y))
            if dist <= max(hx, hy) + pcbnew.ToMM(tr.GetWidth()) / 2:
                return True
        return False

    def segment_clear(x1, y1, x2, y2, layer):
        steps = max(int(math.hypot(x2 - x1, y2 - y1) / 0.15), 1)
        for s in range(steps + 1):
            x = x1 + (x2 - x1) * s / steps
            y = y1 + (y2 - y1) * s / steps
            for px, py, hx, hy, pnet, _smd in pads:
                if pnet == "GND":
                    continue
                dx = max(abs(x - px) - hx, 0)
                dy = max(abs(y - py) - hy, 0)
                if math.hypot(dx, dy) < TRACK / 2 + CLEAR + 0.02:
                    return False
            for tr in items:
                if (tr.GetNetname() or "") == "GND":
                    continue
                if tr.Type() == pcbnew.PCB_VIA_T:
                    if math.hypot(x - pcbnew.ToMM(tr.GetPosition().x), y - pcbnew.ToMM(tr.GetPosition().y)) < VIA_OD / 2 + TRACK / 2 + CLEAR:
                        return False
                    continue
                if tr.GetLayer() != layer:
                    continue
                a, c = tr.GetStart(), tr.GetEnd()
                dist = _seg_dist(x, y, pcbnew.ToMM(a.x), pcbnew.ToMM(a.y), pcbnew.ToMM(c.x), pcbnew.ToMM(c.y))
                if dist < pcbnew.ToMM(tr.GetWidth()) / 2 + TRACK / 2 + CLEAR:
                    return False
        return True

    def add_seg(x1, y1, x2, y2, layer):
        if abs(x1 - x2) < 0.02 and abs(y1 - y2) < 0.02:
            return
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        tr.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        tr.SetWidth(mm(TRACK))
        tr.SetLayer(layer)
        tr.SetNet(b.net("GND"))
        board.Add(tr)
        items.append(tr)
        gnd_items.append(tr)

    stitched = 0
    for (px, py, hx, hy, _net, smd), ref in gnd_pads:
        if hit_gnd(px, py, hx, hy, smd):
            continue
        best = None
        for tr in list(gnd_items):
            if tr.Type() == pcbnew.PCB_VIA_T:
                qx = pcbnew.ToMM(tr.GetPosition().x)
                qy = pcbnew.ToMM(tr.GetPosition().y)
                layer = pcbnew.F_Cu if smd else pcbnew.B_Cu
            else:
                if smd and tr.GetLayer() != pcbnew.F_Cu:
                    continue
                a, c = tr.GetStart(), tr.GetEnd()
                x1, y1 = pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)
                x2, y2 = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
                dx, dy = x2 - x1, y2 - y1
                l2 = dx * dx + dy * dy
                t = 0 if l2 < 1e-9 else max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / l2))
                qx, qy = x1 + t * dx, y1 + t * dy
                layer = tr.GetLayer()
            dist = math.hypot(qx - px, qy - py)
            if best is None or dist < best[0]:
                best = (dist, qx, qy, layer)
        if best is None:
            print("no ground near", ref, flush=True)
            continue
        _d, qx, qy, layer = best
        layer = pcbnew.F_Cu if smd else layer
        candidates = [
            [(px, py, qx, py), (qx, py, qx, qy)],
            [(px, py, px, qy), (px, qy, qx, qy)],
        ]
        placed = False
        for legs in candidates:
            if all(segment_clear(x1, y1, x2, y2, layer) for x1, y1, x2, y2 in legs):
                for x1, y1, x2, y2 in legs:
                    add_seg(x1, y1, x2, y2, layer)
                placed = True
                break
        if placed:
            stitched += 1
        else:
            print("unstitched", ref, round(px, 2), round(py, 2), flush=True)
    print("stitched ground", stitched, flush=True)


def escape_gnd(b):
    """Finish ground pads the maze left sealed, using the real copper as obstacles.

    The grid router keeps an extra margin that closes some legal gaps. This
    pass walks those gaps at the DRC clearance and ties the pad to copper
    that is already on the net.
    """
    board = b.board
    items = list(board.GetTracks())
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            pos = pad.GetPosition()
            sz = pad.GetSize()
            pads.append((
                pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                pcbnew.ToMM(sz.x) / 2.0, pcbnew.ToMM(sz.y) / 2.0,
                pad.GetNetname() or "",
                pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD,
                fp.GetReference(),
            ))
    gnd_pads = [p for p in pads if p[4] == "GND"]
    if not gnd_pads:
        return

    def touches(px, py, hx, hy, smd):
        for tr in items:
            if (tr.GetNetname() or "") != "GND":
                continue
            if tr.Type() == pcbnew.PCB_VIA_T:
                d = math.hypot(px - pcbnew.ToMM(tr.GetPosition().x), py - pcbnew.ToMM(tr.GetPosition().y))
                if d <= max(hx, hy) + VIA_OD / 2.0:
                    return True
                continue
            if smd and tr.GetLayer() != pcbnew.F_Cu:
                continue
            a, c = tr.GetStart(), tr.GetEnd()
            dist = _seg_dist(px, py, pcbnew.ToMM(a.x), pcbnew.ToMM(a.y), pcbnew.ToMM(c.x), pcbnew.ToMM(c.y))
            if dist <= max(hx, hy) + pcbnew.ToMM(tr.GetWidth()) / 2.0:
                return True
        return False

    grid_w = int(PCB_W / GRID) + 2
    grid_h = int(PCB_H / GRID) + 2

    def mark(mask, layer, cx, cy, radius):
        if radius <= 0:
            return
        r = int(math.ceil(radius / GRID)) + 1
        gcx, gcy = _cell(cx, cy)
        rr = (radius / GRID) ** 2
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if (gx - gcx) ** 2 + (gy - gcy) ** 2 <= rr:
                    mask[layer][gy * grid_w + gx] = 1

    def mark_via_at(cx, cy, radius):
        r = int(math.ceil(radius / GRID)) + 1
        gcx, gcy = _cell(cx, cy)
        rr = (radius / GRID) ** 2
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if (gx - gcx) ** 2 + (gy - gcy) ** 2 <= rr:
                    via_blocked[gy * grid_w + gx] = 1

    blocked = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    via_blocked = bytearray(grid_w * grid_h)
    goal = [bytearray(grid_w * grid_h), bytearray(grid_w * grid_h)]
    margin = 0.5
    for layer in (0, 1):
        for gy in range(grid_h):
            for gx in range(grid_w):
                x, y = gx * GRID, gy * GRID
                if x < margin or y < margin or x > PCB_W - margin or y > PCB_H - margin:
                    blocked[layer][gy * grid_w + gx] = 1
                if 14.0 <= x <= 43.0 and 22.0 <= y <= 29.2:
                    blocked[layer][gy * grid_w + gx] = 1
                    via_blocked[gy * grid_w + gx] = 1
    def cover_rect(px, py, hx, hy, extra):
        reach = max(hx, hy) + extra
        r = int(math.ceil(reach / GRID)) + 1
        gcx, gcy = _cell(px, py)
        cells = []
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                dx = max(abs(gx * GRID - px) - hx, 0.0)
                dy = max(abs(gy * GRID - py) - hy, 0.0)
                if math.hypot(dx, dy) < extra:
                    cells.append(gy * grid_w + gx)
        return cells

    for px, py, hx, hy, net, _smd, _ref in pads:
        # Never drop a via on a pad drill, ground included.
        for i in cover_rect(px, py, hx, hy, CLEAR + VIA_OD / 2.0 + 0.18):
            via_blocked[i] = 1
        if net == "GND":
            continue
        for layer in (0, 1):
            for i in cover_rect(px, py, hx, hy, CLEAR + TRACK / 2.0 + 0.10):
                blocked[layer][i] = 1
    for hx, hy in ((4.0, 4.0), (62.0, 4.0), (4.0, 74.0), (62.0, 74.0)):
        for layer in (0, 1):
            mark(blocked, layer, hx, hy, 1.1 + CLEAR + TRACK / 2.0)
        r = int(math.ceil(1.6 / GRID)) + 1
        gcx, gcy = _cell(hx, hy)
        for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
            for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                if math.hypot(gx * GRID - hx, gy * GRID - hy) <= 1.55:
                    via_blocked[gy * grid_w + gx] = 1

    def paint_seg(x1, y1, x2, y2, layer):
        if abs(x1 - x2) < 0.02 and abs(y1 - y2) < 0.02:
            return
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        tr.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        tr.SetWidth(mm(TRACK))
        tr.SetLayer(pcbnew.F_Cu if layer == 0 else pcbnew.B_Cu)
        tr.SetNet(b.net("GND"))
        board.Add(tr)
        items.append(tr)

    def paint_via(x, y):
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        via.SetWidth(mm(VIA_OD))
        via.SetDrill(mm(VIA_DRILL))
        via.SetViaType(pcbnew.VIATYPE_THROUGH)
        via.SetNet(b.net("GND"))
        board.Add(via)
        items.append(via)

    # Foreign copper blocks; ground copper is the target.
    for tr in list(items):
        net = tr.GetNetname() or ""
        if tr.Type() == pcbnew.PCB_VIA_T:
            vx = pcbnew.ToMM(tr.GetPosition().x)
            vy = pcbnew.ToMM(tr.GetPosition().y)
            if net == "GND":
                for layer in (0, 1):
                    mark(goal, layer, vx, vy, VIA_OD / 2.0)
            else:
                for layer in (0, 1):
                    mark(blocked, layer, vx, vy, VIA_OD / 2.0 + CLEAR + TRACK / 2.0 + 0.015)
                r = int(math.ceil((VIA_OD + CLEAR) / GRID)) + 1
                gcx, gcy = _cell(vx, vy)
                for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
                    for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                        if math.hypot(gx * GRID - vx, gy * GRID - vy) <= VIA_OD + CLEAR:
                            via_blocked[gy * grid_w + gx] = 1
            continue
        a, c = tr.GetStart(), tr.GetEnd()
        x1, y1 = pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)
        x2, y2 = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
        layer = 0 if tr.GetLayer() == pcbnew.F_Cu else 1
        half = pcbnew.ToMM(tr.GetWidth()) / 2.0
        steps = max(int(math.hypot(x2 - x1, y2 - y1) / (GRID * 0.5)), 1)
        for s in range(steps + 1):
            x = x1 + (x2 - x1) * s / steps
            y = y1 + (y2 - y1) * s / steps
            if net == "GND":
                mark(goal, layer, x, y, half)
            else:
                mark(blocked, layer, x, y, half + CLEAR + TRACK / 2.0 + 0.10)
                mark_via_at(x, y, half + CLEAR + VIA_OD / 2.0 + 0.05)

    joined = 0
    for px, py, hx, hy, _net, smd, ref in gnd_pads:
        if touches(px, py, hx, hy, smd):
            continue
        # Open this pad. A PTH pad is a target on both layers.
        for layer in (0, 1):
            if smd and layer == 1:
                continue
            gcx, gcy = _cell(px, py)
            rad = max(hx, hy) * 0.9
            r = int(math.ceil(rad / GRID)) + 1
            for gy in range(max(0, gcy - r), min(grid_h, gcy + r + 1)):
                for gx in range(max(0, gcx - r), min(grid_w, gcx + r + 1)):
                    if math.hypot(gx * GRID - px, gy * GRID - py) <= rad:
                        blocked[layer][gy * grid_w + gx] = 0
        start_layers = (0,) if smd else (0, 1)
        start = _cell(px, py)
        came = {}
        q = deque()
        for layer in start_layers:
            node = (layer, start[0], start[1])
            came[node] = None
            q.append(node)
        found = None
        seen = 0
        while q and seen < 400000:
            layer, gx, gy = q.popleft()
            seen += 1
            i = gy * grid_w + gx
            if goal[layer][i] and (gx, gy) != start:
                found = (layer, gx, gy)
                break
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = gx + dx, gy + dy
                if not (0 <= nx < grid_w and 0 <= ny < grid_h):
                    continue
                nxt = (layer, nx, ny)
                if nxt in came or blocked[layer][ny * grid_w + nx]:
                    continue
                came[nxt] = (layer, gx, gy)
                q.append(nxt)
            if via_blocked[i] == 0:
                other = 1 - layer
                nxt = (other, gx, gy)
                if nxt not in came and blocked[other][i] == 0:
                    came[nxt] = (layer, gx, gy)
                    q.append(nxt)
        if found is None:
            print("escape fail", ref, round(px, 2), round(py, 2), "seen", seen, flush=True)
            continue
        # Walk back and draw.
        path = [found]
        cur = found
        while came[cur] is not None:
            cur = came[cur]
            path.append(cur)
        path.reverse()
        i = 0
        while i < len(path) - 1:
            layer = path[i][0]
            j = i + 1
            if path[j][0] != layer:
                paint_via(path[i][1] * GRID, path[i][2] * GRID)
                for lay in (0, 1):
                    mark(goal, lay, path[i][1] * GRID, path[i][2] * GRID, VIA_OD / 2.0)
                i = j
                continue
            dirx = path[j][1] - path[i][1]
            diry = path[j][2] - path[i][2]
            k = j
            while k + 1 < len(path) and path[k + 1][0] == layer:
                if path[k + 1][1] - path[k][1] != dirx or path[k + 1][2] - path[k][2] != diry:
                    break
                k += 1
            paint_seg(path[i][1] * GRID, path[i][2] * GRID, path[k][1] * GRID, path[k][2] * GRID, layer)
            steps = max(abs(path[k][1] - path[i][1]), abs(path[k][2] - path[i][2]), 1)
            for s in range(steps + 1):
                gx = path[i][1] + (path[k][1] - path[i][1]) * s // steps
                gy = path[i][2] + (path[k][2] - path[i][2]) * s // steps
                mark(goal, layer, gx * GRID, gy * GRID, TRACK / 2.0)
            i = k
        joined += 1
    print("escaped ground", joined, flush=True)


def nudge_clearance(b):
    """Slide a track that misses the 0.2 mm pad clearance by a few hundredths."""
    board = b.board
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            pos = pad.GetPosition()
            sz = pad.GetSize()
            pads.append((
                pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y),
                pcbnew.ToMM(sz.x) / 2.0, pcbnew.ToMM(sz.y) / 2.0,
                pad.GetNetname() or "",
            ))

    def gap_to_pads(x1, y1, x2, y2, net):
        worst = 10.0
        worst_pad = None
        steps = max(int(math.hypot(x2 - x1, y2 - y1) / 0.05), 1)
        for s in range(steps + 1):
            x = x1 + (x2 - x1) * s / steps
            y = y1 + (y2 - y1) * s / steps
            for px, py, hx, hy, pnet in pads:
                if pnet == net:
                    continue
                dx = max(abs(x - px) - hx, 0.0)
                dy = max(abs(y - py) - hy, 0.0)
                gap = math.hypot(dx, dy) - TRACK / 2.0
                if gap < worst:
                    worst = gap
                    worst_pad = (px, py)
        return worst, worst_pad

    moved = 0
    for tr in list(board.GetTracks()):
        if tr.Type() == pcbnew.PCB_VIA_T:
            continue
        net = tr.GetNetname() or ""
        a, c = tr.GetStart(), tr.GetEnd()
        x1, y1 = pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)
        x2, y2 = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
        gap, pad = gap_to_pads(x1, y1, x2, y2, net)
        if gap >= 0.20 or pad is None:
            continue
        px, py = pad
        mx, my = (x1 + x2) / 2.0, (y1 + y2) / 2.0
        vx, vy = mx - px, my - py
        norm = math.hypot(vx, vy) or 1.0
        vx, vy = vx / norm, vy / norm
        for dist in (0.08, 0.15, 0.25, 0.40):
            nx1, ny1 = x1 + vx * dist, y1 + vy * dist
            nx2, ny2 = x2 + vx * dist, y2 + vy * dist
            ngap, _pad = gap_to_pads(nx1, ny1, nx2, ny2, net)
            if ngap >= 0.21:
                tr.SetStart(pcbnew.VECTOR2I(mm(nx1), mm(ny1)))
                tr.SetEnd(pcbnew.VECTOR2I(mm(nx2), mm(ny2)))
                moved += 1
                break
    print("nudged tracks", moved, flush=True)


def _rect_poly(x1, y1, x2, y2):
    poly = pcbnew.SHAPE_POLY_SET()
    poly.NewOutline()
    poly.Append(int(mm(x1)), int(mm(y1)))
    poly.Append(int(mm(x2)), int(mm(y1)))
    poly.Append(int(mm(x2)), int(mm(y2)))
    poly.Append(int(mm(x1)), int(mm(y2)))
    return poly


def _islands_touching(poly, points):
    """Keep only filled outlines that cover a GND pad, so DRC sees no islands."""
    kept = pcbnew.SHAPE_POLY_SET()
    if not points:
        return poly
    for i in range(poly.OutlineCount()):
        chain = poly.Outline(i)
        single = pcbnew.SHAPE_POLY_SET()
        single.NewOutline()
        for k in range(chain.GetPointCount()):
            p = chain.CPoint(k)
            single.Append(int(p.x), int(p.y))
        if any(single.Contains(pt) for pt in points):
            kept.AddOutline(chain)
    if kept.OutlineCount() == 0:
        return poly
    return kept


def _union(pieces):
    acc = None
    for piece in pieces:
        if acc is None:
            acc = piece
        else:
            acc.BooleanAdd(piece, 0)
    return acc


def pour(b):
    """Fill GND on both layers without ZONE_FILLER.

    KiCad 7's Python ZONE_FILLER segfaults in this environment. The pour is
    the board rectangle minus every other net's copper, the mounting holes,
    and the antenna window. GND pads are left covered so the fill ties them.
    """
    gnd = b.net("GND")
    # Extra 0.05 mm so nanometre rounding cannot land a 0.2 mm clearance short.
    keep = CLEAR + 0.12
    edge = 0.35
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        # The zone anchor has to sit on real GND copper. A board-sized outline
        # whose corner is bare soldermask shows up as an unconnected zone.
        anchor = None
        for fp in b.board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() != "GND":
                    continue
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and not pad.IsOnLayer(layer):
                    continue
                anchor = pad.GetPosition()
                break
            if anchor is not None:
                break
        if anchor is None:
            anchor = pcbnew.VECTOR2I(mm(edge), mm(edge))
        zone = b.board.AddArea(
            None, gnd.GetNetCode(), layer, anchor,
            pcbnew.ZONE_BORDER_DISPLAY_STYLE_NO_HATCH,
        )
        outline = zone.Outline()
        ax, ay = anchor.x, anchor.y
        outline.Append(ax + int(mm(0.4)), ay)
        outline.Append(ax + int(mm(0.4)), ay + int(mm(0.4)))
        outline.Append(ax, ay + int(mm(0.4)))
        zone.SetNet(gnd)
        zone.SetFillMode(pcbnew.ZONE_FILL_MODE_POLYGONS)
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        zone.SetMinThickness(mm(0.15))
        zone.SetLocalClearance(mm(CLEAR))
        zone.SetThermalReliefGap(mm(0.3))
        zone.SetThermalReliefSpokeWidth(mm(0.3))
        zone.SetAssignedPriority(0)

        pieces = [_rect_poly(14.0, 22.0, 43.0, 29.2)]
        for hx, hy in ((4.0, 4.0), (62.0, 4.0), (4.0, 74.0), (62.0, 74.0)):
            r = 1.1 + 0.30
            pieces.append(_rect_poly(hx - r, hy - r, hx + r, hy + r))
        for fp in b.board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    continue
                if pad.GetNetname() == "GND":
                    continue
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and not pad.IsOnLayer(layer):
                    continue
                pos = pad.GetPosition()
                sz = pad.GetSize()
                cx, cy = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
                hx, hy = pcbnew.ToMM(sz.x) / 2 + keep, pcbnew.ToMM(sz.y) / 2 + keep
                pieces.append(_rect_poly(cx - hx, cy - hy, cx + hx, cy + hy))
        for item in b.board.GetTracks():
            if item.GetNetname() == "GND":
                continue
            if item.Type() == pcbnew.PCB_VIA_T:
                pos = item.GetPosition()
                cx, cy = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
                r = pcbnew.ToMM(item.GetWidth()) / 2 + keep
                pieces.append(_rect_poly(cx - r, cy - r, cx + r, cy + r))
                continue
            if item.GetLayer() != layer:
                continue
            a, c = item.GetStart(), item.GetEnd()
            x1, y1 = pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)
            x2, y2 = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
            r = pcbnew.ToMM(item.GetWidth()) / 2 + keep
            pieces.append(_rect_poly(min(x1, x2) - r, min(y1, y2) - r, max(x1, x2) + r, max(y1, y2) + r))

        filled = _rect_poly(edge, edge, PCB_W - edge, PCB_H - edge)
        obstacles = _union(pieces)
        if obstacles is not None:
            filled.BooleanSubtract(obstacles, 0)
        # Fracture turns holes into slitted outlines. KiCad 7 drops true holes
        # from the filled polygon when it writes the board.
        filled.Fracture(pcbnew.SHAPE_POLY_SET.PM_FAST)
        gnd_pts = []
        for fp in b.board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() != "GND":
                    continue
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and not pad.IsOnLayer(layer):
                    continue
                gnd_pts.append(pad.GetPosition())
        kept = _islands_touching(filled, gnd_pts)
        zone.SetFilledPolysList(layer, kept)
        zone.SetIsFilled(True)
        zone.SetNeedRefill(False)
        print("pour", layer, "islands", kept.OutlineCount(), flush=True)


def write_project(path):
    text = """{
  "board": {
    "design_settings": {
      "defaults": {},
      "diff_pair_dimensions": [],
      "drc_exclusions": [],
      "rules": {
        "min_clearance": 0.2,
        "min_track_width": 0.2,
        "min_via_diameter": 0.8,
        "min_via_drill": 0.4,
        "min_hole_clearance": 0.2,
        "min_hole_to_hole": 0.25
      },
      "track_widths": [0.25, 0.5],
      "via_dimensions": [{"diameter": 0.8, "drill": 0.4}]
    },
    "layer_presets": [],
    "viewports": []
  },
  "boards": [],
  "cvpcb": {"equivalence_files": []},
  "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
  "meta": {"filename": "mothdeck-handheld.kicad_pro", "version": 1},
  "net_settings": {
    "classes": [{
      "name": "Default",
      "clearance": 0.2,
      "track_width": 0.25,
      "via_diameter": 0.8,
      "via_drill": 0.4,
      "microvia_diameter": 0.3,
      "microvia_drill": 0.1,
      "diff_pair_width": 0.2,
      "diff_pair_gap": 0.25,
      "diff_pair_via_gap": 0.25
    }],
    "netclass_assignments": null,
    "netclass_patterns": []
  },
  "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "specctra_dsn": "", "step": "", "vrml": ""}, "page_layout_descr_file": ""},
  "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
  "sheets": [],
  "text_variables": {}
}
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def export_fab(pcb_path):
    out = os.path.join(ROOT, "fab")
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    gerber_dir = os.path.join(out, "gerber")
    os.makedirs(gerber_dir)
    layers = "F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,F.Paste,B.Paste,Edge.Cuts"
    subprocess.check_call([
        "kicad-cli", "pcb", "export", "gerbers",
        "--output", gerber_dir,
        "--layers", layers,
        "--no-x2",
        pcb_path,
    ])
    subprocess.check_call([
        "kicad-cli", "pcb", "export", "drill",
        "--output", gerber_dir + os.sep,
        "--format", "excellon",
        "--excellon-units", "mm",
        "--excellon-separate-th",
        "--generate-map",
        "--map-format", "gerberx2",
        pcb_path,
    ])
    zip_path = os.path.join(ROOT, "mothdeck-handheld-gerbers.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in sorted(os.listdir(gerber_dir)):
            zf.write(os.path.join(gerber_dir, name), arcname=name)
    print("gerbers", zip_path)

    sch = os.path.join(ROOT, "mothdeck-handheld.kicad_sch")
    subprocess.check_call(["kicad-cli", "sch", "export", "pdf", "--output", os.path.join(out, "schematic.pdf"), sch])
    subprocess.check_call(["kicad-cli", "sch", "export", "svg", "--output", out, sch])
    # Board renders: copper + silk, board area only.
    for layer, name in (
        ("F.Cu,F.SilkS,F.Mask,Edge.Cuts", "pcb-top.svg"),
        ("B.Cu,B.SilkS,B.Mask,Edge.Cuts", "pcb-bottom.svg"),
    ):
        subprocess.check_call([
            "kicad-cli", "pcb", "export", "svg",
            "--output", os.path.join(out, name),
            "--layers", layer,
            "--page-size-mode", "2",
            "--exclude-drawing-sheet",
            pcb_path,
        ])
    return out, zip_path


def drc(pcb_path):
    import wx
    if wx.GetApp() is None:
        wx.App(False)
    report = os.path.join(ROOT, "drc-report.txt")
    board = pcbnew.LoadBoard(pcb_path)
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(CLEAR)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_ViasMinSize = mm(VIA_OD)
    ds.m_ViasMinDrill = mm(VIA_DRILL)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleClearance = mm(0.2)
    ds.m_HoleToHoleMin = mm(0.25)
    ds.m_SilkClearance = mm(0.0)
    board.BuildConnectivity()
    ok = pcbnew.WriteDRCReport(board, report, pcbnew.EDA_UNITS_MILLIMETRES, True)
    print("drc write", ok)
    text = open(report, encoding="utf-8", errors="replace").read()
    print(text[:4000])
    return text


def export_footprint_library(pcb_path):
    """Write a project footprint lib that matches the generated footprints.

    Headless DRC warns when a footprint has an empty library id, and it warns
    again when the library copy differs. The lib is the board footprint with
    placement, nets, and timestamps removed.
    """
    import re
    pretty = os.path.join(ROOT, "mothdeck.pretty")
    if os.path.isdir(pretty):
        shutil.rmtree(pretty)
    os.makedirs(pretty)
    text = open(pcb_path, encoding="utf-8").read()
    key = "(footprint "
    i = 0
    written = 0
    while True:
        j = text.find(key, i)
        if j < 0:
            break
        depth = 0
        k = j
        end = None
        while k < len(text):
            if text[k] == "(":
                depth += 1
            elif text[k] == ")":
                depth -= 1
                if depth == 0:
                    end = k + 1
                    break
            k += 1
        if end is None:
            break
        block = text[j:end]
        i = end
        m = re.search(r'\(fp_text reference "([^"]+)"', block)
        if not m:
            continue
        ref = m.group(1)
        block = re.sub(r"\(footprint \"[^\"]*\"", '(footprint "%s"' % ref, block, count=1)
        if "(version " not in block[:80]:
            block = block.replace('(layer "F.Cu")', '(version 20221018) (generator mothdeck)\n    (layer "F.Cu")', 1)
        block = re.sub(r"\s*\(net \d+ \"[^\"]*\"\)", "", block)
        block = re.sub(r"\s*\(tstamp [0-9a-fA-F-]+\)", "", block)
        block = re.sub(r"\n\s*\(at [-+0-9.]+ [-+0-9.]+\)", "\n    (at 0 0)", block, count=1)
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", ref)
        with open(os.path.join(pretty, safe + ".kicad_mod"), "w", encoding="utf-8") as f:
            f.write(block + "\n")
        written += 1
    table = (
        "(fp_lib_table\n"
        "  (version 7)\n"
        '  (lib (name "MothDeck")(type "KiCad")(uri "${KIPRJMOD}/mothdeck.pretty")'
        '(options "")(descr "MothDeck handheld footprints"))\n'
        ")\n"
    )
    with open(os.path.join(ROOT, "fp-lib-table"), "w", encoding="utf-8") as f:
        f.write(table)
    cfg_dir = os.path.expanduser("~/.config/kicad/7.0")
    os.makedirs(cfg_dir, exist_ok=True)
    cfg = os.path.join(cfg_dir, "fp-lib-table")
    entry = (
        '  (lib (name "MothDeck")(type "KiCad")(uri "%s")'
        '(options "")(descr "MothDeck handheld footprints"))\n' % pretty
    )
    if os.path.exists(cfg):
        current = open(cfg, encoding="utf-8").read()
        current = re.sub(r"\s*\(lib \(name \"MothDeck\"\).*?\)\n", "\n", current)
        current = current.replace("(fp_lib_table\n", "(fp_lib_table\n" + entry, 1)
    else:
        current = "(fp_lib_table\n  (version 7)\n" + entry + ")\n"
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(current)
    print("footprints", written, flush=True)


def main():
    os.makedirs(ROOT, exist_ok=True)
    write_project(os.path.join(ROOT, "mothdeck-handheld.kicad_pro"))
    write_schematic(os.path.join(ROOT, "mothdeck-handheld.kicad_sch"))
    pcb_path, _b = build_pcb()
    print("pcb", pcb_path, flush=True)
    export_footprint_library(pcb_path)
    text = drc(pcb_path)
    export_fab(pcb_path)
    # Fail the script if KiCad counted violations. The report's first lines
    # include the error count.
    if "** Found" in text:
        for line in text.splitlines():
            if line.startswith("** Found"):
                print(line)
                if "0 DRC" not in line and "Found 0" not in line:
                    # Keep going so the report is on disk; the caller checks.
                    pass


if __name__ == "__main__":
    main()
