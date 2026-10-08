// Desk stand for the Waveshare ESP32-S3-Touch-LCD-1.28 (round 240x240 touch display)
// Parts: "stand" (tilted holder + pedestal + base) and "cap" (press-fit back cover).
// Open in OpenSCAD, set PART, F6 to render, F7 to export STL.
// All dimensions in mm.
//
// STATUS: renders clean (manifold), NOT yet test-printed. Board numbers come from Waveshare's
// dimension drawing / DXF for this board (wiki: ESP32-S3-Touch-LCD-1.28.zip), not calipers.
// Print PART="fit_test" (a few-minute ring) before printing the full stand.

PART = "both";       // "stand", "cap", "fit_test", "fit_test_set" (clr 0.4/0.5/0.6 = 1/2/3 notches), or "both" (assembled preview)

// ---------- board (from Waveshare's drawing; check with calipers if a print doesn't fit) ----------
board_d       = 38.51; // lens OD, the widest round part (drawing: 38.51 +/-0.05)
stack_t       = 8.7;   // lens front to tallest back part: 8.40 (battery connector; 12-pin is 8.13) + 0.3
view_d        = 35.0;  // front window: > 33.40 (lens view area), < 35.67 (ink edge) hides the border
usb_out       = 2.52;  // USB-C socket below the round outline (41.04 overall - 38.51 lens)
usb_y         = 6.67;  // USB-C socket centre behind the lens front (socket body 5.09..8.25)
// Below the lens the PCB is not round: straight 45-degree edges narrow to a flat bottom around the
// USB-C socket ("pear" shape). Corner points [x, y] from Waveshare's DXF, mm from the lens centre,
// -y toward the USB socket; mirrored for -x.
pcb_corners   = [[13.62, -13.61],   // where the straight edges leave the lens circle
                 [6.66, -20.73],    // PCB bottom corners
                 [6.46, -21.02],    // USB-C socket mounting legs
                 [4.52, -21.78]];   // USB-C socket body

// ---------- your USB-C cable ----------
plug_w        = 13.5;  // plug overmold width + clearance
plug_t        = 8.0;   // plug overmold thickness + clearance
// Owner's cable: straight plug, pointing down. Measure its rigid length (metal + overmold +
// stiff strain relief) and set plug_straight; 30 is a typical straight USB-C plug.
// Right-angle plug instead: plug_straight = length to the bend, elbow_h = elbow height (~16).
plug_straight = 30.0;  // rigid length below the socket face (sets the pedestal height)
elbow_h       = 12.0;  // tunnel height where the cable bends to exit sideways or out the back

// ---------- stand ----------
tilt     = 15;         // lean back from vertical, degrees
clr      = 0.4;        // radial clearance around the board outline: fit_test_set ring #1 fit best on the owner's printer (reprint the set if you change nozzle or printer)
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
D       = board_d + 2*clr;        // pocket diameter across the round part
R_out   = D/2 + wall;

// Board outline (2D, +y = top of the display): lens circle hulled with the PCB/socket corners
module board2d() {
    hull() {
        circle(d=board_d);
        for (p = pcb_corners, s = [-1, 1]) translate([s*p[0], p[1]]) square(0.01, center=true);
    }
}
module pocket2d(c = clr)  { offset(r=c) board2d(); }          // board + clearance
module ring2d(c = clr)    { offset(r=c + wall) board2d(); }   // pocket + wall

// Extrude a 2D board-plane shape along the ring axis (+Y in module coords), bulge pointing down:
// rotate([-90,0,0]) maps 2D +y to -Z, so mirror in y first to keep the display's top up.
module along_axis(h) { rotate([-90, 0, 0]) linear_extrude(height=h) mirror([0, 1]) children(); }
ring_d  = lip_t + stack_t + spig; // ring depth (front to back)
tz0     = base_t + 1;             // tunnel floor (module coords)
tz1     = tz0 + elbow_h;          // tunnel roof
z_sock  = tz1 + plug_straight;    // USB socket face
cz      = z_sock + usb_out + D/2; // ring centre height
ch_y0   = usb_y - plug_t/2;       // channel front face (leaves a front skin)

// Module coords: X = width, Y = depth back from the front glass plane (Y>=0), Z = up the display.
module head_solid() {
    // ring: follows the board outline so the wall stays `wall` thick around the PCB's lower edges
    translate([0, 0, cz]) along_axis(ring_d) ring2d();
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
    translate([0, lip_t, cz]) along_axis(100) pocket2d();
    // recess so the cap plate sits flush against the ring and clears the pedestal
    translate([0, ring_d, cz]) rotate([-90, 0, 0]) cylinder(r=R_out + 0.4, h=100);
}

// Cable cuts, also in module coords. stand_world() clips them to above the base so the tilt
// can't carry them down through the base plate (which used to split the base in two).
module cable_cuts() {
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
        intersection() {                                               // cable cuts, kept above the base
            rotate([-tilt, 0, 0]) cable_cuts();
            translate([-500, -500, base_t]) cube(1000);
        }
        // round off the base corners a little
        for (sx = [-1, 1]) translate([sx*base_w/2, base_d, -1])
            rotate([0, 0, sx > 0 ? 0 : 90]) translate([-6, -6, 0])
                difference() { cube([7, 7, base_t + 2]); cylinder(r=6, h=base_t + 2); }
    }
}

