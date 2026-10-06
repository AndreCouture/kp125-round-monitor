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

// ---------- your settings ----------
struct Plug {
  const char* name;   // label for display
  const char* ip;     // give each plug a DHCP reservation
  float power_w = NAN, voltage_v = NAN, current_a = NAN, total_kwh = NAN;
  bool online = false;
  KasaConn conn;      // protocol (legacy/KLAP) and KLAP session, found on first contact
};

#ifdef PLUGS_INIT
Plug plugs[] = { PLUGS_INIT };
const size_t N_PLUGS = sizeof(plugs) / sizeof(plugs[0]);
#else
Plug plugs[1];                   // discovery-only mode: nothing to poll
const size_t N_PLUGS = 0;
#endif

const uint32_t POLL_MS = 5000;   // 2-10 s is plenty
// -----------------------------------

String lastErr;   // why the last readPlug() failed, for the serial log

bool readPlug(Plug& p) {
  String reply;
  if (!kasaRequest(p.ip, p.conn, "{\"emeter\":{\"get_realtime\":{}}}", reply, lastErr)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, reply)) { lastErr = "reply is not JSON"; return false; }
  JsonObject rt = doc["emeter"]["get_realtime"];
  if (rt.isNull() || rt["err_code"].as<int>() != 0) { lastErr = "no emeter data: " + reply.substring(0, 120); return false; }

  // KP125 reports milli-units; older HS110 uses plain units, handled as fallback
  p.power_w   = rt["power_mw"].is<float>()   ? rt["power_mw"].as<float>() / 1000.0f   : rt["power"].as<float>();
  p.voltage_v = rt["voltage_mv"].is<float>() ? rt["voltage_mv"].as<float>() / 1000.0f : rt["voltage"].as<float>();
  p.current_a = rt["current_ma"].is<float>() ? rt["current_ma"].as<float>() / 1000.0f : rt["current"].as<float>();
  p.total_kwh = rt["total_wh"].is<float>()   ? rt["total_wh"].as<float>() / 1000.0f   : rt["total"].as<float>();
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

void setup() {
  Serial.begin(115200);
#if USE_OLED
  Wire.begin();                         // default SDA 21, SCL 22
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay(); oled.display();
#endif
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) discoverPlugs();
}

void loop() {
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
