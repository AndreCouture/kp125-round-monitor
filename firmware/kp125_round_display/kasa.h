// Kasa local protocols: legacy (TCP 9999, XOR autokey), KLAP (HTTP 80, AES-128-CBC) and
// TPAP (SPAKE2+, AES-128-CCM; see tpap.h). Identical copy in each sketch folder
// (Arduino can't include across sketches) - keep in sync.
//
// kasaReadEnergy() tries legacy first; if port 9999 refuses the connection it asks the plug
// which family it is and uses TPAP or KLAP from then on. KLAP and TPAP need KASA_USER /
// KASA_PASS (TP-Link account) in secrets.h; for KLAP, blank and factory-default credentials
// are tried as a fallback, as python-kasa does. TPAP also needs the clock set (SNTP).
//
// KLAP reference: python-kasa kasa/transports/klaptransport.py (KlapTransport = v1 md5 auth,
// KlapTransportV2 = sha256/sha1 auth; both are accepted, whichever the plug's hash matches).
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <esp_random.h>
#include <vector>
#include "tpap.h"

#ifndef KASA_USER
#define KASA_USER ""
#endif
#ifndef KASA_PASS
#define KASA_PASS ""
#endif

// ---------- legacy protocol ----------
// "autokey" XOR obfuscation, initial key 171
static void kasaEncrypt(const uint8_t* in, uint8_t* out, size_t n) {
  uint8_t key = 171;
  for (size_t i = 0; i < n; i++) { out[i] = key ^ in[i]; key = out[i]; }
}
static void kasaDecrypt(uint8_t* buf, size_t n) {
  uint8_t key = 171;
  for (size_t i = 0; i < n; i++) { uint8_t c = buf[i]; buf[i] = key ^ c; key = c; }
}

// Returns 1 on success, 0 on failure, -1 if the connection was refused (not a legacy plug)
static int legacyQuery(const char* ip, const char* cmd, String& reply, String& err) {
  WiFiClient c;
  c.setTimeout(3000);
  uint32_t t0 = millis();
  if (!c.connect(ip, 9999, 3000)) {
    // A fast failure is a refusal (host up, port closed); ~3 s is a timeout (no host)
    uint32_t ms = millis() - t0;
    err = String("port 9999 ") + (ms < 2500 ? "refused" : "timed out") + " after " + ms + " ms";
    return ms < 2500 ? -1 : 0;
  }
  size_t n = strlen(cmd);
  std::vector<uint8_t> out(4 + n);
  out[0] = n >> 24; out[1] = n >> 16; out[2] = n >> 8; out[3] = n;
  kasaEncrypt((const uint8_t*)cmd, out.data() + 4, n);
  c.write(out.data(), out.size());

  uint8_t hdr[4];
  if (c.readBytes(hdr, 4) != 4) { err = "connected, but no reply"; return 0; }
  uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
  if (len == 0 || len > 8192) { err = String("bad reply length ") + len; return 0; }
  std::vector<uint8_t> in(len + 1);
  if (c.readBytes(in.data(), len) != len) { err = "reply truncated"; return 0; }
  c.stop();
  kasaDecrypt(in.data(), len);
  in[len] = 0;
  reply = String((const char*)in.data());
  return 1;
}

// ---------- KLAP ----------
typedef std::vector<uint8_t> Bytes;

static Bytes klapHash(mbedtls_md_type_t t, const Bytes& in) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(t);
  Bytes out(mbedtls_md_get_size(md));
  mbedtls_md(md, in.data(), in.size(), out.data());
  return out;
}
static Bytes klapHash(mbedtls_md_type_t t, const char* s) {
  return klapHash(t, Bytes((const uint8_t*)s, (const uint8_t*)s + strlen(s)));
}
static Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}
static Bytes str(const char* s) { return Bytes((const uint8_t*)s, (const uint8_t*)s + strlen(s)); }

struct KlapSession {
  bool active = false;
  String cookie;              // "TP_SESSIONID=..."
  uint8_t key[16], iv[12], sig[28];
  int32_t seq = 0;
  uint32_t startMs = 0, lifeMs = 0;
};

