// Desk stand for the Waveshare ESP32-S3-LCD-1.28 (round 240x240 display)
// Parts: "stand" (tilted holder + pedestal + base) and "cap" (press-fit back cover).
// Open in OpenSCAD, set PART, F6 to render, F7 to export STL.
// All dimensions in mm.
//
// STATUS: first draft, NOT yet test-printed. Board dimensions below are estimates —
// measure your board with calipers and update the "measure these" block first.
// Print PART="fit_test" (a 3-minute ring) before printing the full stand.

PART = "both";       // "stand", "cap", "fit_test", or "both" (assembled preview)

// ---------- measure these on your board ----------
board_d       = 38.6;  // outer diameter of the round display/board (widest point)
stack_t       = 10.0;  // front glass to tallest part on the back (incl. 12-pin header)
view_d        = 35.0;  // front window; must be > 32.4 (active area) and < board_d
usb_out       = 4.0;   // how far the USB-C socket sticks out below the round outline
usb_y         = 7.0;   // USB-C socket centre, measured back from the front glass

// ---------- your USB-C cable ----------
plug_w        = 13.5;  // plug overmold width + clearance
plug_t        = 8.0;   // plug overmold thickness + clearance
plug_straight = 24.0;  // straight length from socket face to where a right-angle plug turns
elbow_h       = 16.0;  // height of the right-angle elbow (tunnel height); ~12 is fine for straight plugs

// ---------- stand ----------
tilt     = 15;         // lean back from vertical, degrees
clr      = 0.3;        // radial clearance around the board
wall     = 2.4;        // ring wall
lip_t    = 1.2;        // front lip thickness (holds the glass edge)
spig     = 2.5;        // cap spigot length into the ring
cap_t    = 2.0;        // cap plate thickness
ped_w    = 34;         // pedestal width
ped_d    = 20;         // pedestal depth (front to back)
base_w   = 70;
base_d   = 58;
base_t   = 4;
hdr_notch= 14;         // notch in the cap for the 12-pin cable at the top (0 = none)

$fn = 96;

// ---------- derived ----------
D       = board_d + 2*clr;        // pocket diameter
R_out   = D/2 + wall;
ring_d  = lip_t + stack_t + spig; // ring depth (front to back)
tz0     = base_t + 1;             // tunnel floor (module coords)
tz1     = tz0 + elbow_h;          // tunnel roof
z_sock  = tz1 + plug_straight;    // USB socket face
cz      = z_sock + usb_out + D/2; // ring centre height
ch_y0   = usb_y - plug_t/2;       // channel front face (leaves a front skin)

// Module coords: X = width, Y = depth back from the front glass plane (Y>=0), Z = up the display.
module head_solid() {
    // ring
    translate([0, 0, cz]) rotate([-90, 0, 0]) cylinder(r=R_out, h=ring_d);
    // pedestal (extends below ground; clipped later)
    translate([-ped_w/2, 0, -40]) cube([ped_w, ped_d, cz + 40]);
    // fillet the pedestal into the ring sides
    hull() {
        translate([0, 0, cz]) rotate([-90, 0, 0]) cylinder(r=ped_w/2, h=ring_d);
        translate([-ped_w/2, 0, cz - R_out - 6]) cube([ped_w, ring_d, 1]);
    }
}

module head_cuts() {
    // front window, with a 45-degree chamfer on the front face
    translate([0, -1, cz]) rotate([-90, 0, 0]) cylinder(d=view_d, h=ring_d + 2);
    translate([0, -0.01, cz]) rotate([-90, 0, 0]) cylinder(d1=view_d + 2*lip_t, d2=view_d, h=lip_t);
    // board pocket, open to the back
    translate([0, lip_t, cz]) rotate([-90, 0, 0]) cylinder(d=D, h=100);
    // recess so the cap plate sits flush against the ring and clears the pedestal
    translate([0, ring_d, cz]) rotate([-90, 0, 0]) cylinder(r=R_out + 0.4, h=100);
    // USB socket + plug channel down to the tunnel, open to the back
    translate([-plug_w/2, ch_y0, tz0]) cube([plug_w, 100, cz - tz0]);
    // tunnel across the full width so a right-angle plug can turn left or right
    translate([-100, ch_y0, tz0]) cube([200, ped_d - ch_y0 - 3, elbow_h]);
}

module stand_world() {
    difference() {
        intersection() {
            union() {
                rotate([-tilt, 0, 0]) head_solid();
                translate([-base_w/2, 0, 0]) cube([base_w, base_d, base_t]);
            }
            translate([-500, -500, 0]) cube(1000);                     // above the desk
            rotate([-tilt, 0, 0]) translate([-500, 0, -500]) cube(1000); // behind the front plane
        }
        rotate([-tilt, 0, 0]) head_cuts();
        // round off the base corners a little
        for (sx = [-1, 1]) translate([sx*base_w/2, base_d, -1])
            rotate([0, 0, sx > 0 ? 0 : 90]) translate([-6, -6, 0])
                difference() { cube([7, 7, base_t + 2]); cylinder(r=6, h=base_t + 2); }
    }
}

module cap() {
    // Cap coords: +Y = top of the display, spigot points +Z (toward the board).
    difference() {
        union() {
            cylinder(r=R_out, h=cap_t);                       // plate
            cylinder(d=D - 0.15, h=cap_t + spig);             // press-fit spigot ring
        }
        translate([0, 0, cap_t]) cylinder(d=D - 3.2, h=spig + 1);  // hollow spigot
        // vent slots in the plate
        for (i = [-2:2]) translate([i*5 - 1, -10, -1]) cube([2, 20, cap_t + 2]);
        // bottom: clear the USB socket
        translate([-plug_w/2, -R_out - 1, cap_t]) cube([plug_w, 8, spig + 1]);
        // top: notch for the 12-pin cable
        if (hdr_notch > 0) translate([-hdr_notch/2, D/2 - 6, -1]) cube([hdr_notch, 10, cap_t + spig + 2]);
    }
}

module fit_test() {
    // just the front lip + 4 mm of ring, to check board_d / view_d / clr
    difference() {
        cylinder(r=R_out, h=lip_t + 4);
        translate([0, 0, -1]) cylinder(d=view_d, h=10);
        translate([0, 0, lip_t]) cylinder(d=D, h=10);
        translate([-plug_w/2, -R_out - 1, lip_t]) cube([plug_w, 6, 10]);
    }
}

// ---------- output ----------
// Print orientations: stand lies on its front face (front lip on the bed) -> no supports
// except the short bridge over the cable tunnel. Cap prints flat, spigot up.
if (PART == "stand") {
    rotate([90 + tilt, 0, 0]) stand_world();   // undo tilt, then lay front face on the bed
} else if (PART == "cap") {
    cap();
} else if (PART == "fit_test") {
    fit_test();
} else {
    color("dimgray") stand_world();
    color("steelblue") rotate([-tilt, 0, 0]) translate([0, ring_d + cap_t, cz]) rotate([90, 0, 0]) cap();
}
