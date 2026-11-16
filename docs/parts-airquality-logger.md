# Air-quality logger — parts, wiring, assembly

Two ways to build this. **Merged** reuses the round display's own ESP32-S3 —
cheaper, one board to place near the printer, no second enclosure. **Standalone**
is a second, independent ESP32 — pricier, but fully portable on its own.

Start with Merged unless you specifically want a second, separate unit.

## Option A: Merged into the round display (recommended)

The round display's touch/IMU I2C bus isn't exposed off-board, but its
documented 12-pin connector carries six genuinely free GPIOs — confirmed
directly from Waveshare's schematic (`ESP32-S3-Touch-LCD-1.28-Sch.pdf`), not
assumed. `firmware/kp125_round_display/kp125_round_display.ino` uses two of
them (GPIO17/18) as a second, independent I2C bus for the sensors, built
behind `ENABLE_AIR_QUALITY` so it does nothing unless you turn it on.

### Parts

| Part | Why | Source | ~Price (CAD) |
|---|---|---|---|
| Sensirion **SEN55-SDN-T** (bare module) | PM1.0/2.5/4.0/10, VOC index, NOx index, temp, humidity | [DigiKey.ca](https://www.digikey.ca/en/products/detail/sensirion-ag/SEN55-SDN-T/16342756) | ~$49 |
| Adafruit **SEN54/SEN55 Adapter Breakout** (PID 5964) | Converts the SEN55's JST-GH connector to plain pins, **and** boosts whatever you feed it (3.3–5V) up to a clean 5V/100mA for the SEN55's fan — this is why it's worth keeping even without STEMMA QT elsewhere | [Adafruit](https://www.adafruit.com/product/5964) or DigiKey (search "Adafruit 5964") | ~$8 |
| Sensirion **SCD-41** breakout, *optional* (CO2) | Adafruit's (PID 5190) is the known-good one; a cheaper generic breakout works too if it exposes plain I2C pins | [Adafruit](https://www.adafruit.com/product/5190) | ~$55, or less generic |
| JST-GH 1.0mm 6-pin to loose-wire pigtail cable | Comes with most Sensirion eval boards; buy separately if not included with the SEN55 | any Sensirion/Adafruit reseller | ~$3–5 |
| A short length of wire + a connector matching the board's 12-pin SH1.0 cable (or just splice in) | Taps `3V3`, `GND`, `GPIO17` (SDA), `GPIO18` (SCL) off the existing cable | any electronics shop | ~$3 |

**Total: ~$60 CAD without CO2, ~$115 CAD with it** — no second MCU, no second
enclosure box.

### Wiring

The board's 12-pin cable, pin by pin (confirmed from the schematic):
```
1: GND   2: VSYS   3: RUN/RESET   4: BOOT/GPIO0   5: GND   6: ADC_AVDD/3V3
7: GPIO15   8: GPIO16   9: GPIO17   10: GPIO18   11: GPIO21   12: GPIO33
```

1. Power the SEN55 adapter from pin **6 (3V3)** and **5 or 1 (GND)**. The
   adapter's own boost converter handles the SEN55 fan's 5V need — this is
   why dropping it in favour of a bare-wired SEN55 isn't worth the ~$8
   saved, unless you've confirmed `VSYS` (pin 2) is a steady 5V on your
   build with a multimeter (it tracks the battery-charger IC's output, not
   guaranteed — see the firmware comment next to `AIR_SDA`/`AIR_SCL`).
2. Wire the adapter's SDA/SCL to **pin 9 (GPIO17)** / **pin 10 (GPIO18)**.
3. If adding the SCD41, daisy-chain it off the same SDA/SCL/3V3/GND (address
   `0x62`, no conflict with the SEN55's `0x69`).
4. No SD card needed — the firmware buffers to the board's own internal
   flash (LittleFS) when the NAS is unreachable.

### Firmware

Build `kp125_round_display.ino` with
`--build-property "compiler.cpp.extra_flags=-DENABLE_AIR_QUALITY=1"` (or set
`ENABLE_AIR_QUALITY` to `1` directly in the sketch), **and switch the FQBN's
partition scheme** — the default one has no room once Sensirion's libraries,
LittleFS and HTTPClient link in (confirmed by actually overflowing it: 101%
of a 1.3MB app partition). Use:
```
esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled
```
which gives a 3MB app partition on this board's real 16MB flash — 42% used
with air quality on, comfortable headroom. Building *without*
`ENABLE_AIR_QUALITY` doesn't need this change (90% of the default 1.3MB
partition, same as before this feature existed).

In `secrets.h`, add:
```c
#define NAS_INGEST_URL "http://ds923plus:8787/ingest"
#define NAS_API_KEY    "<same value as tools/airquality-receiver's .env>"
#define AIR_DEVICE_ID  "printroom1"
```
And make sure `AIR_PRINTER_PLUG_NAME` (near the top of the sketch) matches
the exact name you gave the printer's plug in `PLUGS_INIT` — the firmware
reads that plug's already-polled wattage rather than opening a second TPAP
session to the same plug.

**Verified:** compiled clean both ways — `ENABLE_AIR_QUALITY` left at its
default (0, real `secrets.h` untouched, 90% flash on the default partition,
unaffected) and set to 1 (isolated test copy, placeholder `secrets.h`, 42%
flash on the `app3M_fat9M_16MB` partition). Caught and fixed two real bugs
along the way: an Arduino auto-prototype ordering issue (the `AirSample`
struct had to move before `rgb()` — the actual first function in the whole
file, not `readPlug()` as first assumed) and the partition overflow above.
**Not yet wired to real sensors or run with the feature actually on.**

### Enclosure

Just `sensor_mount` from `stand/airquality_enclosure.scad` — a vented
bracket for the SEN55 adapter + SCD41, mounted near the existing display
stand. No second box: it's the same board.

## Option B: Standalone second ESP32

For a fully separate, portable unit — its own MCU, its own enclosure. Pricier
(~$170–180 CAD) because it duplicates what the round display already has
(a Wi-Fi MCU, a stand).

| Part | Why | Source | ~Price (CAD) |
|---|---|---|---|
| Sensirion **SEN55-SDN-T** (bare module) | Main sensor | [DigiKey.ca](https://www.digikey.ca/en/products/detail/sensirion-ag/SEN55-SDN-T/16342756) | ~$49 |
| Adafruit **SEN54/SEN55 Adapter Breakout** (PID 5964) | STEMMA QT + fan power | [Adafruit](https://www.adafruit.com/product/5964) | ~$8 |
| Adafruit **SCD-41** breakout (PID 5190) | CO2, optional | [Adafruit](https://www.adafruit.com/product/5190) | ~$55 |
| Adafruit **Feather ESP32 V2** (PID 5400) | MCU, STEMMA QT port | DigiKey **1528-5400-ND** | ~$35 |
| Adafruit **Adalogger FeatherWing** (PID 2922) | microSD, stacks on the Feather | [Adafruit](https://www.adafruit.com/products/2922) | ~$13 |
| microSD card (4–16 GB) | Queue buffer | any shop | ~$10 |
| STEMMA QT cable ×2 | Feather → SEN55 adapter → SCD41 | [Adafruit](https://www.adafruit.com/product/4209) | ~$6 |

Wiring, firmware target, and the `box`/`lid`/`fit_test` enclosure parts are
all in `firmware/airquality_logger/` and `stand/airquality_enclosure.scad` —
see those files' own header comments. Compiles clean against
`esp32:esp32:adafruit_feather_esp32_v2` (35% flash). Not yet wired, flashed,
or test-printed.

## Both options

Mount sensors near the printer but not in the direct nozzle exhaust or a
cooling fan's draft — ambient room air is what you want. The SEN55 has its
own aspirating fan; don't block its inlet or exhaust vents.

## Licensing note

`stand/*.scad` is currently CC-BY-NC-SA-4.0 (**non-commercial use only**). If
this is going to be sold as a kit, that license needs to change before it
ships to anyone — flagged, not yet resolved.