// POST body to http://ip/path; fills resp with the raw body. Returns HTTP status (<0 = transport error).
static int klapPost(const char* ip, const String& path, const String& cookie, const Bytes& body,
                    Bytes& resp, String* setCookie = nullptr) {
  WiFiClient wc;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(wc, String("http://") + ip + path)) return -100;
  const char* keys[] = {"Set-Cookie"};
  http.collectHeaders(keys, 1);
  if (cookie.length()) http.addHeader("Cookie", cookie);
  http.addHeader("Content-Type", "application/octet-stream");
  int code = http.POST(const_cast<uint8_t*>(body.data()), body.size());
  if (code == 200) {
    int len = http.getSize();
    if (len > 0 && len <= 8192) {
      resp.resize(len);
      if (http.getStreamPtr()->readBytes(resp.data(), len) != (size_t)len) code = -101;
    } else code = -102;   // KLAP replies always carry a Content-Length
    if (setCookie) *setCookie = http.header("Set-Cookie");
  }
  http.end();
  return code;
}

// handshake1 + handshake2; on success fills s with the session keys
static bool klapHandshake(const char* ip, KlapSession& s, String& err) {
  s.active = false;
  Bytes localSeed(16);
  esp_fill_random(localSeed.data(), localSeed.size());

  Bytes resp; String setCookie;
  int code = klapPost(ip, "/app/handshake1", "", localSeed, resp, &setCookie);
  if (code != 200) { err = String("KLAP handshake1 HTTP ") + code; return false; }
  if (resp.size() != 48) { err = String("KLAP handshake1 reply is ") + resp.size() + " bytes, expected 48"; return false; }
  Bytes remoteSeed(resp.begin(), resp.begin() + 16), serverHash(resp.begin() + 16, resp.end());

  // Session cookie "TP_SESSIONID=xxx;TIMEOUT=86400"
  int semi = setCookie.indexOf(';');
  s.cookie = semi < 0 ? setCookie : setCookie.substring(0, semi);
  int t = setCookie.indexOf("TIMEOUT=");
  uint32_t timeoutS = t < 0 ? 86400 : setCookie.substring(t + 8).toInt();

  // Find which credentials (and KLAP version) the plug's hash was made with
  struct Cred { const char* u; const char* p; const char* label; };
  const Cred creds[] = {
    {KASA_USER, KASA_PASS, "account"},
    {"", "", "blank"},
    {"kasa@tp-link.net", "kasaSetup", "kasa default"},
  };
  Bytes authHash; int version = 0; const char* used = nullptr;
  for (auto& c : creds) {
    Bytes v1 = klapHash(MBEDTLS_MD_MD5, cat({klapHash(MBEDTLS_MD_MD5, c.u), klapHash(MBEDTLS_MD_MD5, c.p)}));
    if (klapHash(MBEDTLS_MD_SHA256, cat({localSeed, v1})) == serverHash) { authHash = v1; version = 1; used = c.label; break; }
    Bytes v2 = klapHash(MBEDTLS_MD_SHA256, cat({klapHash(MBEDTLS_MD_SHA1, c.u), klapHash(MBEDTLS_MD_SHA1, c.p)}));
    if (klapHash(MBEDTLS_MD_SHA256, cat({localSeed, remoteSeed, v2})) == serverHash) { authHash = v2; version = 2; used = c.label; break; }
  }
  if (!version) { err = "KLAP auth failed: plug doesn't match KASA_USER/KASA_PASS (check secrets.h)"; return false; }

  Bytes h2 = version == 1 ? klapHash(MBEDTLS_MD_SHA256, cat({remoteSeed, authHash}))
                          : klapHash(MBEDTLS_MD_SHA256, cat({remoteSeed, localSeed, authHash}));
  code = klapPost(ip, "/app/handshake2", s.cookie, h2, resp);
  if (code != 200) { err = String("KLAP handshake2 HTTP ") + code + " (" + used + " creds, v" + version + ")"; return false; }

  // Session keys, derived from local_seed + remote_seed + auth_hash
  Bytes lh = cat({localSeed, remoteSeed, authHash});
  Bytes k = klapHash(MBEDTLS_MD_SHA256, cat({str("lsk"), lh}));
  Bytes ivs = klapHash(MBEDTLS_MD_SHA256, cat({str("iv"), lh}));
  Bytes sg = klapHash(MBEDTLS_MD_SHA256, cat({str("ldk"), lh}));
  memcpy(s.key, k.data(), 16);
  memcpy(s.iv, ivs.data(), 12);
  s.seq = (int32_t)(((uint32_t)ivs[28] << 24) | ((uint32_t)ivs[29] << 16) | ((uint32_t)ivs[30] << 8) | ivs[31]);
  memcpy(s.sig, sg.data(), 28);
  s.startMs = millis();
  s.lifeMs = (timeoutS > 1200 ? timeoutS - 1200 : timeoutS / 2) * 1000UL;   // renew 20 min early
  s.active = true;
  Serial.printf("KLAP v%d session with %s (%s credentials)\n", version, ip, used);
  return true;
}

