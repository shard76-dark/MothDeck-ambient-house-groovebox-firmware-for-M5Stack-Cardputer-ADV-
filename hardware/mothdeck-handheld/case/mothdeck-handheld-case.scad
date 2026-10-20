// MothDeck handheld case.
// Outer size 154 x 96 x 22.2 mm. Y = 0 is the USB edge, Y = 96 is the antenna edge.
// The 4x4 membrane sits in the left bay, rotated 90 degrees clockwise so its
// ribbon leaves toward the left wall. The 66 x 78 mm carrier sits in the right bay,
// antenna toward Y = 96. Four M2 holes in the carrier land on posts.
//
//   openscad -D 'part="bottom"' -o case-bottom.stl mothdeck-handheld-case.scad
//   openscad -D 'part="top"'    -o case-top.stl    mothdeck-handheld-case.scad

part = "both";

W = 154;
D = 96;
BH = 20;
LID = 2.2;
WALL = 2.2;
FLOOR = 2.0;

// Carrier origin in the case. PCB local y = 0 is the antenna, so case Y runs
// the other way: case_y = pcb_y0 - pcb_local_y.
pcb_x0 = 84;
pcb_y0 = 90;

module pcb_hole(px, py) {
    translate([pcb_x0 + px, pcb_y0 - py, FLOOR])
        cylinder(h = 4.2, d = 5.2, $fn = 32);
}

module bottom() {
    difference() {
        cube([W, D, BH]);
        translate([WALL, WALL, FLOOR])
            cube([W - 2 * WALL, D - 2 * WALL, BH]);
        // DevKit micro-USB, bottom wall. The socket is the USB end of the module.
        translate([pcb_x0 + 22, -1, 8])
            cube([14, WALL + 2, 6]);
        // TP4056 USB-C, bottom wall, to the right of the DevKit socket.
        translate([pcb_x0 + 44, -1, 7.5])
            cube([12, WALL + 2, 7]);
        // PCM5102A headphone bushing, right wall.
        translate([W - WALL - 1, pcb_y0 - 34, 11])
            rotate([0, 90, 0])
                cylinder(h = WALL + 2, d = 7.2, $fn = 40);
        // Slide switch handle, left of the carrier, through the top of the wall
        // is in the lid. A small slot in the inner left of the PCB bay is not
        // required; the handle comes through the lid.
    }
    // Carrier posts. Hole is drilled in the lid screws; the post is a pilot.
    difference() {
        union() {
            pcb_hole(4, 4);
            pcb_hole(62, 4);
            pcb_hole(4, 74);
            pcb_hole(62, 74);
        }
        union() {
            translate([pcb_x0 + 4, pcb_y0 - 4, FLOOR])
                cylinder(h = 6, d = 2.2, $fn = 24);
            translate([pcb_x0 + 62, pcb_y0 - 4, FLOOR])
                cylinder(h = 6, d = 2.2, $fn = 24);
            translate([pcb_x0 + 4, pcb_y0 - 74, FLOOR])
                cylinder(h = 6, d = 2.2, $fn = 24);
            translate([pcb_x0 + 62, pcb_y0 - 74, FLOOR])
                cylinder(h = 6, d = 2.2, $fn = 24);
        }
    }
    // 603450 cell, 50 x 34 x 6 mm, under the keypad.
    difference() {
        translate([8, 30, FLOOR])
            cube([54, 38, 7]);
        translate([10, 32, FLOOR + 1.2])
            cube([50, 34, 8]);
    }
    // Lid screw posts, four corners.
    for (xy = [[6, 6], [W - 6, 6], [6, D - 6], [W - 6, D - 6]]) {
        translate([xy[0], xy[1], FLOOR])
            difference() {
                cylinder(h = BH - FLOOR, d = 6, $fn = 28);
                translate([0, 0, -1])
                    cylinder(h = BH, d = 1.8, $fn = 20);
            }
    }
}

module top() {
    difference() {
        union() {
            cube([W, D, LID]);
            // Skirt that drops over the shell.
            translate([0.4, 0.4, -2.4])
                difference() {
                    cube([W - 0.8, D - 0.8, 2.4]);
                    translate([WALL - 0.2, WALL - 0.2, -1])
                        cube([W - 2 * WALL + 0.4, D - 2 * WALL + 0.4, 4]);
                }
        }
        // Keypad window. The membrane is rotated 90 degrees clockwise;
        // the ribbon exits the left edge, outside this window.
        translate([12, 20, -4])
            cube([56, 58, 12]);
        // SSD1306 window, top of the carrier bay.
        translate([pcb_x0 + 8, pcb_y0 - 22, -4])
            cube([28, 16, 12]);
        // HT16K33 4-digit window, under the OLED.
        translate([pcb_x0 + 6, pcb_y0 - 40, -4])
            cube([40, 14, 12]);
        // Step, beat, play, record. Carrier LEDs are at y = 50.5, x = 42 + 6*i.
        for (i = [0:3]) {
            translate([pcb_x0 + 42 + i * 6, pcb_y0 - 50.5, -4])
                cylinder(h = 12, d = 3.4, $fn = 28);
        }
        // Power switch, between the bays.
        translate([pcb_x0 + 2, pcb_y0 - 46, -4])
            cube([9, 5, 12]);
        // Corner screws.
        for (xy = [[6, 6], [W - 6, 6], [6, D - 6], [W - 6, D - 6]]) {
            translate([xy[0], xy[1], -4])
                cylinder(h = 12, d = 3.4, $fn = 24);
        }
    }
}

if (part == "bottom") {
    bottom();
} else if (part == "top") {
    top();
} else {
    bottom();
    translate([0, 0, BH])
        top();
}
