// ESP32 energy monitor for TP-Link Kasa KP125 smart plugs
// Polls each plug locally (TCP 9999) and prints power, voltage, current, total kWh.
// Optional: shows live watts on a 128x64 SSD1306 OLED (set USE_OLED 1).
//
// Libraries (Arduino Library Manager):
//   - ArduinoJson (v7)
//   - Adafruit SSD1306 + Adafruit GFX   (only if USE_OLED = 1)
// Board: any ESP32 (Arduino-ESP32 core 2.x or 3.x)

#include <WiFi.h>
#include <ArduinoJson.h>
#include <vector>

#define USE_OLED 0

#if USE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#endif

// ---------- your settings ----------
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

struct Plug {
  const char* name;   // label for display
  const char* ip;     // give each plug a DHCP reservation
  float power_w = NAN, voltage_v = NAN, current_a = NAN, total_kwh = NAN;
  bool online = false;
};

Plug plugs[] = {
  {"Desk",   "192.168.1.50"},
  {"Fridge", "192.168.1.51"},
  // add more...
};
const size_t N_PLUGS = sizeof(plugs) / sizeof(plugs[0]);

const uint32_t POLL_MS = 5000;   // 2-10 s is plenty
// -----------------------------------

// Kasa "autokey" XOR obfuscation, initial key 171
static void kasaEncrypt(const uint8_t* in, uint8_t* out, size_t n) {
  uint8_t key = 171;
  for (size_t i = 0; i < n; i++) { out[i] = key ^ in[i]; key = out[i]; }
}
static void kasaDecrypt(uint8_t* buf, size_t n) {
  uint8_t key = 171;
  for (size_t i = 0; i < n; i++) { uint8_t c = buf[i]; buf[i] = key ^ c; key = c; }
}

// Send one JSON command, return decrypted JSON reply
bool kasaQuery(const char* ip, const char* cmd, String& reply) {
  WiFiClient c;
  c.setTimeout(3000);
  if (!c.connect(ip, 9999, 3000)) return false;

  size_t n = strlen(cmd);
  std::vector<uint8_t> out(4 + n);
  out[0] = n >> 24; out[1] = n >> 16; out[2] = n >> 8; out[3] = n;
  kasaEncrypt((const uint8_t*)cmd, out.data() + 4, n);
  c.write(out.data(), out.size());

  uint8_t hdr[4];
  if (c.readBytes(hdr, 4) != 4) return false;
  uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                 ((uint32_t)hdr[2] << 8) | hdr[3];
  if (len == 0 || len > 8192) return false;

  std::vector<uint8_t> in(len + 1);
  if (c.readBytes(in.data(), len) != len) return false;
  c.stop();
  kasaDecrypt(in.data(), len);
  in[len] = 0;
  reply = String((const char*)in.data());
  return true;
}

bool readPlug(Plug& p) {
  String reply;
  if (!kasaQuery(p.ip, "{\"emeter\":{\"get_realtime\":{}}}", reply)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, reply)) return false;
  JsonObject rt = doc["emeter"]["get_realtime"];
  if (rt.isNull() || rt["err_code"].as<int>() != 0) return false;

  // KP125 reports milli-units; older HS110 uses plain units, handled as fallback
  p.power_w   = rt["power_mw"].is<float>()   ? rt["power_mw"].as<float>() / 1000.0f   : rt["power"].as<float>();
  p.voltage_v = rt["voltage_mv"].is<float>() ? rt["voltage_mv"].as<float>() / 1000.0f : rt["voltage"].as<float>();
  p.current_a = rt["current_ma"].is<float>() ? rt["current_ma"].as<float>() / 1000.0f : rt["current"].as<float>();
  p.total_kwh = rt["total_wh"].is<float>()   ? rt["total_wh"].as<float>() / 1000.0f   : rt["total"].as<float>();
  return true;
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
}

void loop() {
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
      Serial.printf("%-8s no response (%s)\n", p.name, p.ip);
    }
  }
  Serial.printf("TOTAL    %7.1f W\n\n", total);
#if USE_OLED
  drawOled(total);
#endif
}
