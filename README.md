# KP125 Round Power Monitor

Live power readout from TP-Link Kasa KP125 smart plugs on a Waveshare ESP32-S3 1.28" round display, in a 3D-printed desk stand.

## Hand this to Claude Code
1. Unzip somewhere, e.g. `~/projects/kp125-round-monitor`.
2. Plug the board in over USB-C.
3. In that folder run `claude` and say something like:
   > Read CLAUDE.md and continue the project. Start with task 1.

Claude Code will pick up the context, task list and current status from `CLAUDE.md`.

## Secrets
Wi-Fi and TP-Link credentials live only in each sketch's git-ignored `secrets.h` (copy `secrets.example.h`). After cloning, enable the guard that refuses commits containing them:
```
git config core.hooksPath .githooks
```

## Doing it by hand
**Firmware (Arduino IDE)**
- Boards Manager: *esp32 by Espressif*. Board: **ESP32S3 Dev Module**, Flash Size 16MB, PSRAM enabled.
- Libraries: **LovyanGFX** (lovyan03) and **ArduinoJson** v7. The display fonts (Inter, anti-aliased) are generated into `fonts_inter.h` by `tools/make_vlw.py`.
- Open `firmware/kp125_monitor` first, set Wi-Fi + plug IPs, flash, and check the Serial Monitor (115200) shows readings.
- Then flash `firmware/kp125_round_display` with the same settings, and set `MAX_W` to your gauge full-scale (e.g. circuit limit).
- If upload fails: hold BOOT, tap RESET, release BOOT, upload again.

**Stand (OpenSCAD)**
- Measure your board and update the "measure these" block at the top of `stand/display_stand.scad`.
- Print `PART="fit_test"` first (tiny ring) to check the fit, then `stand` and `cap`.
- Stand prints front-face-down, no supports. The board drops in from the back; the cap presses in behind it.

## Files
- `CLAUDE.md` — project brief + task list for Claude Code
- `docs/hardware.md` — pinout, dimensions, plug protocol notes
- `firmware/` — Arduino sketches
- `stand/display_stand.scad` — parametric stand + cap
