// Desk stand for the Waveshare ESP32-S3-Touch-LCD-1.28 (round 240x240 touch display)
// Parts: "stand" (fixed tilt), or the hinged version "hinge_head" + "hinge_base" + 2x "hinge_pin"
// (adjustable tilt with click stops), plus "cap" (press-fit back cover) for either.
// Open in OpenSCAD, set PART, F6 to render, F7 to export STL.
// All dimensions in mm.
//
// STATUS: renders clean (manifold), NOT yet test-printed. Board numbers come from Waveshare's
// dimension drawing / DXF for this board (wiki: ESP32-S3-Touch-LCD-1.28.zip), not calipers.
// Print PART="fit_test" (a few-minute ring) before printing the full stand.

PART = "both";       // "stand", "cap", "hinge_head", "hinge_base", "hinge_pin", "hinge_both" (preview),
                     // "fit_test", "fit_test_set" (clr 0.4/0.5/0.6 = 1/2/3 notches), or "both" (fixed-stand preview)

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

// ---------- cable-tie anchors in the base (optional) ----------
// Two slots through the base on either side of the cable, joined by a groove underneath so the
// tie sits flush and the base stays flat: thread the tie down one slot, up the other, round the cable.
tie_anchors  = true;
tie_slot     = [4.5, 2.0];   // slot length along the cable x width; fits ties up to ~4 mm wide
tie_span     = 11;           // slot-to-slot distance across the cable
tie_groove   = 1.5;          // depth of the groove under the base

// ---------- hinged stand (PART "hinge_head", "hinge_base", "hinge_pin", "hinge_both") ----------
// The head (display ring + neck + two side ears) pivots between two cheeks on the base and clicks
// into hinge_tilts: a bump on each cheek drops into one of the dimples on the ear.
hinge_tilts  = [0, 10, 20, 30];  // click-stop angles of backward tilt; keep them >= 10 deg apart
hinge_show   = 15;           // tilt shown in the "hinge_both" preview
pivot_h      = 34;           // pivot height above the desk (leaves ~18 mm for the cable to bend)
head_drop    = 40;           // ring centre above the pivot
ear_t        = 4;            // ear (neck side wall) thickness
ear_r        = 12;           // ear disc radius around the pivot
cheek_t      = 4;            // cheek thickness
cheek_gap    = 0.2;          // ear-to-cheek gap
pin_d        = 4;            // pivot pin diameter
pin_fit_cheek= 0.2;          // pin hole oversize in the cheek (snug, the pin stays put)
pin_fit_ear  = 0.3;          // pin hole oversize in the ear (turns freely)
det_r        = 10;           // click-stop radius from the pivot axis
det_bump     = 0.75;         // bump radius on the cheek
det_proud    = 0.5;          // how far the bump stands out of the cheek
det_dimple   = 0.85;         // dimple radius in the ear
det_angle    = -60;          // bump position around the axis (0 = toward the back, 90 = up)
hbase_w      = 70;           // hinged base size
hbase_d      = 58;
pivot_y      = 18;           // pivot distance from the base's front edge

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
        base_corners(base_w, base_d);
        // cable-tie anchors: left and right where the cable leaves the side tunnel, and at the back
        if (tie_anchors) {
            for (sx = [-1, 1]) tie_cut(sx * 27, 10, along_x=true);
            tie_cut(0, 42, along_x=false);
        }
    }
}

module base_corners(w, d) {
    for (sx = [-1, 1]) translate([sx*w/2, d, -1])
        rotate([0, 0, sx > 0 ? 0 : 90]) translate([-6, -6, 0])
            difference() { cube([7, 7, base_t + 2]); cylinder(r=6, h=base_t + 2); }
}

// Cable-tie anchor centred on the cable path at (x, y) in base coordinates (z = 0 on the desk).
// along_x: the cable runs along X there, so the slots sit either side of it in Y.
module tie_cut(x, y, along_x = true) {
    sl = along_x ? [tie_slot[0], tie_slot[1]] : [tie_slot[1], tie_slot[0]];
    for (s = [-1, 1]) {
        off = s * tie_span/2;
        translate([x + (along_x ? 0 : off) - sl[0]/2, y + (along_x ? off : 0) - sl[1]/2, -1])
            cube([sl[0], sl[1], base_t + 2]);                                  // slot through the base
    }
    gl = along_x ? [tie_slot[0], tie_span + tie_slot[1]] : [tie_span + tie_slot[1], tie_slot[0]];
    translate([x - gl[0]/2, y - gl[1]/2, -1]) cube([gl[0], gl[1], tie_groove + 1]);   // groove underneath
}

// ---------- hinged stand ----------
neck_w = plug_w + 2*ear_t;   // neck/ear width (the cheeks sit just outside it)
hy_p   = ring_d / 2;         // pivot depth behind the front glass plane, head coordinates

// Horizontal hole along X with a teardrop point (apex_y: toward +Y, else toward +Z) so it prints
// without support in that orientation
module tear2d(r) { union() { circle(r=r); rotate(45) square(r); } }
module hole_x(d, len, apex_y = true) {
    rotate([0, 90, 0]) linear_extrude(height=len, center=true) rotate(apex_y ? 0 : 90) tear2d(d/2);
}

