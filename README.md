# KP125 Round Power Monitor

A round touch display on your desk that shows live electricity use from TP-Link Kasa smart plugs, read directly over your home network, in a 3D-printed stand.

## Features

- Live total power on an arc gauge, with headroom against your circuit limit (`MAX_W`).
- Per-plug power, voltage and current.
- Task meters (all plugs or one plug): press and hold to start; energy, cost, time, average and max; they survive reboots.
- Today, this month and a 7-day history, with an estimated cost from your electricity rate.
- A 24-hour desk clock page, night dimming with touch to wake, and a battery voltage readout when a Li-ion is connected.
- Talks to the plugs locally, no cloud: Kasa KP125M over TPAP (TP-Link's newer encrypted protocol, with a device certificate check), plus legacy Kasa and KLAP.

## Parts

| Part | Where to buy |
|---|---|
| Waveshare **ESP32-S3-Touch-LCD-1.28** (touch version) | [Waveshare](https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm) · [Amazon.com](https://www.amazon.com/s?k=Waveshare+ESP32-S3-Touch-LCD-1.28) · [Amazon.ca](https://www.amazon.ca/s?k=Waveshare+ESP32-S3-Touch-LCD-1.28) |
| TP-Link Kasa **KP125M** smart plug(s) | [TP-Link](https://www.tp-link.com/us/home-networking/smart-plug/kp125m/) · [Amazon.com](https://www.amazon.com/s?k=Kasa+KP125M) · [Amazon.ca](https://www.amazon.ca/s?k=Kasa+KP125M) |
| USB-C cable (straight plug) and 5 V USB power adapter | Any |
| *Optional:* 3D printer and PLA for the stand | — |

See [docs/setup.md](docs/setup.md) for details and the list of supported plugs.

## Quick start

1. Set up the plugs in the Kasa app on 2.4 GHz Wi-Fi and give each a DHCP reservation.
2. Install `arduino-cli` with "esp32 by Espressif" 3.x and the libraries LovyanGFX and ArduinoJson.
3. In `firmware/kp125_round_display`, copy `secrets.example.h` to `secrets.h` and fill in Wi-Fi, your TP-Link account and the plug IPs.
4. Set `MAX_W` and `RATE_PER_KWH` at the top of `kp125_round_display.ino`.
5. Flash it with FQBN `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=enabled`.
6. Print the stand from `stand/display_stand.scad` (`fit_test` first).
7. After cloning, run `git config core.hooksPath .githooks` so credentials can't be committed.

## Documentation

- [docs/setup.md](docs/setup.md): parts, supported plugs, configuration, settings, using the display, the stand, troubleshooting.
- [docs/hardware.md](docs/hardware.md): board pinout and dimensions, plug protocols and the energy requests used.
- [docs/tpap-design.md](docs/tpap-design.md): how the TPAP client works, its security decisions and review.
- [CLAUDE.md](CLAUDE.md): project brief and status for Claude Code.

## Repository layout

```text
firmware/kp125_round_display/   the display firmware (main sketch)
firmware/kp125_monitor/         serial-only plug reader, useful for testing plugs
stand/display_stand.scad        parametric desk stand and back cap (OpenSCAD)
tools/make_vlw.py               generates the display fonts
docs/                           guides and notes
```

## Credits

- TPAP protocol details come from [python-kasa pull request #1592](https://github.com/python-kasa/python-kasa/pull/1592).
- Display fonts: [Inter](https://github.com/rsms/inter), SIL Open Font License 1.1 (see `fonts/Inter-LICENSE.txt`).
- Board dimensions from Waveshare's ESP32-S3-Touch-LCD-1.28 drawing.
