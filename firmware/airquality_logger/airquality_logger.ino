// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 12113775 Canada Inc.

// ESP32 air-quality logger for the 3D printer room: Sensirion SEN55 (PM1.0/2.5/4.0/10,
// VOC index, NOx index, temperature, humidity) + optional SCD41 (CO2), both I2C, plus
// the wattage of the plug powering the printer (TPAP, reused from kp125_monitor's
// kasa.h/tpap.h) so every row carries "was it printing" for free.
//
// Each sample is appended to a CSV queue file on a microSD card, then the queue is
// flushed to a small HTTP receiver on the NAS (see tools/airquality-receiver/). Rows
// that fail to push stay queued and are retried next loop, so a Wi-Fi or NAS outage
// doesn't lose data — it just catches up once things are back.
//
// Libraries (Arduino Library Manager):
//   - ArduinoJson (v7)
//   - Sensirion I2C SEN5X   (github.com/Sensirion/arduino-i2c-sen5x)
//   - Sensirion I2C SCD4x   (github.com/Sensirion/arduino-i2c-scd4x)
//
// Reference build: Adafruit Feather ESP32 V2 (FQBN esp32:esp32:adafruit_feather_esp32_v2)
// + Adalogger FeatherWing (microSD, stacked on the Feather headers) + a Sensirion SEN55
// module on Adafruit's SEN54/SEN55 STEMMA QT adapter + an Adafruit SCD-41 STEMMA QT
// breakout, both daisy-chained off the Feather's STEMMA QT port with STEMMA QT cables.
// See docs/parts-airquality-logger.md for the full parts list and wiring notes.
//
// No wires to run for I2C or SD on that build — it's two STEMMA QT cables and the
// FeatherWing plugs straight onto the Feather's header pins. Wire.begin() with no
// arguments and SD.begin(SS) below pick up that board's own default pins (SDA 22 /
// SCL 20 for STEMMA QT, SS 33 for the Adalogger's microSD CS) automatically — on a
// different ESP32 board, override SD_CS_PIN below and check its own SDA/SCL defaults.

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2cScd4x.h>
#include <time.h>

// Wi-Fi, Kasa account, printer plug and NAS endpoint live in secrets.h (git-ignored)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and fill in your Wi-Fi, Kasa and NAS values"
#endif
#include "kasa.h"   // after secrets.h: uses KASA_USER / KASA_PASS

// ---------- your settings ----------
const uint32_t SAMPLE_MS = 15000;      // 10-30 s is plenty for this purpose
const uint32_t FLUSH_MS  = 20000;      // how often to try pushing the queue to the NAS
const int      SD_CS_PIN = SS;   // board's own default (33 on the Feather ESP32 V2 + Adalogger)
const char*    QUEUE_PATH = "/queue.csv";
const size_t   MAX_QUEUE_LINES_PER_FLUSH = 20;   // cap per flush so one tick can't stall on a huge backlog

const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.google.com";
// -----------------------------------

SensirionI2CSen5x sen5x;
SensirionI2cScd4x scd4x;
bool sen55Ok = false, scd41Ok = false, sdOk = false;

KasaConn printerConn;   // protocol (legacy/KLAP/TPAP) and session, found on first contact
String lastErr;

// Defined before any function: Arduino's auto-generated prototypes land above the first
// function in the file, so a type used in a later function's signature must already exist
// here (same reason SET_LOOP_TASK_STACK_SIZE sits below everything in kp125_monitor.ino).
struct Sample {
  float pm1_0 = NAN, pm2_5 = NAN, pm4_0 = NAN, pm10 = NAN;
  float voc = NAN, nox = NAN, temp_c = NAN, rh_pct = NAN;
  float co2_ppm = NAN;
  float printer_w = NAN;
  bool printer_online = false;
};

// ---------- Wi-Fi / time ----------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " ok " + WiFi.localIP().toString() : " failed");
  static bool sntp = false;
  if (WiFi.status() == WL_CONNECTED && !sntp) { configTime(0, 0, NTP_SERVER1, NTP_SERVER2); sntp = true; }
}

// ---------- sensors ----------
void initSensors() {
  Wire.begin();   // no args: uses this board's own default SDA/SCL (STEMMA QT port on the Feather V2)

  sen5x.begin(Wire);   // void: SEN5X begin() can't itself fail, only the I2C calls after it
  uint16_t err = sen5x.deviceReset();
  if (err == 0) err = sen5x.startMeasurement();
  sen55Ok = (err == 0);
  Serial.println(sen55Ok ? "SEN55 ok" : "SEN55 not found/failed to start — check wiring");

  scd4x.begin(Wire, SCD41_I2C_ADDR_62);
  int16_t serr = scd4x.wakeUp();
  serr = scd4x.stopPeriodicMeasurement();
  serr = scd4x.reinit();
  serr = scd4x.startPeriodicMeasurement();
  scd41Ok = (serr == 0);
  Serial.println(scd41Ok ? "SCD41 ok" : "SCD41 not found/failed to start — check wiring (CO2 column will be blank)");
}

// "Not ready yet" on the SCD41 just keeps the previous value in Sample, since the row
// still needs *a* number; a real I2C-level error leaves the field as NAN.
void readSen55(Sample& s) {
  if (!sen55Ok) return;
  uint16_t err = sen5x.readMeasuredValues(s.pm1_0, s.pm2_5, s.pm4_0, s.pm10, s.rh_pct, s.temp_c, s.voc, s.nox);
  if (err) { char m[128]; errorToString(err, m, sizeof m); Serial.printf("SEN55 read error: %s\n", m); }
}