static void klapIv(const KlapSession& s, int32_t seq, uint8_t out[16]) {
  memcpy(out, s.iv, 12);
  out[12] = (uint32_t)seq >> 24; out[13] = (uint32_t)seq >> 16; out[14] = (uint32_t)seq >> 8; out[15] = seq;
}

static bool klapQuery(const char* ip, KlapSession& s, const char* cmd, String& reply, String& err) {
  if (s.active && millis() - s.startMs > s.lifeMs) s.active = false;
  if (!s.active && !klapHandshake(ip, s, err)) return false;

  // Encrypt: AES-128-CBC, PKCS#7, IV = iv[12] + seq; body = sha256(sig + seq + ct) + ct
  int32_t seq = ++s.seq;
  size_t n = strlen(cmd), pad = 16 - n % 16;
  Bytes pt((const uint8_t*)cmd, (const uint8_t*)cmd + n);
  pt.insert(pt.end(), pad, (uint8_t)pad);
  Bytes ct(pt.size());
  uint8_t iv[16];
  klapIv(s, seq, iv);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, s.key, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, pt.size(), iv, pt.data(), ct.data());
  Bytes seqB = {(uint8_t)((uint32_t)seq >> 24), (uint8_t)((uint32_t)seq >> 16), (uint8_t)((uint32_t)seq >> 8), (uint8_t)seq};
  Bytes body = cat({klapHash(MBEDTLS_MD_SHA256, cat({Bytes(s.sig, s.sig + 28), seqB, ct})), ct});

  Bytes resp;
  int code = klapPost(ip, String("/app/request?seq=") + seq, s.cookie, body, resp);
  if (code != 200) {
    mbedtls_aes_free(&aes);
    s.active = false;   // expired/rejected session: handshake again next time
    err = String("KLAP request HTTP ") + code;
    return false;
  }
  if (resp.size() < 48 || (resp.size() - 32) % 16) {
    mbedtls_aes_free(&aes);
    err = String("KLAP reply is ") + resp.size() + " bytes";
    return false;
  }

  // Decrypt reply (first 32 bytes are its signature)
  Bytes out(resp.size() - 32);
  klapIv(s, seq, iv);
  mbedtls_aes_setkey_dec(&aes, s.key, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, out.size(), iv, resp.data() + 32, out.data());
  mbedtls_aes_free(&aes);
  uint8_t p = out.back();
  if (p == 0 || p > 16) { s.active = false; err = "KLAP reply has bad padding (wrong session key?)"; return false; }
  reply = String((const char*)out.data(), out.size() - p);
  return true;
}

// ---------- public interface ----------
struct KasaConn {
  enum Proto : uint8_t { UNKNOWN, LEGACY, KLAP, TPAP } proto = UNKNOWN;
  bool smart = false;   // SMART JSON ({"method":...}) rather than legacy IOT ({"emeter":...})
  KlapSession klap;
  tpap::Session tpap;
};