// Head coordinates: as the fixed stand's module coords (X width, Y back from the front glass, Z up)
// with the pivot at (0, hy_p, 0) and the ring centre at z = head_drop.
module hinge_head() {
    difference() {
        intersection() {
            union() {
                translate([0, 0, head_drop]) along_axis(ring_d) ring2d();
                hull() {                                                    // neck ending in the ears
                    translate([-neck_w/2, 0, 0]) cube([neck_w, ring_d, head_drop - 8]);
                    translate([0, hy_p, 0]) rotate([0, 90, 0]) cylinder(r=ear_r, h=neck_w, center=true);
                }
            }
            translate([-100, 0, -100]) cube(200);                           // nothing in front of the glass plane
        }
        // window, pocket and cap recess, as on the fixed stand
        translate([0, -1, head_drop]) rotate([-90, 0, 0]) cylinder(d=view_d, h=ring_d + 2);
        translate([0, -0.01, head_drop]) rotate([-90, 0, 0]) cylinder(d1=view_d + 2*lip_t, d2=view_d, h=lip_t);
        translate([0, lip_t, head_drop]) along_axis(100) pocket2d();
        translate([0, ring_d, head_drop]) rotate([-90, 0, 0]) cylinder(r=R_out + 0.4, h=100);
        // plug channel from the socket down between the ears, open at the back and the bottom
        translate([-plug_w/2, ch_y0, -ear_r - 1]) cube([plug_w, 100, head_drop + ear_r + 1]);
        // pivot holes (teardrop pointing +Y = up when the head prints front face down)
        translate([0, hy_p, 0]) hole_x(pin_d + pin_fit_ear, neck_w + 2, apex_y=true);
        // click-stop dimples on both ears' outer faces, one per tilt
        for (t = hinge_tilts, sx = [-1, 1]) {
            a = det_angle + t;
            translate([sx * neck_w/2, hy_p + det_r * cos(a), det_r * sin(a)]) sphere(r=det_dimple, $fn=24);
        }
    }
}

// Base coordinates: z = 0 on the desk, Y back from the base's front edge, pivot at (0, pivot_y, pivot_h)
module hinge_base() {
    cx = neck_w/2 + cheek_gap;   // cheek inner face
    difference() {
        union() {
            translate([-hbase_w/2, 0, 0]) cube([hbase_w, hbase_d, base_t]);
            for (sx = [-1, 1]) {
                x0 = sx > 0 ? cx : -cx - cheek_t;
                hull() {                                                    // cheek
                    translate([x0, pivot_y - ear_r, 0]) cube([cheek_t, 2*ear_r, base_t]);
                    translate([x0, pivot_y, pivot_h]) rotate([0, 90, 0]) cylinder(r=ear_r, h=cheek_t);
                }
                // click-stop bump on the inner face
                translate([sx * (cx + det_bump - det_proud), pivot_y + det_r * cos(det_angle), pivot_h + det_r * sin(det_angle)])
                    sphere(r=det_bump, $fn=24);
            }
        }
        translate([0, pivot_y, pivot_h]) hole_x(pin_d + pin_fit_cheek, neck_w + 2*cheek_t + 4, apex_y=false);
        base_corners(hbase_w, hbase_d);
        if (tie_anchors) tie_cut(0, hbase_d - 12, along_x=false);           // cable runs out the back
    }
}

// Pivot pin: snug in the cheek, turns in the ear, stops before the cable channel. Print two.
pin_len = cheek_t + cheek_gap + ear_t - 0.5;
module hinge_pin() {
    cylinder(d=pin_d + 3, h=1.5);                                    // head (prints on the bed)
    translate([0, 0, 1.5]) cylinder(d=pin_d, h=pin_len - 0.5);
    translate([0, 0, 1.5 + pin_len - 0.5]) cylinder(d1=pin_d, d2=pin_d - 0.8, h=0.5);   // lead-in tip
}

module hinge_assembled(t) {
    color("dimgray") hinge_base();
    color("slategray") translate([0, pivot_y, pivot_h]) rotate([-t, 0, 0]) translate([0, -hy_p, 0]) {
        hinge_head();
        color("steelblue") translate([0, ring_d + cap_t, head_drop]) rotate([90, 0, 0]) cap();
    }
    for (sx = [-1, 1]) color("goldenrod")
        translate([sx * (neck_w/2 + cheek_gap + cheek_t + 1.5), pivot_y, pivot_h]) rotate([0, -sx * 90, 0]) hinge_pin();
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
} else if (PART == "hinge_head") {
    rotate([90, 0, 0]) hinge_head();          // front face on the bed, like the fixed stand
} else if (PART == "hinge_base") {
    hinge_base();                             // flat
} else if (PART == "hinge_pin") {
    for (sx = [-1, 1]) translate([sx * 6, 0, 0]) hinge_pin();   // two pins, head down
} else if (PART == "hinge_both") {
    hinge_assembled(hinge_show);
} else {
    color("dimgray") stand_world();
    color("steelblue") rotate([-tilt, 0, 0]) translate([0, ring_d + cap_t, cz]) rotate([90, 0, 0]) cap();
}