// Cap fit. The spigot is slightly undersized and held by six crush ribs that stand cap_fit proud
// of the pocket wall. The ribs run past the spigot toward the board (rib_reach) for more grip, each
// backed by a small fin. Four feet continue the spigot onto the bare PCB rim (a band just inside the
// board edge that Waveshare's DXF shows free of parts) and press the board against the front lip.
cap_fit     = 0.2;    // rib interference with the pocket wall (radial); 0.15 was slightly loose on the owner's printer
rib_r       = 0.6;    // half-round crush rib radius
// On the round part of the pocket (it flares from 225 to 315 deg) and, for the extended part, at
// angles where the DXF shows nothing at the board edge (0 and 180 hit parts, e.g. the battery connector)
rib_angles  = [8, 45, 135, 172, 200, 340];
pcb_back    = 5.0;    // lens front to the PCB's back face (Waveshare drawing)
rib_reach   = stack_t - pcb_back - 0.3;   // ribs extend this far past the spigot, stopping 0.3 short of the PCB
rib_fin_r   = 18.2;   // inner edge of the fin behind each rib (r 18.2-18.5 is clear at all rib angles in the DXF)
foot_load   = 0.2;    // feet are this much longer than the gap, so the board is held snug
foot_r      = [17.9, 18.7];   // radial band of the feet (on the PCB rim)
foot_w      = 8;              // angular width of each foot, degrees (~2.5 mm)
// Cap coordinates are the back view mirrored (x -> -x). Back-view angles 20/155/200/330 deg are
// clear in the DXF at r 18.2-18.5; in cap coordinates they are:
foot_angles = [160, 25, 340, 210];

module cap() {
    // Cap coords: +Y = top of the display, spigot points +Z (toward the board).
    foot_h = stack_t - pcb_back + foot_load;          // spigot face -> PCB back, plus preload
    difference() {
        union() {
            cylinder(r=R_out, h=cap_t);                           // plate
            cylinder(d=D - 0.3, h=cap_t + spig);                  // spigot, 0.15 mm radial slip
            for (a = rib_angles) rotate(a) {                                                 // crush ribs
                rc = D/2 + cap_fit - rib_r;                   // rib centre radius
                rib_h = spig + rib_reach;
                translate([rc, 0, cap_t]) {
                    cylinder(r=rib_r, h=rib_h - 0.6);
                    translate([0, 0, rib_h - 0.6]) cylinder(r1=rib_r, r2=rib_r - 0.35, h=0.6);    // lead-in
                }
                translate([rib_fin_r, -rib_r, cap_t]) cube([rc - rib_fin_r, 2*rib_r, rib_h - 0.6]);   // fin
            }
            for (a = foot_angles) rotate(a - foot_w/2)                                       // feet
                translate([0, 0, cap_t + spig - 0.01])
                    rotate_extrude(angle=foot_w) translate([foot_r[0], 0]) square([foot_r[1] - foot_r[0], foot_h + 0.01]);
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

// Front lip + 4 mm of ring with radial clearance c; `marks` small notches on the top of the rim
// identify the ring in a set.
module fit_ring(c, marks = 0) {
    top = board_d/2 + c + wall;   // rim at the top of the display
    difference() {
        linear_extrude(height=lip_t + 4) ring2d(c);
        translate([0, 0, -1]) cylinder(d=view_d, h=10);
        translate([0, 0, lip_t]) linear_extrude(height=10) pocket2d(c);   // board outline incl. USB-C socket
        for (i = [0 : marks - 1]) translate([(i - (marks - 1)/2) * 3 - 0.5, top - 1, -1]) cube([1, 2, 10]);
    }
}

// Just the front lip + 4 mm of ring at the current clr, to check board_d / view_d / clr
module fit_test() { fit_ring(clr); }

// Three rings in one print with radial clearance fit_set_clr[i], marked with i+1 notches:
// try the board in each and set clr to the best fit
fit_set_clr = [0.4, 0.5, 0.6];
module fit_test_set() {
    for (i = [0 : len(fit_set_clr) - 1]) {
        c = fit_set_clr[i];
        translate([i * (board_d + 2*0.6 + 2*wall + 3), 0, 0]) fit_ring(c, i + 1);
    }
}

// ---------- output ----------
// Print orientations: stand lies on its front face (front lip on the bed) -> no supports
// except the short bridge over the cable tunnel. Cap prints flat, spigot up.
if (PART == "stand") {
    rotate([90 + tilt, 0, 0]) stand_world();   // undo tilt, then lay front face on the bed
} else if (PART == "cap") {
    cap();
} else if (PART == "fit_test_set") {
    fit_test_set();
} else if (PART == "fit_test") {
    fit_test();
} else {
    color("dimgray") stand_world();
    color("steelblue") rotate([-tilt, 0, 0]) translate([0, ring_d + cap_t, cz]) rotate([90, 0, 0]) cap();
}