// kwh is the plug's cumulative counter. KP125M: energy_mwh (1 mWh resolution), which is the
// month-to-date figure, so it drops back to 0 on the 1st. Legacy IOT plugs: total_wh.
struct KasaEnergy { float w = 0, v = 0, a = 0; double kwh = 0; };

static const char* IOT_ENERGY_CMD = "{\"emeter\":{\"get_realtime\":{}}}";
static const char* SMART_ENERGY_CMD = "{\"method\":\"get_emeter_data\"}";

// Asks a plug that refused port 9999 which family it is: 2 = SMART preferring TPAP,
// 1 = SMART (KLAP), 0 = not SMART (KLAP with legacy IOT requests)
static int smartFamily(const char* ip) {
  const char* req = "{\"method\":\"login\",\"params\":{\"sub_method\":\"discover\"}}";
  tpap::Bytes body;
  if (tpap::post(ip, 80, "/", "application/json", (const uint8_t*)req, strlen(req), body) != 200) return 0;
  JsonDocument d;
  if (deserializeJson(d, (const char*)body.data(), body.size()) || (d["error_code"] | -1) != 0) return 0;
  JsonObject r = d["result"];
  return (r["tpap_preferred"] | false) && r["tpap"].is<JsonObject>() ? 2 : 1;
}

static bool parseEnergy(const String& reply, bool smart, KasaEnergy& e, String& err) {
  JsonDocument doc;
  if (deserializeJson(doc, reply)) { err = "reply is not JSON"; return false; }
  JsonObject rt;
  int ec;
  if (smart) { rt = doc["result"]; ec = doc["error_code"] | -1; }
  else       { rt = doc["emeter"]["get_realtime"]; ec = rt["err_code"] | -1; }
  if (rt.isNull() || ec != 0) { err = "no energy data: " + reply.substring(0, 120); return false; }
  // KP125/KP125M report milli-units; older HS110 plain units, handled as fallback
  e.w   = rt["power_mw"].is<float>()   ? rt["power_mw"].as<float>() / 1000.0f   : rt["power"].as<float>();
  e.v   = rt["voltage_mv"].is<float>() ? rt["voltage_mv"].as<float>() / 1000.0f : rt["voltage"].as<float>();
  e.a   = rt["current_ma"].is<float>() ? rt["current_ma"].as<float>() / 1000.0f : rt["current"].as<float>();
  if (rt["energy_mwh"].is<double>())    e.kwh = rt["energy_mwh"].as<double>() / 1e6;
  else if (rt["total_wh"].is<double>()) e.kwh = rt["total_wh"].as<double>() / 1e3;
  else if (rt["energy_wh"].is<double>()) e.kwh = rt["energy_wh"].as<double>() / 1e3;
  else                                  e.kwh = rt["total"].as<double>();
  return true;
}

struct KasaUsage { float todayKwh = NAN, monthKwh = NAN; };

static const char* SMART_USAGE_CMD = "{\"method\":\"get_energy_usage\"}";

// Today's and this month's energy from the plug's own counters (SMART plugs only; call after
// kasaReadEnergy() has picked the protocol)
static bool kasaReadUsage(const char* ip, KasaConn& c, KasaUsage& u, String& err) {
  if (!c.smart || (c.proto != KasaConn::TPAP && c.proto != KasaConn::KLAP)) {
    err = "daily/monthly totals need a SMART plug";
    return false;
  }
  String reply;
  bool ok = c.proto == KasaConn::TPAP ? tpap::query(ip, KASA_USER, KASA_PASS, c.tpap, SMART_USAGE_CMD, reply, err)
                                      : klapQuery(ip, c.klap, SMART_USAGE_CMD, reply, err);
  if (!ok) return false;
  JsonDocument d;
  if (deserializeJson(d, reply) || (d["error_code"] | -1) != 0) { err = "no usage data: " + reply.substring(0, 120); return false; }
  JsonObject r = d["result"];
  u.todayKwh = r["today_energy"].is<float>() ? r["today_energy"].as<float>() / 1000.0f : NAN;   // Wh
  u.monthKwh = r["month_energy"].is<float>() ? r["month_energy"].as<float>() / 1000.0f : NAN;
  return true;
}

