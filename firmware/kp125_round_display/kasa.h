// Kasa local protocols: legacy (TCP 9999, XOR autokey) and KLAP (HTTP 80, AES-128-CBC).
// Identical copy in each sketch folder (Arduino can't include across sketches) - keep in sync.
//
// kasaRequest() tries legacy first; if port 9999 refuses the connection it switches that
// plug to KLAP and stays there. KLAP needs KASA_USER / KASA_PASS (TP-Link account) in
// secrets.h; blank and factory-default credentials are tried as a fallback, as python-kasa does.
//
// KLAP reference: python-kasa kasa/transports/klaptransport.py (KlapTransport = v1 md5 auth,
// KlapTransportV2 = sha256/sha1 auth; both are accepted, whichever the plug's hash matches).
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <esp_random.h>
#include <vector>

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
  enum Proto : uint8_t { UNKNOWN, LEGACY, KLAP } proto = UNKNOWN;
  KlapSession klap;
};

// Send one JSON command to the plug at ip, return its JSON reply. err says why on failure.
static bool kasaRequest(const char* ip, KasaConn& c, const char* cmd, String& reply, String& err) {
  if (c.proto == KasaConn::KLAP) return klapQuery(ip, c.klap, cmd, reply, err);
  int r = legacyQuery(ip, cmd, reply, err);
  if (r == 1) { c.proto = KasaConn::LEGACY; return true; }
  if (r == -1 && c.proto == KasaConn::UNKNOWN) {   // port 9999 refused: try KLAP
    String klapErr;
    if (klapQuery(ip, c.klap, cmd, reply, klapErr)) { c.proto = KasaConn::KLAP; return true; }
    err += "; " + klapErr;
  }
  return false;
}
