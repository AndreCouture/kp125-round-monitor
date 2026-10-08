// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 12113775 Canada Inc.

// ESP32 energy monitor for TP-Link Kasa KP125 smart plugs
// Polls each plug locally (legacy TCP 9999 or KLAP over HTTP, see kasa.h) and prints
// power, voltage, current, total kWh.
// At boot it also broadcasts a Kasa discovery and prints every plug that answers,
// ready to paste into secrets.h. Leave PLUGS_INIT undefined for discovery-only mode.
// Optional: shows live watts on a 128x64 SSD1306 OLED (set USE_OLED 1).
//
// Libraries (Arduino Library Manager):
//   - ArduinoJson (v7)
//   - Adafruit SSD1306 + Adafruit GFX   (only if USE_OLED = 1)
// Board: any ESP32 (Arduino-ESP32 core 2.x or 3.x)

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <vector>

#define USE_OLED 0

#if USE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#endif

// Wi-Fi credentials and plug list live in secrets.h (git-ignored)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and fill in your Wi-Fi and plug IPs"
#endif
#include "kasa.h"   // after secrets.h: uses KASA_USER / KASA_PASS
#if TPAP_SELFTEST
#include "tpap_selftest.h"   // build with -DTPAP_SELFTEST=1 to run the TPAP known-answer test at boot
#endif

// ---------- your settings ----------
struct Plug {
  const char* name;   // label for display
  const char* ip;     // give each plug a DHCP reservation
  float power_w = NAN, voltage_v = NAN, current_a = NAN, total_kwh = NAN;
  bool online = false;
  KasaConn conn;      // protocol (legacy/KLAP/TPAP) and session, found on first contact
};

#ifdef PLUGS_INIT
Plug plugs[] = { PLUGS_INIT };
const size_t N_PLUGS = sizeof(plugs) / sizeof(plugs[0]);
#else
Plug plugs[1];                   // discovery-only mode: nothing to poll
const size_t N_PLUGS = 0;
#endif

const uint32_t POLL_MS = 5000;   // 2-10 s is plenty
// Time servers (SNTP asks both); TPAP needs the time to check the plugs' certificates. To run
// without internet, put a LAN NTP server first (e.g. your router's IP) and keep a public one second.
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.google.com";
// -----------------------------------

String lastErr;   // why the last readPlug() failed, for the serial log

bool readPlug(Plug& p) {
  KasaEnergy e;
  if (!kasaReadEnergy(p.ip, p.conn, e, lastErr)) return false;
  p.power_w = e.w; p.voltage_v = e.v; p.current_a = e.a; p.total_kwh = e.kwh;
  return true;
}

// Broadcast get_sysinfo on UDP 9999 (same XOR, no length prefix) and print each
// plug that answers. Plugs on KLAP-only firmware don't answer this.
void discoverPlugs() {
  const char* cmd = "{\"system\":{\"get_sysinfo\":{}}}";
  size_t n = strlen(cmd);
  std::vector<uint8_t> out(n);
  kasaEncrypt((const uint8_t*)cmd, out.data(), n);

  WiFiUDP udp;
  udp.begin(9998);
  std::vector<IPAddress> seen;
  std::vector<uint8_t> buf(2048);
  Serial.printf("Discovering Kasa plugs on %s ...\n", WiFi.broadcastIP().toString().c_str());
  Serial.println("#define PLUGS_INIT \\");

  for (int round = 0; round < 3; round++) {
    udp.beginPacket(WiFi.broadcastIP(), 9999);
    udp.write(out.data(), n);
    udp.endPacket();
    for (uint32_t t0 = millis(); millis() - t0 < 1000; delay(10)) {
      int len = udp.parsePacket();
      if (len <= 0) continue;
      IPAddress ip = udp.remoteIP();
      len = udp.read(buf.data(), buf.size() - 1);
      bool dup = false;
      for (auto& s : seen) dup |= (s == ip);
      if (dup || len <= 0) continue;
      seen.push_back(ip);

      kasaDecrypt(buf.data(), len);
      buf[len] = 0;
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)buf.data())) continue;
      JsonObject si = doc["system"]["get_sysinfo"];
      bool emeter = strstr(si["feature"] | "", "ENE") != nullptr;
      Serial.printf("  {\"%s\", \"%s\"}, /* %s mac %s%s */ \\\n",
                    si["alias"] | "?", ip.toString().c_str(), si["model"] | "?",
                    si["mac"] | "?", emeter ? "" : "  (no energy meter)");
    }
  }
  udp.stop();
  if (seen.empty())
    Serial.println("  (none found: plugs on another network/VLAN, or KLAP-only firmware)");
  Serial.println();
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " ok " + WiFi.localIP().toString() : " failed");
  // TPAP checks the plug's certificate dates, so it needs real time (UTC is fine)
  static bool sntp = false;
  if (WiFi.status() == WL_CONNECTED && !sntp) { configTime(0, 0, NTP_SERVER1, NTP_SERVER2); sntp = true; }
}

