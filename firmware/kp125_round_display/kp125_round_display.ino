// Live KP125 power monitor on the Waveshare ESP32-S3-Touch-LCD-1.28 (round 240x240 GC9A01)
//
// Screen: arc gauge of total power vs MAX_W, big total in the middle,
// "W left" headroom, then each plug cycles through with its own reading.
// Dots at the bottom show which plugs are online.
//
// Libraries (Arduino Library Manager):
//   - GFX Library for Arduino  (by moononournation, "Arduino_GFX")
//   - ArduinoJson v7
// Board settings: "ESP32S3 Dev Module", Flash 16MB, PSRAM "QSPI PSRAM".
// The board's USB goes through a CH343 serial chip, so USB CDC On Boot can stay disabled.

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <vector>

// Wi-Fi credentials and plug list live in secrets.h (git-ignored)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and fill in your Wi-Fi and plug IPs"
#endif
#include "kasa.h"   // after secrets.h: uses KASA_USER / KASA_PASS

// 1 = fake readings, to check the screen layout without reachable plugs.
// Build with --build-property "compiler.cpp.extra_flags=-DDEMO_MODE=1" to avoid editing.
#ifndef DEMO_MODE
#define DEMO_MODE 0
#endif

// ---------------- your settings ----------------
struct PlugCfg { const char* name; const char* ip; };
const PlugCfg PLUGS[] = { PLUGS_INIT };   // up to ~10 fit the status dots
const size_t N_PLUGS = sizeof(PLUGS) / sizeof(PLUGS[0]);

const float    MAX_W        = 3000;   // full-scale of the gauge (e.g. your circuit limit)
const uint32_t POLL_MS      = 3000;   // plug polling interval
const uint32_t CYCLE_MS     = 3000;   // how long each plug is shown
// ------------------------------------------------

// Waveshare ESP32-S3-Touch-LCD-1.28 pins (owner's board; RST/BL differ from the non-touch model)
#define LCD_DC   8
#define LCD_CS   9
#define LCD_SCK  10
#define LCD_MOSI 11
#define LCD_RST  14
#define LCD_BL   2

Arduino_DataBus* bus   = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX*     panel = new Arduino_GC9A01(bus, LCD_RST, 0 /*rotation*/, true /*IPS*/);
Arduino_Canvas*  gfx   = new Arduino_Canvas(240, 240, panel);   // off-screen buffer = no flicker

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_BG    = rgb(0, 0, 0);
const uint16_t COL_TRACK = rgb(40, 40, 48);
const uint16_t COL_TEXT  = rgb(240, 240, 240);
const uint16_t COL_DIM   = rgb(130, 130, 140);
const uint16_t COL_GREEN = rgb(60, 210, 110);
const uint16_t COL_AMBER = rgb(250, 180, 40);
const uint16_t COL_RED   = rgb(240, 70, 60);

// ---------- shared readings (written by poll task, read by UI) ----------
struct Reading { float w = 0, v = 0, a = 0, kwh = 0; bool online = false; bool seen = false; };
Reading readings[N_PLUGS];
portMUX_TYPE readMux = portMUX_INITIALIZER_UNLOCKED;
KasaConn conns[N_PLUGS];   // protocol + KLAP session per plug; poll task only
// Explicit prototype: stops the Arduino preprocessor emitting one above struct Reading
bool readPlug(const char* ip, KasaConn& conn, Reading& r, String& err);

bool readPlug(const char* ip, KasaConn& conn, Reading& r, String& err) {
  String reply;
  if (!kasaRequest(ip, conn, "{\"emeter\":{\"get_realtime\":{}}}", reply, err)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, reply)) { err = "reply is not JSON"; return false; }
  JsonObject rt = doc["emeter"]["get_realtime"];
  if (rt.isNull() || rt["err_code"].as<int>() != 0) { err = "no emeter data: " + reply.substring(0, 120); return false; }
  r.w   = rt["power_mw"].as<float>()   / 1000.0f;
  r.v   = rt["voltage_mv"].as<float>() / 1000.0f;
  r.a   = rt["current_ma"].as<float>() / 1000.0f;
  r.kwh = rt["total_wh"].as<float>()   / 1000.0f;
  return true;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
}

