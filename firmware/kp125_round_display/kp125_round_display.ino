// Live KP125 power monitor on the Waveshare ESP32-S3-Touch-LCD-1.28 (round 240x240 GC9A01)
//
// Screen: arc gauge of total power vs MAX_W, big total in the middle,
// "W left" headroom, then each plug cycles through with its own reading.
// Dots at the bottom show which plugs are online.
// Tap (or swipe) for more pages: TODAY (kWh, peak/low power) and MONTH (kWh, per plug);
// it returns to the gauge after PAGE_TIMEOUT_MS without a touch.
//
// Libraries (Arduino Library Manager):
//   - GFX Library for Arduino  (by moononournation, "Arduino_GFX")
//   - ArduinoJson v7
// Board settings: "ESP32S3 Dev Module", Flash 16MB, PSRAM "QSPI PSRAM".
// The board's USB goes through a CH343 serial chip, so USB CDC On Boot can stay disabled.

#include <WiFi.h>
#include <Wire.h>
#include <time.h>
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

const float    MAX_W        = 1440;   // gauge full-scale: 15 A x 120 V x 80% continuous-load limit
const uint32_t POLL_MS      = 3000;   // plug polling interval
const uint32_t CYCLE_MS     = 3000;   // how long each plug is shown
const uint32_t USAGE_MS     = 60000;  // how often to fetch today's/month's kWh from the plugs
const uint32_t PAGE_TIMEOUT_MS = 20000;   // back to the gauge after this long without a touch
// Local time zone (POSIX TZ) for the daily peak/low reset at midnight; plugs report America/Toronto
const char*    TZ_INFO      = "EST5EDT,M3.2.0,M11.1.0";
// ------------------------------------------------

// Waveshare ESP32-S3-Touch-LCD-1.28 pins (owner's board; RST/BL differ from the non-touch model)
#define LCD_DC   8
#define LCD_CS   9
#define LCD_SCK  10
#define LCD_MOSI 11
#define LCD_RST  14
#define LCD_BL   2
// CST816S touch controller (shares I2C with the IMU)
#define TP_SDA   6
#define TP_SCL   7
#define TP_RST   13
#define TP_ADDR  0x15

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
struct Reading {
  float w = 0, v = 0, a = 0, kwh = 0;
  float todayKwh = NAN, monthKwh = NAN;   // plug's own counters, refreshed every USAGE_MS
  bool online = false, seen = false;
};
Reading readings[N_PLUGS];

// Peak/low of the total power today, from polls where every plug answered.
// Kept in RAM only: a reboot starts a new tracking window ("since" time).
struct DayStats {
  float peakW = NAN, lowW = NAN;
  time_t peakAt = 0, lowAt = 0, since = 0;
  int yday = -1;   // local day of year these belong to
};
DayStats dayStats;
portMUX_TYPE readMux = portMUX_INITIALIZER_UNLOCKED;   // guards readings[] and dayStats
KasaConn conns[N_PLUGS];   // protocol + session per plug; poll task only
// Explicit prototype: stops the Arduino preprocessor emitting one above struct Reading
bool readPlug(const char* ip, KasaConn& conn, Reading& r, String& err);