#if USE_OLED
void drawOled(float total) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.printf("%.0f W", total);
  oled.setTextSize(1);
  int y = 20;
  for (size_t i = 0; i < N_PLUGS && y < 64; i++, y += 11) {
    oled.setCursor(0, y);
    if (plugs[i].online) oled.printf("%-8s %7.1f W", plugs[i].name, plugs[i].power_w);
    else                 oled.printf("%-8s  offline", plugs[i].name);
  }
  oled.display();
}
#endif

// TPAP's elliptic-curve and certificate code needs more than the default 8 KB loop stack.
// (Kept below the struct definitions: the macro defines a function, and Arduino inserts its
// generated prototypes above the first function.)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  Serial.begin(115200);
#if USE_OLED
  Wire.begin();                         // default SDA 21, SCL 22
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay(); oled.display();
#endif
  connectWiFi();
#if TPAP_SELFTEST
  // After Wi-Fi so SNTP can set the clock: the DAC checks in the self-test need real time
  for (int i = 0; i < 40 && !tpap::clockReady(); i++) delay(250);
  tpap::selfTest();
#endif
  if (WiFi.status() == WL_CONNECTED) discoverPlugs();
}

#if SERIAL_CONSOLE
// Development console, only in builds with -DSERIAL_CONSOLE=1 (it can send ANY command, including
// switching a plug off). Type "<plug number> <json>", e.g.  1 {"method":"get_device_time"}
void serialConsole() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') { if (line.length() < 1024) line += ch; continue; }
    int sp = line.indexOf(' ');
    int n = sp > 0 ? line.substring(0, sp).toInt() : 0;
    if (n >= 1 && n <= (int)N_PLUGS) {
      String reply, err;
      Plug& p = plugs[n - 1];
      if (kasaRawQuery(p.ip, p.conn, line.substring(sp + 1).c_str(), reply, err)) Serial.printf("<< %s\n", reply.c_str());
      else Serial.printf("!! %s\n", err.c_str());
    } else if (line.length()) {
      Serial.printf("usage: <plug 1..%u> <json>\n", (unsigned)N_PLUGS);
    }
    line = "";
  }
}
#endif

void loop() {
#if SERIAL_CONSOLE
  serialConsole();
#endif
  if (N_PLUGS == 0) {              // discovery-only mode
    delay(30000);
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) discoverPlugs();
    return;
  }

  static uint32_t last = 0;
  if (millis() - last < POLL_MS && last != 0) return;
  last = millis();

  connectWiFi();
  float total = 0;
  for (size_t i = 0; i < N_PLUGS; i++) {
    Plug& p = plugs[i];
    p.online = readPlug(p);
    if (p.online) {
      total += p.power_w;
      Serial.printf("%-8s %7.1f W  %5.1f V  %5.3f A  %8.3f kWh\n",
                    p.name, p.power_w, p.voltage_v, p.current_a, p.total_kwh);
    } else {
      Serial.printf("%-8s no response (%s): %s\n", p.name, p.ip, lastErr.c_str());
    }
  }
  Serial.printf("TOTAL    %7.1f W\n\n", total);
#if USE_OLED
  drawOled(total);
#endif
}
