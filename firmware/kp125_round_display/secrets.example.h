// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 12113775 Canada Inc.

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

// Only used when built with ENABLE_AIR_QUALITY=1 (SEN55 + SCD41 on this same board, see
// docs/parts-airquality-logger.md). Harmless to leave as-is otherwise.
#define NAS_INGEST_URL "http://ds923plus:8787/ingest"
// Shared secret: generate your own (openssl rand -hex 16) and put the same value in the
// receiver's .env (see tools/airquality-receiver/).
#define NAS_API_KEY "change-me"
#define AIR_DEVICE_ID "printroom1"
