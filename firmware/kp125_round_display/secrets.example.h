// Copy this file to secrets.h (git-ignored) and fill in your values.
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// TP-Link (Kasa app) account, only needed for plugs on KLAP firmware (see kasa.h)
#define KASA_USER "you@example.com"
#define KASA_PASS "your-kasa-password"

// {"label", "ip"} per plug. Give each plug a DHCP reservation so its IP never changes.
#define PLUGS_INIT \
  {"Desk",   "192.168.1.50"}, \
  {"Fridge", "192.168.1.51"},