void readScd41(Sample& s) {
  if (!scd41Ok) return;
  bool ready = false;
  if (scd4x.getDataReadyStatus(ready) != 0 || !ready) return;   // keep previous value this tick
  uint16_t co2 = 0; float t = 0, rh = 0;
  if (scd4x.readMeasurement(co2, t, rh) == 0) s.co2_ppm = co2;
}

void readPrinterPlug(Sample& s) {
  KasaEnergy e;
  s.printer_online = kasaReadEnergy(PRINTER_PLUG_IP, printerConn, e, lastErr);
  if (s.printer_online) s.printer_w = e.w;
  else Serial.printf("printer plug no response (%s): %s\n", PRINTER_PLUG_IP, lastErr.c_str());
}

// ---------- CSV queue on SD ----------
// One line per sample: ts,pm1_0,pm2_5,pm4_0,pm10,voc,nox,temp_c,rh_pct,co2_ppm,printer_w,printer_online
String toCsvLine(time_t ts, const Sample& s) {
  char buf[220];
  snprintf(buf, sizeof buf, "%lld,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.0f,%.1f,%d\n",
           (long long)ts, s.pm1_0, s.pm2_5, s.pm4_0, s.pm10, s.voc, s.nox,
           s.temp_c, s.rh_pct, s.co2_ppm, s.printer_w, s.printer_online ? 1 : 0);
  return String(buf);
}

void initSD() {
  sdOk = SD.begin(SD_CS_PIN);
  Serial.println(sdOk ? "SD card ok" : "SD card not found — rows that fail to push will be lost");
}

void queueLine(const String& line) {
  if (!sdOk) return;
  File f = SD.open(QUEUE_PATH, FILE_APPEND);
  if (!f) { Serial.println("SD: couldn't open queue file for append"); sdOk = false; return; }
  f.print(line);
  f.close();
}

// Push one CSV line to the NAS. Returns true on HTTP 2xx.
bool pushLine(const String& line) {
  // columns match toCsvLine() above
  long long ts; float pm1_0, pm2_5, pm4_0, pm10, voc, nox, temp_c, rh_pct, co2_ppm, printer_w;
  int printer_online;
  if (sscanf(line.c_str(), "%lld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d",
             &ts, &pm1_0, &pm2_5, &pm4_0, &pm10, &voc, &nox, &temp_c, &rh_pct,
             &co2_ppm, &printer_w, &printer_online) != 12) {
    Serial.println("queue: skipping malformed line");
    return true;   // drop it rather than get stuck retrying garbage forever
  }

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["ts"] = (long)ts;
  doc["pm1_0"] = pm1_0; doc["pm2_5"] = pm2_5; doc["pm4_0"] = pm4_0; doc["pm10"] = pm10;
  doc["voc_index"] = voc; doc["nox_index"] = nox;
  doc["temp_c"] = temp_c; doc["rh_pct"] = rh_pct;
  doc["co2_ppm"] = co2_ppm;
  doc["printer_w"] = printer_w; doc["printer_online"] = (bool)printer_online;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(NAS_INGEST_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", NAS_API_KEY);
  int code = http.POST(body);
  http.end();
  if (code < 200 || code >= 300) {
    Serial.printf("push failed: HTTP %d\n", code);
    return false;
  }
  return true;
}

// Flush up to MAX_QUEUE_LINES_PER_FLUSH lines: try to push each in order, stop at the
// first failure (keep ordering, don't skip ahead), rewrite the queue file with whatever
// is left. Simple and fine for a queue of this size; not built for huge backlogs.
void flushQueue() {
  if (!sdOk || WiFi.status() != WL_CONNECTED) return;
  if (!SD.exists(QUEUE_PATH)) return;

  File f = SD.open(QUEUE_PATH, FILE_READ);
  if (!f) return;
  std::vector<String> remaining;
  size_t tried = 0;
  bool stopPushing = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() == 0) continue;
    line += "\n";
    if (stopPushing || tried >= MAX_QUEUE_LINES_PER_FLUSH) { remaining.push_back(line); continue; }
    tried++;
    if (pushLine(line)) continue;              // sent ok, drop it
    stopPushing = true;                         // first failure: keep this and everything after
    remaining.push_back(line);
  }
  f.close();

  SD.remove(QUEUE_PATH);
  if (!remaining.empty()) {
    File out = SD.open(QUEUE_PATH, FILE_APPEND);
    if (out) { for (auto& l : remaining) out.print(l); out.close(); }
  }
  if (tried) Serial.printf("flush: tried %u queued line(s), %u remain\n", (unsigned)tried, (unsigned)remaining.size());
}

// TPAP's elliptic-curve and certificate code needs more than the default 8 KB loop stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  Serial.begin(115200);
  connectWiFi();
  for (int i = 0; i < 40 && !tpap::clockReady(); i++) { delay(250); connectWiFi(); }
  initSensors();
  initSD();
}

void loop() {
  static uint32_t lastSample = 0, lastFlush = 0;
  connectWiFi();

  uint32_t now = millis();
  if (lastSample == 0 || now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    Sample s;
    readSen55(s);
    readScd41(s);
    readPrinterPlug(s);

    time_t ts = time(nullptr);
    String line = toCsvLine(ts, s);
    Serial.print("sample: "); Serial.print(line);

    // Try a live push first so the common case (Wi-Fi/NAS up) has no SD round-trip;
    // only queue it if that fails.
    if (!pushLine(line)) queueLine(line);
  }

  if (now - lastFlush >= FLUSH_MS) {
    lastFlush = now;
    flushQueue();
  }
}
