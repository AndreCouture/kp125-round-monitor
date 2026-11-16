# Air-quality logger — parts, wiring, assembly

Companion hardware for `firmware/airquality_logger/`. Built around STEMMA QT
(Adafruit's I2C connector standard) and Adafruit's Feather/FeatherWing stacking
headers specifically so there's no soldering beyond the usual header pins, and
no hand-wired I2C or SPI bus to get wrong.

## Parts list

Prices are rough and change often — check the live page before ordering.
DigiKey and Mouser both ship to Canada; Adafruit's own store does too, with
longer transit but no reseller markup.

| Part | Why | Source | ~Price (CAD) |
|---|---|---|---|
| Sensirion **SEN55-SDN-T** (bare sensor module) | PM1.0/2.5/4.0/10, VOC index, NOx index, temp, humidity — the main sensor | [DigiKey.ca](https://www.digikey.ca/en/products/detail/sensirion-ag/SEN55-SDN-T/16342756) | ~$49 |
| Adafruit **SEN54/SEN55 Adapter Breakout** (STEMMA QT/Qwiic, PID 5964) | Converts the SEN55's 6-pin JST GH connector to STEMMA QT | DigiKey (search "Adafruit 5964") or [Adafruit](https://www.adafruit.com/product/5964) | ~$8 |
| Adafruit **SCD-41** STEMMA QT breakout (PID 5190) | CO2, with its own temp/humidity too | [Adafruit](https://www.adafruit.com/product/5190) (no DigiKey/Mouser stock found at review time — check again) | ~$55 |
| Adafruit **Feather ESP32 V2** STEMMA QT (PID 5400) | The MCU — built-in STEMMA QT port, Wi-Fi, stacking headers for the FeatherWing below | DigiKey part **1528-5400-ND** | ~$35 |
| Adafruit **Adalogger FeatherWing** (RTC + microSD, PID 2922) | Stacks straight onto the Feather's headers — this is the "SD card wiring," no separate breakout or SPI wires | [Adafruit](https://www.adafruit.com/products/2922) | ~$13 |
| microSD card, any small capacity (4–16 GB) | The queue buffer only needs a few MB/day — nothing fills it | any electronics or computer shop | ~$10 |
| STEMMA QT cable, 100 mm, qty 2 (often sold as a 4-pack, PID 4209) | Feather → SEN55 adapter → SCD41, daisy-chained | [Adafruit](https://www.adafruit.com/product/4209) or DigiKey | ~$6 |
| USB-C cable + 5V/1A+ wall adapter | Power, left plugged in near the printer | anything on hand | — |
| CR1220 coin cell (optional) | Keeps the Adalogger's RTC ticking across power loss — not required, since the sketch gets its clock from NTP at boot anyway | any shop | ~$3 |

**Total: roughly $170–180 CAD**, most of it the two Sensirion-made sensors.

If the SCD41 breakout turns out hard to source, the sketch runs fine without
it — `scd41Ok` just stays false and the CO2 column logs as blank. You lose
CO2, keep everything else.

## Wiring

With this specific combo, there's no point-to-point wiring to do:

1. **Stack the Adalogger FeatherWing** directly onto the Feather ESP32 V2's
   header pins (both have matching Feather-standard headers — it just plugs
   on top). This gives the Feather a microSD slot on its SPI bus, chip-select
   on GPIO 33 automatically (the sketch's `SD_CS_PIN` is set to `SS`, which
   resolves to the right pin for this board).
2. **STEMMA QT cable** from the Feather's STEMMA QT port to the SEN55
   adapter's **IN** STEMMA QT port.
3. **STEMMA QT cable** from the SEN55 adapter's **OUT** port to the SCD41
   breakout's STEMMA QT port. (SEN55 is on I2C address `0x69`, SCD41 on
   `0x62` — no address conflict, so they daisy-chain on the same bus.)
4. **Insert the microSD card** into the Adalogger's slot before powering on.
5. Power the whole stack over the Feather's USB-C port.

That's the entire build: two cables, one board stacked on another, one card
inserted. No breadboard, no loose GPIO wires.

## Firmware note

`airquality_logger.ino` targets FQBN `esp32:esp32:adafruit_feather_esp32_v2`
for this build. `Wire.begin()` with no arguments and `SD.begin(SS)` both pick
up this board's own default pins — verified against the installed board
package's `pins_arduino.h` (`SDA=22, SCL=20, SS=33` on this variant), not
assumed from a generic ESP32 devkit's pinout.

## Placement

Mount it near the printer but not directly in the nozzle exhaust or a cooling
fan's draft — ambient room/enclosure air is what you want. The SEN55 has its
own small aspirating fan, so don't block its inlet or exhaust vents.

## Enclosure

`stand/airquality_enclosure.scad` — renders clean (OpenSCAD 2026.06, Manifold
backend, checked manifold + visually via PNG preview), **not yet
test-printed**. Three parts:

- `box` — friction-fit tray + lid for the Feather + Adalogger FeatherWing
  stack, with a USB-C cable slot and a slot for the two STEMMA QT cables
  leaving to the sensors.
- `sensor_mount` — a separate, deliberately open/vented bracket for the SEN55
  adapter + SCD41 breakout, with cable-tie anchors. These two **cannot go in
  the sealed box** — the SEN55 has its own aspirating fan and both sensors
  need real ambient air, not trapped box air — so they get a perforated tray
  instead.
- `fit_test` — a short collar + matching ribbed plug; print this first and
  check the press-fit by hand before committing to the full box (same role
  as `display_stand.scad`'s `fit_test`).

`feather_w`/`feather_l`/`feather_t` and the Adalogger's `wing_*` dimensions
come from Adafruit's own published specs for these exact products — fairly
solid. **`sen55_adapter_*` and `scd41_board_*` are placeholders** — no
confirmed dimension was found for those two specific breakout boards at
design time, which is why `sensor_mount` is a loose perforated tray with wide
tolerances rather than a snug pocket. Measure both with calipers once they're
in hand and tighten those variables for a tidier fit — same workflow this
project already uses for `display_stand.scad`'s `board_d`/`stack_t`.
