# Hardware notes

## Waveshare ESP32-S3-LCD-1.28 (non-touch)
- SoC ESP32-S3R2 (2 MB PSRAM), 16 MB external flash, CH343P USB-UART (UART0 on GPIO43/44), auto-download circuit.
- Display: GC9A01A, 240×240 round IPS, 4-wire SPI up to 80 MHz, active area Ø32.4 mm.
- IMU: QMI8658 on I2C.
- Battery: MX1.25 2-pin 3.7 V Li-ion, ETA6096 charger; battery sense on GPIO1 through a 200k/100k divider (V = raw × 3.3 / 4096 × 3).

| GPIO | Function |
|---|---|
| 8  | LCD_DC |
| 9  | LCD_CS |
| 10 | LCD_CLK |
| 11 | LCD_MOSI |
| 12 | LCD_RST |
| 40 | LCD_BL (backlight, PWM-able) |
| 6 / 7 | I2C SDA / SCL (IMU) |
| 5  | TP_INT (touch version) |
| 47 / 48 | IMU INT1 / INT2 |
| 1  | Battery ADC |
| 0  | BOOT |

**Touch version (ESP32-S3-Touch-LCD-1.28)** differs: CST816S touch on I2C, backlight on **GPIO2**, and a 6-GPIO SH1.0 connector instead of the 1.27 mm header. Check its wiki for LCD_RST before using.

Sources: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28 , https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28 (schematic + 3D model zip linked from the wiki — useful for exact stand dimensions).

## Physical dimensions — NOT yet measured
Estimates used in `stand/display_stand.scad`:
- Round outline ≈ 38.6 mm (the touch version's lens is specced at 38.51 mm OD; non-touch assumed similar).
- Total stack front glass → back components ≈ 10 mm (guess).
- USB-C socket protrudes ≈ 4 mm below the round outline, centre ≈ 7 mm behind the front glass (guess from photos).
Waveshare publishes a 3D model (`ESP32-S3-LCD-1.28.zip` on the wiki) — use it to replace these guesses if available.

## KP125 local protocol (legacy Kasa)
- TCP port 9999. Frame = 4-byte big-endian length + payload.
- Payload obfuscation: XOR autokey, key starts at 171; encrypt `key = out[i]`, decrypt `key = in[i]`.
- `{"emeter":{"get_realtime":{}}}` → `{"emeter":{"get_realtime":{"voltage_mv":…,"current_ma":…,"power_mw":…,"total_wh":…,"err_code":0}}}`
- `{"system":{"get_sysinfo":{}}}` gives alias, model, firmware, relay state.
- KP125**M** (Matter) and some newer firmware use **KLAP** (HTTP POST /app/handshake1, /app/handshake2, then /app/request with AES-128-CBC). Reference implementation: python-kasa. Implemented in `firmware/*/kasa.h`, but not yet verified against a real plug (see below).

## What the owner's plugs actually are (probed 2026-10-05)
Both plugs are **KP125M(US)**, firmware **1.4.1 Build 260721 Rel.06565**, `device_type` `SMART.KASAPLUG`. On this firmware:
- TCP 9999 is refused (no legacy protocol).
- TP-Link discovery (UDP 20002) and `POST /` with `{"method":"login","params":{"sub_method":"discover"}}` both report `encrypt_type: "TPAP"`, `tpap_preferred: true`.
- KLAP `POST /app/handshake1` returns **HTTP 403** before any credential check; the old AES transport `POST /app` returns `error_code 1003`.
- Turning on Kasa app → Me → Settings → **Third Party Compatibility** (including toggling it off/on, and power-cycling a plug) did **not** change `tpap_preferred`.

TPAP is SPAKE2+ (P-256) based. As of 2026-10-05 python-kasa has no released TPAP transport ([PR #1592](https://github.com/python-kasa/python-kasa/pull/1592) open; context in [issue #1733](https://github.com/python-kasa/python-kasa/issues/1733)). The KLAP check that matters is `tpap_preferred: false` from the HTTP discover call: in that issue, plugs reporting `false` worked over plain KLAP (login version 2).
- Poll no faster than every ~2 s.