// Runs on core 0 so slow/offline plugs never freeze the display
void pollTask(void*) {
  for (;;) {
    connectWiFi();
    for (size_t i = 0; i < N_PLUGS; i++) {
      Reading r;
      String err = "WiFi down";
#if DEMO_MODE
      // Fake readings for checking the layout: slow swing through all gauge colours,
      // last plug stays offline so the red dot/"offline" path shows too
      float t = millis() / 1000.0f;
      r.online = (i + 1 < N_PLUGS) || N_PLUGS == 1;
      r.w = max(0.0f, MAX_W / N_PLUGS * (0.5f + 0.5f * sinf(t / 8 + i)));
      r.v = 120; r.a = r.w / 120; r.kwh = 12.3f;
#else
      r.online = (WiFi.status() == WL_CONNECTED) && readPlug(PLUGS[i].ip, conns[i], r, err);
#endif
      r.seen = true;
      portENTER_CRITICAL(&readMux);
      if (r.online) readings[i] = r;
      else { readings[i].online = false; readings[i].seen = true; }
      portEXIT_CRITICAL(&readMux);
      if (r.online) Serial.printf("%-8s ok  %.1f W\n", PLUGS[i].name, r.w);
      else          Serial.printf("%-8s --  %s\n", PLUGS[i].name, err.c_str());
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

// ---------- drawing helpers ----------
// Thick arc, angles in degrees, 0 = 3 o'clock, clockwise
void drawArc(int cx, int cy, int r1, int r2, float a0, float a1, uint16_t col) {
  const float step = 2.0f;
  for (float a = a0; a < a1; a += step) {
    float b = min(a + step + 0.5f, a1);   // slight overlap avoids hairline gaps
    float ra = a * DEG_TO_RAD, rb = b * DEG_TO_RAD;
    int x0 = cx + lroundf(cosf(ra) * r1), y0 = cy + lroundf(sinf(ra) * r1);
    int x1 = cx + lroundf(cosf(ra) * r2), y1 = cy + lroundf(sinf(ra) * r2);
    int x2 = cx + lroundf(cosf(rb) * r1), y2 = cy + lroundf(sinf(rb) * r1);
    int x3 = cx + lroundf(cosf(rb) * r2), y3 = cy + lroundf(sinf(rb) * r2);
    gfx->fillTriangle(x0, y0, x1, y1, x2, y2, col);
    gfx->fillTriangle(x1, y1, x3, y3, x2, y2, col);
  }
  // rounded caps
  float rc = (r1 + r2) / 2.0f, cr = (r2 - r1) / 2.0f;
  gfx->fillCircle(cx + cosf(a0 * DEG_TO_RAD) * rc, cy + sinf(a0 * DEG_TO_RAD) * rc, cr, col);
  gfx->fillCircle(cx + cosf(a1 * DEG_TO_RAD) * rc, cy + sinf(a1 * DEG_TO_RAD) * rc, cr, col);
}

void textCentered(const char* s, int y, uint8_t size, uint16_t col) {
  int w = strlen(s) * 6 * size;          // built-in font is 6x8 per char
  gfx->setTextSize(size);
  gfx->setTextColor(col);
  gfx->setCursor(120 - w / 2, y);
  gfx->print(s);
}

uint16_t loadColor(float frac) {
  if (frac < 0.5f) return COL_GREEN;
  if (frac < 0.8f) return COL_AMBER;
  return COL_RED;
}

// ---------- UI ----------
const float ARC_START = 135, ARC_SWEEP = 270;   // gauge opens at the bottom
float shownW = 0;                               // animated value
size_t cycleIdx = 0;
uint32_t lastCycle = 0;

void drawUI() {
  Reading snap[N_PLUGS];
  portENTER_CRITICAL(&readMux);
  memcpy(snap, readings, sizeof(snap));
  portEXIT_CRITICAL(&readMux);

  float total = 0; int online = 0;
  for (size_t i = 0; i < N_PLUGS; i++) if (snap[i].online) { total += snap[i].w; online++; }

  shownW += (total - shownW) * 0.15f;           // smooth needle
  float frac = constrain(shownW / MAX_W, 0.0f, 1.0f);
  uint16_t col = loadColor(frac);

  gfx->fillScreen(COL_BG);

  // gauge
  drawArc(120, 120, 104, 116, ARC_START, ARC_START + ARC_SWEEP, COL_TRACK);
  if (frac > 0.005f) drawArc(120, 120, 104, 116, ARC_START, ARC_START + ARC_SWEEP * frac, col);

  char buf[32];
  if (WiFi.status() != WL_CONNECTED) {
    textCentered("WiFi...", 104, 3, COL_DIM);
    gfx->flush();
    return;
  }

  textCentered("TOTAL", 50, 2, COL_DIM);
  if (shownW >= 10000) snprintf(buf, sizeof(buf), "%.1fk", shownW / 1000);
  else                 snprintf(buf, sizeof(buf), "%.0f", shownW);
  textCentered(buf, 76, 6, COL_TEXT);            // big number, 48 px tall
  float left = MAX_W - total;
  if (left >= 0) snprintf(buf, sizeof(buf), "W  -  %.0f W left", left);
  else           snprintf(buf, sizeof(buf), "W  -  %.0f W over", -left);
  textCentered(buf, 130, 1, left >= 0 ? COL_DIM : COL_RED);

  // cycling plug line
  if (millis() - lastCycle > CYCLE_MS) { lastCycle = millis(); cycleIdx = (cycleIdx + 1) % N_PLUGS; }
  const Reading& p = snap[cycleIdx];
  textCentered(PLUGS[cycleIdx].name, 150, 2, COL_TEXT);
  if (p.online)       snprintf(buf, sizeof(buf), "%.1f W", p.w);
  else if (p.seen)    snprintf(buf, sizeof(buf), "offline");
  else                snprintf(buf, sizeof(buf), "...");
  textCentered(buf, 172, 2, p.online ? loadColor(p.w / MAX_W) : COL_RED);

  // status dots
  int spacing = 12, x0 = 120 - (int)(N_PLUGS - 1) * spacing / 2;
  for (size_t i = 0; i < N_PLUGS; i++) {
    uint16_t dc = !snap[i].seen ? COL_TRACK : (snap[i].online ? COL_GREEN : COL_RED);
    gfx->fillCircle(x0 + i * spacing, 204, i == cycleIdx ? 4 : 2, dc);
  }

  gfx->flush();
}

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  if (!gfx->begin()) Serial.println("gfx->begin() failed (panel init or 115 KB framebuffer alloc)");
  gfx->fillScreen(COL_BG);
  gfx->flush();

  xTaskCreatePinnedToCore(pollTask, "poll", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  drawUI();
  delay(33);   // ~30 fps
}
