# CLAUDE.md — KP125 round power monitor

Handoff from a claude.ai chat. Read this file, `README.md` and `docs/hardware.md` first.

## What we're building
A small desk gadget that shows live electricity use from TP-Link Kasa **KP125** smart plugs:
- **Board:** Waveshare **ESP32-S3-Touch-LCD-1.28** (round 240×240 GC9A01 IPS, ESP32-S3R2, 16 MB flash, 2 MB PSRAM, CH343 USB-serial, CST816S touch, QMI8658 IMU). Photo: `docs/photos/board.jpeg`. Confirmed on hardware: LCD_RST **14**, backlight **GPIO2** (the non-touch pins leave the screen black); see `docs/hardware.md`.
- **Data:** polls each plug over the LAN. The owner's plugs are actually **KP125M** on firmware 1.4.1, which only speak **TPAP** (SPAKE2+); legacy (TCP 9999) and KLAP are implemented in `kasa.h` but rejected by these plugs. See `docs/hardware.md` for the probed protocol profile.
- **Display:** arc gauge of total W vs `MAX_W`, big total, "W left" headroom, cycling per-plug line, online/offline dots.
- **Enclosure:** 3D-printed tilted desk stand + press-fit back cap (OpenSCAD).

## Repo layout
```
firmware/kp125_round_display/   main sketch for the round display (Arduino, primary)
firmware/kp125_monitor/         serial-only version, handy for testing plug comms on any ESP32
stand/display_stand.scad        stand + cap, parametric, first draft
docs/setup.md                   user guide: parts, supported plugs, configuration, settings, troubleshooting
docs/hardware.md                pinout, dimensions, protocol notes, sources
docs/tpap-design.md             TPAP client design, security decisions, review
tools/make_vlw.py               generates the anti-aliased display fonts (fonts_inter.h)
docs/photos/board.jpeg          photo of the board back
```

## Current status
| Piece | State |
|---|---|
| `kp125_monitor.ino` | Compiles and runs. Legacy + KLAP + TPAP + discovery; reads the owner's KP125M plugs. |
| `kp125_round_display.ino` | Compiles and runs on the touch board. LovyanGFX (replaced Arduino_GFX) with anti-aliased Inter fonts from `tools/make_vlw.py`; touch pages gauge/TASK/TODAY/MONTH; `DEMO_MODE` for layout checks. |
| TPAP transport | Ported to the ESP32 (`tpap.h`, see `docs/tpap-design.md`); self-test (known-answer + rejection paths) passes, reads both plugs live ("Bambu" .140, "Workbench" .252); back-off on failed logins. Reviewed by the local LLM gateway 2026-10-06 (see the design doc). |
| `display_stand.scad` | Renders clean (OpenSCAD 2026.06, Manifold). Board numbers from Waveshare's drawing/DXF for the touch board; straight USB-C plug assumed 30 mm rigid (measure). **Not yet test-printed** — print `fit_test` first. |

## Tasks, in order
1. **Toolchain.** Prefer `arduino-cli` (or PlatformIO if the owner prefers). Install core `esp32:esp32` (3.x is fine; Waveshare's own demos pin 2.0.12 only because of TFT_eSPI — we don't use it). Libraries: `GFX Library for Arduino`, `ArduinoJson`. FQBN: `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=enabled` (verify option names with `arduino-cli board details`).
2. **Secrets.** Move `WIFI_SSID`/`WIFI_PASS` and the plug list into a git-ignored `secrets.h` (+ committed `secrets.example.h`) in both sketches. Ask the owner for plug names/IPs; suggest DHCP reservations.
3. **Compile both sketches**; fix any API mismatches (Arduino_GFX constructor signatures and `Arduino_Canvas` have changed across versions — check the installed version's examples for GC9A01 on ESP32-S3).
4. **Flash `kp125_monitor` first** and confirm on the serial monitor (115200) that the plugs answer. If every plug times out on port 9999 even with correct IPs, the plugs have newer firmware using the **KLAP** protocol → implement KLAP (handshake1/handshake2 with SHA-256 of credentials, then AES-128-CBC; reference: python-kasa `kasa/transports/klaptransport.py`) using mbedtls, behind the same `readPlug()` interface. Needs the owner's TP-Link account email/password in `secrets.h`.
5. **Flash `kp125_round_display`** and check: backlight on, no flicker, arc/text positions look centred on the round glass, offline plug doesn't stall the UI. Tune `MAX_W` with the owner.
6. **Stand.** Install OpenSCAD (CLI) and render: `openscad -o out/stand.stl -D 'PART="stand"' stand/display_stand.scad`, same for `cap` and `fit_test`; also render PNG previews (`--render --imgsize=1000,800 -D 'PART="both"'`) and look at them. Fix any non-manifold/geometry bugs. Then ask the owner to measure with calipers and update the "measure these" block: `board_d`, `stack_t` (front glass → tallest back component incl. the 12-pin header), `view_d`, `usb_out`, `usb_y`, and their USB-C plug (`plug_straight`, right-angle or straight). They should print `fit_test` before the full stand.
7. **Nice-to-haves (ask before doing):** MQTT or a tiny web page with the readings; daily kWh; screen dimming at night (backlight PWM on the BL pin); IMU-based auto-rotate or tap-to-wake; battery voltage readout (GPIO1, ×3 divider).

## Conventions
- Arduino folder name must equal the sketch name — keep that structure.
- Never commit Wi-Fi or TP-Link credentials.
- Keep the stand fully parametric; all fit-critical numbers live at the top of the .scad.
- Stand print orientation: front face down on the bed (no supports except a short bridge over the cable tunnel); cap prints flat.

## Owner context
- Has KP125 plugs already installed and using the Kasa app.
- Prints their own parts and uses OpenSCAD (style reference: their toothbrush stand design — parameters at top, `PART` selector, print orientation baked in).
- Uses a right-angle USB-C cable on the board (elbow turns sideways), so the stand has a cross-tunnel for the elbow.
