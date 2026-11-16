// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 12113775 Canada Inc. Non-commercial use only; credit required. See LICENSE.
// Enclosure for firmware/airquality_logger's electronics (Adafruit Feather ESP32 V2 stacked
// with an Adalogger FeatherWing), plus a separate open, vented bracket for the two STEMMA QT
// sensor boards (SEN55 adapter + SCD41), which need real airflow and must NOT go in a sealed box.
// Parts list and wiring: docs/parts-airquality-logger.md
//
// Open in OpenSCAD, set PART, F6 to render, F7 to export STL.
// All dimensions in mm.
//
// STATUS: renders clean (manifold, OpenSCAD 2026.06). NOT yet test-printed, NOT yet fitted to
// real boards. feather_w/feather_l/feather_t and wing_* come from Adafruit's own published
// dimensions for these exact products (PID 5400, PID 2922) — fairly solid. sen55_adapter_*
// and scd41_board_* are PLACEHOLDERS (no published dimension found for those two boards at
// design time) — the sensor_mount is deliberately a loose, generic perforated tray rather than
// a snug pocket for exactly this reason. Print PART="fit_test" first and check the stack
// actually slides into the box before committing to a full print; measure the two sensor
// boards with calipers and tighten sensor_mount's slot sizes once you have them in hand —
// same workflow display_stand.scad used for board_d/stack_t before its first real print.
//
// Print orientation: box floor-down (open top up) — no supports; the two cable slots bridge
// cleanly like the main stand's cable tunnel. lid flat-side down, spigot up — no supports.
// sensor_mount flat, vent slots up.

PART = "box";   // "box" (base, open-top tray for the Feather+Wing stack), "lid",
                // "sensor_mount" (vented bracket for the SEN55 adapter + SCD41), "fit_test", "both"

// ---------- Feather ESP32 V2 + Adalogger FeatherWing stack (Adafruit PID 5400 + PID 2922) ----------
feather_w     = 22.8;  // board width (both the Feather and the Wing share this — standard Feather footprint)
feather_l     = 52.3;  // Feather length
feather_t     = 7.2;   // Feather board + components thickness (rounded up slightly for the USB-C socket)
wing_l        = 50.8;  // Adalogger FeatherWing length (slightly shorter than the Feather — fine, centred)
wing_t        = 1.6;   // Wing board thickness
header_gap    = 9.0;   // Feather-to-Wing gap once stacked on female headers (standard Feather stacking height)
stack_t       = feather_t + header_gap + wing_t + 2;   // total stack height + a little slack

// ---------- box (tray + lid, friction-fit like display_stand.scad's cap) ----------
clr       = 0.5;   // clearance around the board stack footprint (loosen/tighten per your printer)
wall      = 2.0;
floor_t   = 2.0;
lid_t     = 2.0;
spig      = 2.5;         // lid spigot length into the box wall
cap_fit   = 0.3;         // crush-rib interference (same idea as display_stand.scad's cap_fit)
n_ribs    = 6;           // crush ribs around the lid spigot

usb_slot_w = 12;   // USB-C cable clearance slot, short end of the box
usb_slot_h = 8;
cable_slot_w = 10; // slot for the 2 STEMMA QT cables leaving to the sensor_mount, other short end
cable_slot_h = 6;

box_in_w = feather_w + clr * 2;
box_in_l = feather_l + clr * 2;
box_out_w = box_in_w + wall * 2;
box_out_l = box_in_l + wall * 2;

// Standoffs that the Feather's own mounting holes would normally use — since this board's exact
// hole coordinates weren't confirmed at design time, these are plain corner rails (friction
// grip on the board edge, not screw posts). Fine for something that sits still on a shelf;
// revisit with real screw posts if this will be handled/moved often.
rail_w = 2.0;
rail_h = 3.0;

// ---------- sensor_mount: open bracket for the SEN55 adapter + SCD41 breakout ----------
// PLACEHOLDERS — no confirmed dimensions for these two specific boards at design time.
// Measure both with calipers and tighten slot_w/slot_l/slot_gap before trusting the fit;
// as shipped this is intentionally loose (a perforated tray + zip-tie anchors, not a pocket),
// so a few mm of error here just means a slightly loose tray, not a part that doesn't fit.
sen55_adapter_w = 18;   // typical small Adafruit STEMMA QT breakout footprint — CONFIRM
sen55_adapter_l = 26;
scd41_board_w   = 23;   // Sensirion SCD41 bare module is ~22.8 x 25.5mm; the Adafruit breakout
scd41_board_l   = 26;   // is a little larger to carry the STEMMA QT connectors — CONFIRM

mount_wall   = 1.6;
mount_floor  = 1.6;
mount_gap    = 4;     // gap between the two board slots
vent_slot_w  = 2.5;   // width of each vent slot in the floor (under the SEN55's fan + general airflow)
vent_slot_pitch = 5;

tie_slot  = [4.5, 2.0];   // same cable-tie slot as display_stand.scad's tie_anchors
tie_span  = 11;

// =========================================================================

module rounded_rect(w, l, r) {
  hull() for (x = [-w/2 + r, w/2 - r]) for (y = [-l/2 + r, l/2 - r])
    translate([x, y]) circle(r = r);
}

