# Security Policy

## Supported versions

There are no numbered releases. Only the latest code on the `main` branch is supported; fixes land there.

## Reporting a vulnerability

Please report security problems privately, not in a public issue:

1. Open the repository's **Security** tab.
2. Click **Report a vulnerability** (or go straight to [the private report form](https://github.com/AndreCouture/kp125-round-monitor/security/advisories/new)).

Include what is affected (firmware, stand, tools), how to reproduce it, and what an attacker could do with it. Never include your own Wi-Fi or TP-Link passwords.

This project is maintained by one person, so replies are best effort. You'll be told whether the report is accepted, and accepted issues are fixed on `main` and credited in the advisory unless you prefer otherwise.

## In scope

- The firmware's communication with the plugs (`kasa.h`, `tpap.h`): TPAP handshake, session encryption and the device certificate check, KLAP and the legacy protocol.
- Leaks of credentials from the firmware, its serial output or the repository tooling (`.githooks/pre-commit`).

## Known limitations (not vulnerabilities)

- **Credentials in flash:** Wi-Fi and TP-Link credentials are compiled into the firmware in plain text, so anyone with physical access to the board can read them. ESP32 flash encryption would prevent this but permanently changes the chip, so it is left to the owner. See [docs/setup.md](docs/setup.md#11-security-notes) and [docs/tpap-design.md](docs/tpap-design.md).
- **Legacy Kasa protocol:** older plugs that use TCP port 9999 have no real encryption; that is how those plugs work.
- **Plain HTTP for TPAP:** the plugs negotiate TPAP without TLS; the session itself is encrypted and authenticated by TPAP.