// Daily energy history (SMART plugs): get_energy_data with interval 1440 returns one Wh value per
// day for the whole quarter that starts at `start` (epoch of local midnight, 1st day of the quarter;
// verified on KP125M fw 1.4.1), future days 0. Fills wh with that array.
static bool kasaReadDaily(const char* ip, KasaConn& c, time_t start, time_t end, std::vector<float>& wh,
                          String& err) {
  if (!c.smart || (c.proto != KasaConn::TPAP && c.proto != KasaConn::KLAP)) {
    err = "daily history needs a SMART plug";
    return false;
  }
  char cmd[160];
  snprintf(cmd, sizeof(cmd),
           "{\"method\":\"get_energy_data\",\"params\":{\"start_timestamp\":%lld,\"end_timestamp\":%lld,\"interval\":1440}}",
           (long long)start, (long long)end);
  String reply;
  bool ok = c.proto == KasaConn::TPAP ? tpap::query(ip, KASA_USER, KASA_PASS, c.tpap, cmd, reply, err)
                                      : klapQuery(ip, c.klap, cmd, reply, err);
  if (!ok) return false;
  JsonDocument d;
  if (deserializeJson(d, reply) || (d["error_code"] | -1) != 0) { err = "no history: " + reply.substring(0, 120); return false; }
  JsonObject r = d["result"];
  JsonArray data = r["data"];
  if (data.isNull() || data.size() > 100 || (r["interval"] | 0) != 1440 ||
      (long long)(r["start_timestamp"] | 0LL) != (long long)start) {
    err = "unexpected history reply";
    return false;
  }
  wh.clear();
  for (JsonVariant v : data) wh.push_back(v.as<float>());
  return true;
}

// Send any JSON request over the plug's protocol, which an earlier successful kasaReadEnergy()
// picked. Development aid (kp125_monitor's serial console); not used by normal polling.
static bool kasaRawQuery(const char* ip, KasaConn& c, const char* json, String& reply, String& err) {
  switch (c.proto) {
    case KasaConn::LEGACY: return legacyQuery(ip, json, reply, err) == 1;
    case KasaConn::KLAP:   return klapQuery(ip, c.klap, json, reply, err);
    case KasaConn::TPAP:   return tpap::query(ip, KASA_USER, KASA_PASS, c.tpap, json, reply, err);
    default:               err = "protocol not known yet (no successful poll)"; return false;
  }
}

// Read the plug's live power. The protocol is picked on first contact and kept:
// legacy (TCP 9999) -> if refused, TPAP when the plug prefers it, otherwise KLAP.
static bool kasaReadEnergy(const char* ip, KasaConn& c, KasaEnergy& e, String& err) {
  String reply;
  if (c.proto == KasaConn::UNKNOWN) {
    int r = legacyQuery(ip, IOT_ENERGY_CMD, reply, err);
    if (r == 0) return false;                      // timeout or bad reply: try again next poll
    if (r == 1) c.proto = KasaConn::LEGACY;
    else {                                         // port 9999 refused
      int fam = smartFamily(ip);
      c.proto = fam == 2 ? KasaConn::TPAP : KasaConn::KLAP;
      c.smart = fam > 0;
      reply = "";
    }
  }
  bool ok = true;
  switch (c.proto) {
    case KasaConn::LEGACY:
      if (reply.isEmpty()) ok = legacyQuery(ip, IOT_ENERGY_CMD, reply, err) == 1;
      break;
    case KasaConn::KLAP:
      ok = klapQuery(ip, c.klap, c.smart ? SMART_ENERGY_CMD : IOT_ENERGY_CMD, reply, err);
      break;
    case KasaConn::TPAP:
      ok = tpap::query(ip, KASA_USER, KASA_PASS, c.tpap, SMART_ENERGY_CMD, reply, err);
      break;
    default:
      ok = false;
  }
  return ok && parseEnergy(reply, c.smart, e, err);
}