// Small ribs on the spigot's two long sides, each sticking out cap_fit beyond the otherwise-
// undersized spigot, so only the ribs take the interference on insertion (same idea as
// display_stand.scad's cap crush ribs — six extended ribs, cap_fit 0.3 — adapted to a
// rectangular spigot instead of that design's round one).
module spigot_ribs(w, l, h, n_per_side, rib_h) {
  for (i = [0 : n_per_side - 1]) {
    y = -l/2 + (i + 0.5) * l / n_per_side;
    translate([ w/2, y, h/2]) cube([cap_fit * 2, rib_h, h], center = true);
    translate([-w/2, y, h/2]) cube([cap_fit * 2, rib_h, h], center = true);
  }
}

// The lid's spigot footprint: a touch undersized so it slides into the box's cavity, with
// spigot_ribs() (called from both lid() and fit_test()) taking up the actual interference.
spigot_w = box_in_w - 1.0;
spigot_l = box_in_l - 1.0;

module box() {
  difference() {
    union() {
      // outer shell
      linear_extrude(height = floor_t + stack_t + wall)
        rounded_rect(box_out_w, box_out_l, 3);
      // lip for the lid to spigot into
      translate([0, 0, floor_t + stack_t + wall])
        linear_extrude(height = spig)
          difference() {
            rounded_rect(box_out_w, box_out_l, 3);
            rounded_rect(box_in_w, box_in_l, 2);
          }
    }
    // hollow interior
    translate([0, 0, floor_t])
      linear_extrude(height = stack_t + wall + spig + 1)
        rounded_rect(box_in_w, box_in_l, 2);
    // USB-C cable slot, one short end, at Feather-socket height (near the floor)
    translate([0, -box_out_l/2 + wall/2, floor_t + feather_t/2 + 1])
      cube([usb_slot_w, wall * 3, usb_slot_h], center = true);
    // STEMMA QT cable pass-through, the other short end
    translate([0, box_out_l/2 - wall/2, floor_t + stack_t/2 + 1])
      cube([cable_slot_w, wall * 3, cable_slot_h], center = true);
  }
  // board-edge retaining rails, inside the floor
  for (x = [-box_in_w/2 + rail_w/2, box_in_w/2 - rail_w/2])
    translate([x, 0, floor_t + rail_h/2])
      cube([rail_w, box_in_l - 4, rail_h], center = true);
}

module lid() {
  union() {
    linear_extrude(height = lid_t)
      rounded_rect(box_out_w, box_out_l, 3);
    translate([0, 0, lid_t]) {
      linear_extrude(height = spig)
        difference() {
          rounded_rect(spigot_w, spigot_l, 2);
          rounded_rect(spigot_w - wall, spigot_l - wall, 1.5);
        }
      spigot_ribs(spigot_w, spigot_l, spig, n_ribs / 2, 3);
    }
  }
}

module sensor_mount() {
  total_w = sen55_adapter_w + scd41_board_w + mount_gap + mount_wall * 3;
  total_l = max(sen55_adapter_l, scd41_board_l) + mount_wall * 2;
  difference() {
    linear_extrude(height = mount_floor)
      rounded_rect(total_w, total_l, 2);
    // vent slots across the whole floor — airflow matters more than a snug pocket here
    for (x = [-total_w/2 + 4 : vent_slot_pitch : total_w/2 - 4])
      translate([x, 0, -0.5])
        linear_extrude(height = mount_floor + 1)
          square([vent_slot_w, total_l - 6], center = true);
  }
  // low edge rails marking each board's slot (not a snug pocket — see note above)
  rail_h2 = 2.5;
  x0 = -total_w/2 + mount_wall + sen55_adapter_w/2;
  x1 = x0 + sen55_adapter_w/2 + mount_gap/2 + mount_wall/2 + scd41_board_w/2;
  for (cx = [x0, x1])
    for (dx = [-1, 1])
      translate([cx + dx * (sen55_adapter_w/2 - 1), 0, rail_h2/2])
        cube([2, total_l - 6, rail_h2], center = true);
  // cable-tie anchor pair at one end, to strap this to a shelf edge or printer frame
  translate([0, -total_l/2 + 6, -0.1])
    for (dx = [-tie_span/2, tie_span/2])
      translate([dx, 0, 0])
        linear_extrude(height = mount_floor + 0.2)
          square([tie_slot[0], tie_slot[1]], center = true);
}

module fit_test() {
  // Quick print: a short collar matching the box's wall/lip profile, and a short plug matching
  // the lid's spigot + crush ribs, side by side — print both, press them together by hand, check
  // cap_fit before committing to the full box (same idea as display_stand.scad's fit_test ring).
  test_h = 6;
  collar_x = -(box_out_w/2 + 6);
  translate([collar_x, 0, 0])
    linear_extrude(height = test_h)
      difference() { rounded_rect(box_out_w, box_out_l, 3); rounded_rect(box_in_w, box_in_l, 2); }
  plug_x = box_out_w/2 + 6;
  translate([plug_x, 0, 0]) {
    linear_extrude(height = test_h)
      difference() {
        rounded_rect(spigot_w, spigot_l, 2);
        rounded_rect(spigot_w - wall, spigot_l - wall, 1.5);
      }
    spigot_ribs(spigot_w, spigot_l, test_h, n_ribs / 2, 3);
  }
}

if (PART == "box") box();
else if (PART == "lid") lid();
else if (PART == "sensor_mount") sensor_mount();
else if (PART == "fit_test") fit_test();
else if (PART == "both") { box(); translate([box_out_w + 10, 0, 0]) lid(); translate([-(sen55_adapter_w + scd41_board_w + 40), 0, 0]) sensor_mount(); }