bool readPlug(const char* ip, KasaConn& conn, Reading& r, String& err) {
  KasaEnergy e;
  if (!kasaReadEnergy(ip, conn, e, err)) return false;
  r.w = e.w; r.v = e.v; r.a = e.a; r.kwh = e.kwh;
  return true;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  // TPAP checks the plug's certificate dates (needs real time); TZ_INFO gives local midnight
  static bool sntp = false;
  if (WiFi.status() == WL_CONNECTED && !sntp) { configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com"); sntp = true; }
}

// Fold one complete poll's total into today's peak/low; resets at local midnight
void updateDayStats(float total) {
  time_t now = time(nullptr);
  if (!tpap::clockReady()) return;
  struct tm lt;
  localtime_r(&now, &lt);
  portENTER_CRITICAL(&readMux);
  DayStats& s = dayStats;
  if (s.yday != lt.tm_yday) { s = DayStats(); s.yday = lt.tm_yday; s.since = now; }
  if (isnan(s.peakW) || total > s.peakW) { s.peakW = total; s.peakAt = now; }
  if (isnan(s.lowW) || total < s.lowW)   { s.lowW = total;  s.lowAt = now; }
  portEXIT_CRITICAL(&readMux);
}

// Runs on core 0 so slow/offline plugs never freeze the display
void pollTask(void*) {
  uint32_t lastUsage = 0;
  bool usageDue = true;
  for (;;) {
    connectWiFi();
    float total = 0;
    bool allOnline = true;
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
      r.todayKwh = 1.23f * (i + 1); r.monthKwh = 34.5f * (i + 1);
#else
      r.online = (WiFi.status() == WL_CONNECTED) && readPlug(PLUGS[i].ip, conns[i], r, err);
      if (r.online && usageDue) {
        KasaUsage u;
        String uerr;
        if (kasaReadUsage(PLUGS[i].ip, conns[i], u, uerr)) { r.todayKwh = u.todayKwh; r.monthKwh = u.monthKwh; }
        else Serial.printf("%-8s usage: %s\n", PLUGS[i].name, uerr.c_str());
      }
#endif
      r.seen = true;
      portENTER_CRITICAL(&readMux);
      if (r.online) {
        if (isnan(r.todayKwh)) { r.todayKwh = readings[i].todayKwh; r.monthKwh = readings[i].monthKwh; }   // keep last known
        readings[i] = r;
      } else { readings[i].online = false; readings[i].seen = true; }
      portEXIT_CRITICAL(&readMux);
      if (r.online) Serial.printf("%-8s ok  %.1f W\n", PLUGS[i].name, r.w);
      else          Serial.printf("%-8s --  %s\n", PLUGS[i].name, err.c_str());
      total += r.online ? r.w : 0;
      allOnline &= r.online;
    }
    if (allOnline) updateDayStats(total);
    if (usageDue) lastUsage = millis();
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    usageDue = millis() - lastUsage >= USAGE_MS;
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

// ---------- touch (CST816S) ----------
// Same register read as the owner's Speedometer project: 0x01 = gesture, fingers, X hi/lo, Y hi/lo
bool touchRead(int16_t& x, int16_t& y) {
  uint8_t b[6];
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;   // chip asleep (no finger) NACKs
  if (Wire.requestFrom((int)TP_ADDR, 6) != 6) return false;
  for (auto& v : b) v = Wire.read();
  if (b[1] == 0 || b[1] == 0xFF) return false;
  x = ((b[2] & 0x0F) << 8) | b[3];
  y = ((b[4] & 0x0F) << 8) | b[5];
  return true;
}

// ---------- UI ----------
const float ARC_START = 135, ARC_SWEEP = 270;   // gauge opens at the bottom
enum Page { PAGE_GAUGE, PAGE_TODAY, PAGE_MONTH, N_PAGES };
int page = PAGE_GAUGE;
uint32_t lastTouch = 0;
float shownW = 0;                               // animated value
size_t cycleIdx = 0;
uint32_t lastCycle = 0;
Reading snap[N_PLUGS];                          // UI-side copies, refreshed every frame
DayStats statsSnap;

// Tap = next page; swipe left/right = next/previous
void handleTouch() {
  static bool down = false;
  static int16_t x0, y0, xl, yl;
  static uint32_t t0;
  int16_t x, y;
  if (touchRead(x, y)) {
    if (!down) { down = true; x0 = x; y0 = y; t0 = millis(); }
    xl = x; yl = y;
    return;
  }
  if (!down) return;
  down = false;                                  // finger lifted: classify the gesture
  int dx = xl - x0, dy = yl - y0;
  if (abs(dx) > 50 && abs(dx) > abs(dy)) page = (page + (dx < 0 ? 1 : N_PAGES - 1)) % N_PAGES;
  else if (abs(dx) < 25 && abs(dy) < 25 && millis() - t0 < 800) page = (page + 1) % N_PAGES;
  else return;
  lastTouch = millis();
}

void drawPageDots() {
  for (int i = 0; i < N_PAGES; i++)
    gfx->fillCircle(108 + i * 12, 224, 2, i == page ? COL_TEXT : COL_TRACK);
}

// "14:32" in local time
void hhmm(time_t t, char* buf, size_t n) {
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(buf, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

// Sum of the plugs' kWh counters; false if no plug has reported one yet
bool sumKwh(bool month, float& out) {
  out = 0;
  bool any = false;
  for (size_t i = 0; i < N_PLUGS; i++) {
    float v = month ? snap[i].monthKwh : snap[i].todayKwh;
    if (!isnan(v)) { out += v; any = true; }
  }
  return any;
}

void drawToday() {
  char buf[32], t[8];
  textCentered("TODAY", 44, 2, COL_DIM);
  const DayStats& s = statsSnap;
  if (s.since) {                                 // tracking began after midnight, e.g. after a reboot
    struct tm a;
    localtime_r(&s.since, &a);
    if (a.tm_hour || a.tm_min > 1) {
      hhmm(s.since, t, sizeof(t));
      snprintf(buf, sizeof(buf), "peak/low since %s", t);
      textCentered(buf, 64, 1, COL_DIM);
    }
  }
  float kwh;
  if (sumKwh(false, kwh)) snprintf(buf, sizeof(buf), "%.2f", kwh); else snprintf(buf, sizeof(buf), "--");
  textCentered(buf, 76, 5, COL_TEXT);            // 40 px tall
  textCentered("kWh", 120, 2, COL_DIM);
  if (isnan(s.peakW)) {
    textCentered("peak/low: waiting", 150, 1, COL_DIM);
  } else {
    snprintf(buf, sizeof(buf), "peak %.0f W", s.peakW);
    textCentered(buf, 144, 2, loadColor(s.peakW / MAX_W));
    hhmm(s.peakAt, t, sizeof(t)); snprintf(buf, sizeof(buf), "at %s", t);
    textCentered(buf, 162, 1, COL_DIM);
    snprintf(buf, sizeof(buf), "low %.0f W", s.lowW);
    textCentered(buf, 176, 2, COL_GREEN);
    hhmm(s.lowAt, t, sizeof(t)); snprintf(buf, sizeof(buf), "at %s", t);
    textCentered(buf, 194, 1, COL_DIM);
  }
}

void drawMonth() {
  static const char* MONTHS[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY",
                                 "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  char buf[32];
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  textCentered(tpap::clockReady() ? MONTHS[lt.tm_mon] : "MONTH", 44, 2, COL_DIM);
  float kwh;
  if (sumKwh(true, kwh)) snprintf(buf, sizeof(buf), "%.1f", kwh); else snprintf(buf, sizeof(buf), "--");
  textCentered(buf, 70, 5, COL_TEXT);
  textCentered("kWh", 114, 2, COL_DIM);
  // per-plug breakdown: big text for up to 3 plugs, small for up to 6
  bool big = N_PLUGS <= 3;
  int y = 140, step = big ? 22 : 12;
  for (size_t i = 0; i < N_PLUGS && i < 6; i++, y += step) {
    if (isnan(snap[i].monthKwh)) snprintf(buf, sizeof(buf), "%s --", PLUGS[i].name);
    else snprintf(buf, sizeof(buf), "%s %.1f", PLUGS[i].name, snap[i].monthKwh);
    textCentered(buf, y, big ? 2 : 1, COL_TEXT);
  }
}

void drawUI() {
  portENTER_CRITICAL(&readMux);
  memcpy(snap, readings, sizeof(snap));
  statsSnap = dayStats;
  portEXIT_CRITICAL(&readMux);

  if (page != PAGE_GAUGE && millis() - lastTouch > PAGE_TIMEOUT_MS) page = PAGE_GAUGE;

  float total = 0; int online = 0;
  for (size_t i = 0; i < N_PLUGS; i++) if (snap[i].online) { total += snap[i].w; online++; }

  shownW += (total - shownW) * 0.15f;           // smooth needle (keeps animating on other pages)
  float frac = constrain(shownW / MAX_W, 0.0f, 1.0f);
  uint16_t col = loadColor(frac);

  gfx->fillScreen(COL_BG);
  if (page == PAGE_TODAY || page == PAGE_MONTH) {
    if (page == PAGE_TODAY) drawToday(); else drawMonth();
    drawPageDots();
    gfx->flush();
    return;
  }

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
  drawPageDots();

  gfx->flush();
}

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  if (!gfx->begin()) Serial.println("gfx->begin() failed (panel init or 115 KB framebuffer alloc)");
  gfx->fillScreen(COL_BG);
  gfx->flush();

  // Touch controller: reset pulse, then I2C (same bus as the IMU)
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(20);
  digitalWrite(TP_RST, HIGH);
  delay(60);
  Wire.begin(TP_SDA, TP_SCL);

  // 16 KB: TPAP's elliptic-curve and certificate code needs more than 8 KB
  xTaskCreatePinnedToCore(pollTask, "poll", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  handleTouch();
  drawUI();
  delay(33);   // ~30 fps
}
