# Setup guide

## 1. What you need

| Part | Notes | Where to buy |
| :--- | :--- | :--- |
| **Waveshare ESP32-S3-Touch-LCD-1.28** | The **touch** version. Round 1.28" 240×240 touch display, ESP32-S3, 16 MB flash, 2 MB PSRAM. The non-touch ESP32-S3-LCD-1.28 uses different display pins and shows a black screen with this firmware. | [Waveshare](https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm) · [Amazon.com](https://www.amazon.com/s?k=Waveshare+ESP32-S3-Touch-LCD-1.28) · [Amazon.ca](https://www.amazon.ca/s?k=Waveshare+ESP32-S3-Touch-LCD-1.28) |
| **TP-Link Kasa KP125M** smart plug(s) | Matter, energy monitoring, 15 A / 1800 W, 2.4 GHz Wi-Fi. One or more. | [TP-Link](https://www.tp-link.com/us/home-networking/smart-plug/kp125m/) · [Amazon.com](https://www.amazon.com/s?k=Kasa+KP125M) · [Amazon.ca](https://www.amazon.ca/s?k=Kasa+KP125M) |
| USB-C data cable | Must carry data (not charge-only) for flashing. The stand is designed for a **straight** USB-C plug with a rigid part of about 30 mm. | [Amazon.com](https://www.amazon.com/s?k=USB-C+data+cable+short) · [Amazon.ca](https://www.amazon.ca/s?k=USB-C+data+cable+short) |
| 5 V USB power adapter | Any phone charger; the board draws well under 1 A. | [Amazon.com](https://www.amazon.com/s?k=5V+USB+wall+charger) · [Amazon.ca](https://www.amazon.ca/s?k=5V+USB+wall+charger) |
| PLA filament *(optional)* | For the desk stand and cap, if you have a 3D printer. | [Amazon.com](https://www.amazon.com/s?k=PLA+filament+1.75mm) · [Amazon.ca](https://www.amazon.ca/s?k=PLA+filament+1.75mm) |
| 3.7 V Li-ion/LiPo battery, MX1.25 2-pin *(optional)* | Plugs into the board's battery connector; the display then shows its voltage. **Check the polarity against the + and − marks on the board before connecting**: MX1.25 battery plugs are not wired the same way by every seller, and reversed polarity can destroy the board. The stand's cap has no space for a battery. | [Amazon.com](https://www.amazon.com/s?k=3.7V+lipo+battery+MX1.25+2pin) · [Amazon.ca](https://www.amazon.ca/s?k=3.7V+lipo+battery+MX1.25+2pin) |

The Amazon links are searches for the exact model, so they stay current; check that the listing says **ESP32-S3-Touch-LCD-1.28** (touch) and **KP125M**.

## 2. Supported smart plugs

| Plug | Protocol | Status |
| :--- | :--- | :--- |
| Kasa KP125M, firmware 1.4.1 | TPAP (SPAKE2+ login, AES-128-CCM, device certificate check) | **Tested** |
| Kasa plugs on the older local protocol (TCP port 9999), e.g. KP115, HS110, older KP125 firmware | Legacy Kasa | Implemented, not tested on real hardware |
| Kasa/Tapo SMART plugs that use KLAP | KLAP | Implemented, not tested on real hardware |

The firmware picks the protocol for each plug automatically on first contact. Multi-outlet power strips (e.g. HS300) are not supported. Energy monitoring is required; plugs without it cannot be read.

## 3. Prepare the plugs

1. Set each plug up in the Kasa app on a **2.4 GHz** Wi-Fi network. Note the TP-Link account email and password: KP125M plugs need them for local access.
2. In your router, give each plug a **DHCP reservation** so its IP address never changes.
3. The display must be on a network that can reach the plugs (for example the same IoT Wi-Fi).
4. Optional: Kasa app → Me → Settings → **Third Party Compatibility**. On KP125M firmware 1.4.1 this did not change the protocol, and the firmware does not need it.

## 4. Install the toolchain

- `arduino-cli` (or the Arduino IDE) with the board package **esp32 by Espressif** 3.x.
- Libraries from the Library Manager: **LovyanGFX** and **ArduinoJson** (v7).
- Board settings: **ESP32S3 Dev Module**, Flash Size **16MB**, PSRAM **enabled**. With arduino-cli the FQBN is `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=enabled`.
- After cloning, enable the commit guard that blocks committing credentials:
  ```
  git config core.hooksPath .githooks
  ```

## 5. Configure `secrets.h`

Each sketch folder (`firmware/kp125_round_display` and `firmware/kp125_monitor`) needs its own `secrets.h`, copied from `secrets.example.h`. It is git-ignored; never commit it.

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// TP-Link (Kasa app) account, only needed for plugs on KLAP or TPAP firmware
#define KASA_USER "you@example.com"
#define KASA_PASS "your-kasa-password"

// {"label", "ip"} per plug
#define PLUGS_INIT \
  {"Desk",   "192.168.1.50"}, \
  {"Fridge", "192.168.1.51"},
```

- Labels are shown on screen.
- The Wi-Fi must be 2.4 GHz.
- Credentials are compiled into the firmware and stored in the board's flash in plain text (see [Security notes](#11-security-notes)).

## 6. Settings

They are at the top of `firmware/kp125_round_display/kp125_round_display.ino`; change them and re-flash.

| Setting | Default | Meaning |
| :--- | :--- | :--- |
| `MAX_W` | `1440` | Gauge full scale and "W left" headroom. 1440 = 15 A × 120 V × 80 % continuous load. |
| `RATE_PER_KWH` | `0.11142` | Your electricity rate per kWh for the cost estimate; `0` hides cost. Energy only: no delivery, fixed charges or tax. |
| `CURRENCY` | `"$"` | Shown before costs. |
| `TZ_INFO` | `"EST5EDT,M3.2.0,M11.1.0"` | POSIX time zone; used for midnight resets, night dimming and daily history. |
| `NTP_SERVER1` / `NTP_SERVER2` | `"pool.ntp.org"` / `"time.google.com"` | Time servers (both are asked). The time is required before the plugs' certificates can be checked. To run without internet, set the first to a LAN NTP server such as your router's IP and keep a public one second as a fallback. Also in `kp125_monitor.ino`. |
| `NIGHT_FROM` / `NIGHT_TO` | `22` / `7` | Night dimming window in local hours (may wrap midnight; equal values = never dim). Can also be set at build time, e.g. `-DNIGHT_FROM=12 -DNIGHT_TO=14`, to test during the day. |
| `DAY_BRIGHTNESS` / `NIGHT_BRIGHTNESS` | `255` / `12` | Backlight levels 0–255; `0` turns the screen off at night. |
| `WAKE_MS` | `30000` | How long a touch keeps the screen bright at night. |
| `POLL_MS` | `3000` | How often each plug is read. |
| `PAGE_TIMEOUT_MS` | `20000` | TODAY, MONTH and 7 DAYS return to the gauge after this long without a touch. |

## 7. Flash and first boot

1. *Optional but useful:* flash `firmware/kp125_monitor` first and open the serial monitor at **115200** baud. It prints each plug's power, voltage, current and energy, or why a plug did not answer.
2. Flash `firmware/kp125_round_display`, for example:
   ```
   arduino-cli compile -b esp32:esp32:esp32s3:FlashSize=16M,PSRAM=enabled firmware/kp125_round_display
   arduino-cli upload  -b esp32:esp32:esp32s3:FlashSize=16M,PSRAM=enabled -p <port> firmware/kp125_round_display
   ```
3. On first boot the board joins Wi-Fi, gets the time over NTP (needed to verify the plugs' certificates; readings start a few seconds later), then logs in to each plug.
4. If an upload fails: hold **BOOT**, tap **RESET**, release **BOOT**, and upload again.

## 8. Using the display

**Gestures:** tap or swipe right = next page · swipe left = previous page · swipe up/down = change plug or meter where a page has several · press and hold = reset (TASK page). The dot row at the bottom, with ◀ ▶ at its ends, shows the page; a vertical dot column on the right, with ▲ ▼, means that page has a swipe up/down choice and shows which one is selected.

| Page | Shows |
| :--- | :--- |
| **Gauge** | Live power on an arc gauge (green below 50 % of `MAX_W`, amber to 80 %, red above). Swipe up/down to switch between **LIVE - ALL** (all plugs added together, with the headroom left before `MAX_W` and how many plugs are online) and **LIVE - *plug name*** (that plug's watts, volts and amps, and the total for comparison). The vertical dots on the right are ALL then each plug: green = online, red = offline; the larger one is shown. |
| **TASK** | Task meters: one for all plugs and one per plug (swipe up/down). Press and hold about 1.2 s to reset and start the meter shown. Shows energy, estimated cost, elapsed time, average and max power. Saved to flash, so it survives reboots. |
| **TODAY** | Today's kWh and cost, plus the day's peak and lowest total power with their times. |
| **MONTH** | This month's kWh and cost, total and per plug. |
| **7 DAYS** | Daily kWh bars for the last 7 days from the plugs' own history, with the 7-day total and cost (swipe up/down for all plugs or one plug). |
| **CLOCK** | 24-hour time, the date and the current total power. One swipe left from the gauge; it stays up (no timeout). |

At night the screen dims; the first touch only wakes it. It stays bright if total power is above `MAX_W`.

**Battery:** if a 1-cell Li-ion is plugged into the board's MX1.25 connector, the gauge page shows its voltage and an approximate charge icon at the top. Without a battery nothing is shown (the board reads about 4.8 V there, which no Li-ion reaches). The charge level is a rough estimate from the voltage and reads high while charging.

## 9. Print the stand

- File: `stand/display_stand.scad` (OpenSCAD). Two versions share the same display ring and cap:
  - **Fixed stand:** `stand` (tilt set before printing with `tilt`, default 15°).
  - **Adjustable stand:** `hinge_head`, `hinge_base` and `hinge_pin` (the file prints two pins). The head clicks into 0°, 10°, 20° and 30° of backward tilt (`hinge_tilts`); tilt it by hand.
  - Plus `cap`, and the fit tests `fit_test` / `fit_test_set`. Render one part at a time, e.g.
  ```
  openscad -o stand.stl -D 'PART="stand"' stand/display_stand.scad
  openscad -o hinge_head.stl -D 'PART="hinge_head"' stand/display_stand.scad
  ```
- **Hinged stand assembly:** fit the board and cap into the head, then set the head's ears between the base's cheeks and push a pin through each cheek into the ear (snug in the cheek, free in the ear). The pins stop short of the cable channel. The cable runs down between the ears and out the back of the base. The head prints front face down, the base flat, the pins standing on their heads. Click stops: a bump on each cheek drops into a dimple on the ear; set the steps no closer than 10° apart.
- **Cable ties (optional, both stands):** pairs of slots through the base, joined by a groove underneath so the base stays flat. Thread a tie down one slot and up the other, round the cable. The fixed stand has anchors left and right of the pedestal (where the cable leaves the side tunnel) and at the back; the hinged base has one at the back. `tie_anchors = false` removes them.
- Print **`fit_test_set`** first: three thin rings with 0.4, 0.5 and 0.6 mm clearance, marked with 1, 2 and 3 notches. Put the board in each, lens down, connector toward the pointed end, and set `clr` to the clearance that fits best (0.4 on the author's printer). Use the same printer and nozzle for the test and the stand.
- The stand prints **front face down** with no supports except a short bridge over the cable tunnel; the cap prints flat.
- The board sits in the ring from the back and the cap presses in behind it. The USB-C cable goes down a channel under the display and leaves through a side tunnel.
- The cap holds by six crush ribs that run about 6 mm along the pocket wall (`cap_fit`, 0.2 mm; raise it if the cap is loose, lower it if it's too hard to fit). They sit on the round part of the pocket, at angles clear of the board's edge components. Four feet on the cap press the bare edge of the PCB so the display can't wobble: push the cap in until you feel the feet touch the board.
- The board is not round at the bottom: below the lens the PCB narrows along 45° edges to a flat bottom around the USB-C socket. The pocket follows that outline (`pcb_corners`, from Waveshare's drawing) plus `clr`.
- `plug_straight` (default 30 mm, the rigid length of a straight USB-C plug) sets the pedestal height: measure yours.

## 10. Troubleshooting

| Symptom | Cause / fix |
| :--- | :--- |
| Screen stays black | Non-touch board. This firmware is for the touch version (backlight GPIO2, reset GPIO14). |
| `TPAP waiting for NTP time` | The board can't reach `NTP_SERVER1` or `NTP_SERVER2` yet. Check internet access, or point `NTP_SERVER1` at a LAN NTP server (e.g. your router). |
| `plug confirmation mismatch (wrong KASA_PASS?)` | `KASA_USER` / `KASA_PASS` don't match the TP-Link account the plugs belong to. |
| `backing off after N failed login(s)` | Repeated login failures: the firmware waits 3 s, doubling to 5 min, between attempts so the plug isn't flooded. |
| `port 9999 timed out` | Wrong IP, plug offline, or the display is on a network that can't reach the plug. |
| A 7 DAYS bar shows `-` | That day's history couldn't be read yet; it retries every 2 minutes. `--` instead of bars means the plug has no daily history (legacy protocol). |

## 11. Security notes

- Credentials live only in `secrets.h`, which is git-ignored; the pre-commit hook refuses commits containing them.
- Anyone with physical access to the board can read the credentials from flash. ESP32 flash encryption would prevent that but is a permanent change to the chip, so it is not enabled.
- TPAP plugs are checked against TP-Link's root certificate before any data is trusted.
