// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 12113775 Canada Inc.

// Copy this file to secrets.h (git-ignored) and fill in your values.
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// TP-Link (Kasa app) account — needed for the printer plug, same as kp125_monitor.
// See kp125_monitor/kasa.h: legacy/KLAP plugs can leave these blank, TPAP plugs (e.g. KP125M) need them.
#define KASA_USER "you@example.com"
#define KASA_PASS "your-kasa-password"

// The plug powering the 3D printer, so each log row carries printer wattage.
// Give it a DHCP reservation so the IP never changes.
#define PRINTER_PLUG_NAME "Printer"
#define PRINTER_PLUG_IP   "192.168.1.52"

// Small HTTP receiver on the NAS (see tools/airquality-receiver/). Plain HTTP on the
// LAN only — don't expose this endpoint outside your network.
#define NAS_INGEST_URL "http://ds923plus:8787/ingest"
// Shared secret: generate your own (e.g. `openssl rand -hex 16`) and put the same
// value in the receiver's API_KEY environment variable.
#define NAS_API_KEY "change-me"

// Identifies this logger in the NAS database if you ever run more than one.
#define DEVICE_ID "printroom1"
