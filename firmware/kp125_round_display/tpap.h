// TPAP client: TP-Link's SPAKE2+ local protocol, as spoken by KP125M plugs on firmware 1.4.x.
// Design, scope and security decisions: docs/tpap-design.md. Reference implementation:
// python-kasa PR #1592, kasa/transports/tpaptransport.py.
// Identical copy in each sketch folder (Arduino can't include across sketches) - keep in sync.
//
// Only the variant those plugs negotiate is supported (plain HTTP, pake 2, cipher suite 1 =
// P-256/SHA-256, AES-128-CCM, password_shadow passwd_id 2, DAC attestation). Anything else
// fails closed with an error naming the unsupported parameter.
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <time.h>
#include <vector>
#include <mbedtls/base64.h>
#include <mbedtls/ccm.h>
#include <mbedtls/ecp.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>

namespace tpap {

typedef std::vector<uint8_t> Bytes;

const size_t MAX_BODY = 16384;               // largest HTTP reply accepted (share reply ~3 KB)
const long MAX_ITERATIONS = 20000;           // PBKDF2 cap; plugs ask for 3000
const time_t CLOCK_VALID_AFTER = 1704067200; // 2024-01-01: SNTP has set the clock

// SPAKE2+ points M and N for cipher suite 1 (P-256), uncompressed SEC1
static const uint8_t M_POINT[65] = {
  0x04, 0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55, 0xba, 0x9d, 0xd7, 0x24, 0x25, 0x79, 0xf2,
  0x99, 0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95, 0xaf, 0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1,
  0x2f, 0x5f, 0xf3, 0x55, 0x16, 0x3e, 0x43, 0xce, 0x22, 0x4e, 0x0b, 0x0e, 0x65, 0xff, 0x02, 0xac,
  0x8e, 0x5c, 0x7b, 0xe0, 0x94, 0x19, 0xc7, 0x85, 0xe0, 0xca, 0x54, 0x7d, 0x55, 0xa1, 0x2e, 0x2d,
  0x20};
static const uint8_t N_POINT[65] = {
  0x04, 0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37, 0xb0, 0x4d, 0x99, 0x7f, 0x38, 0xc3, 0x77,
  0x07, 0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2, 0x4b, 0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b,
  0x49, 0x07, 0xd6, 0x0a, 0xa6, 0xbf, 0xad, 0xe4, 0x50, 0x08, 0xa6, 0x36, 0x33, 0x7f, 0x51, 0x68,
  0xc6, 0x4d, 0x9b, 0xd3, 0x60, 0x34, 0x80, 0x8c, 0xd5, 0x64, 0x49, 0x0b, 0x1e, 0x65, 0x6e, 0xdb,
  0xe7};

// TP-Link device root CA (python-kasa TPAP_ROOT_CA_PEM, SHA-256 of the PEM text e838b636...459015)
static const char ROOT_CA_PEM[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIICNzCCAdygAwIBAgIUNLD7w5j5WU/efCe8bqkfGSRGgLYwCgYIKoZIzj0EAwIw\n"
  "ezEnMCUGA1UEAwweVFAtTElOSyBTWVNURU1TIERFVklDRSBST09UIENBMR0wGwYD\n"
  "VQQKDBRUUC1MSU5LIFNZU1RFTVMgSU5DLjEPMA0GA1UEBwwGSXJ2aW5lMRMwEQYD\n"
  "VQQIDApDYWxpZm9ybmlhMQswCQYDVQQGEwJVUzAgFw0yNDExMjIwMjU3NDhaGA8y\n"
  "MDU0MTExNTAyNTc0OFowezEnMCUGA1UEAwweVFAtTElOSyBTWVNURU1TIERFVklD\n"
  "RSBST09UIENBMR0wGwYDVQQKDBRUUC1MSU5LIFNZU1RFTVMgSU5DLjEPMA0GA1UE\n"
  "BwwGSXJ2aW5lMRMwEQYDVQQIDApDYWxpZm9ybmlhMQswCQYDVQQGEwJVUzBZMBMG\n"
  "ByqGSM49AgEGCCqGSM49AwEHA0IABLwo8H9H6BoJDvcoewi4wPrPryVXir4z4yXV\n"
  "n29R5XCAcFfKk06pYPupG6pjaKOLKWXnaOdPZThDFxwGLo3urV2jPDA6MAsGA1Ud\n"
  "DwQEAwIBhjAMBgNVHRMEBTADAQH/MB0GA1UdDgQWBBRivfUtiHYsZBOKo80uZEwk\n"
  "XhBkdDAKBggqhkjOPQQDAgNJADBGAiEA+7j5jemtXcGYN0unH+9rjVhVAL7WrsOi\n"
  "5rbc0IIvD6MCIQCZuGGssu4Ygt2V8Vr0QF2fO9wxfNB3aRRMYQ+6lMrLGA==\n"
  "-----END CERTIFICATE-----\n";

struct Session {
  bool active = false;
  String stok;          // session token, part of the request URL
  uint16_t port = 80;
  uint8_t key[16];      // AES-128-CCM key
  uint8_t nonce[12];    // base nonce; last 4 bytes replaced by the sequence number
  uint32_t seq = 0;     // next request sequence number
};

static void dropSession(Session& s) {
  mbedtls_platform_zeroize(s.key, sizeof(s.key));
  mbedtls_platform_zeroize(s.nonce, sizeof(s.nonce));
  s.stok = "";
  s.seq = 0;
  s.active = false;
}

static void wipe(Bytes& b) {
  if (!b.empty()) mbedtls_platform_zeroize(b.data(), b.size());
  b.clear();
}
static void wipe(String& s) {
  if (s.length()) mbedtls_platform_zeroize(const_cast<char*>(s.c_str()), s.length());
  s = "";
}

static bool clockReady() { return time(nullptr) > CLOCK_VALID_AFTER; }

// Constant-time equality (mbedtls/constant_time.h has no extern "C" guard for C++)
static bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
  volatile uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

// Hardware TRNG (true random while the radio is on, which it is whenever we talk to a plug)
static int rng(void*, unsigned char* out, size_t n) {
  esp_fill_random(out, n);
  return 0;
}

// ---------- encoding helpers ----------
static String b64(const uint8_t* p, size_t n) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, p, n);   // olen = required size incl. NUL
  std::vector<unsigned char> out(olen);
  if (mbedtls_base64_encode(out.data(), out.size(), &olen, p, n)) return "";
  return String((const char*)out.data());
}

// Decodes s into out; false if empty, malformed or longer than maxLen bytes
static bool unb64(const char* s, Bytes& out, size_t maxLen) {
  size_t n = strlen(s), olen = 0;
  out.clear();
  if (n == 0 || n > 4 * ((maxLen + 2) / 3)) return false;
  out.resize(n);
  if (mbedtls_base64_decode(out.data(), out.size(), &olen, (const unsigned char*)s, n)) { out.clear(); return false; }
  out.resize(olen);
  return olen > 0 && olen <= maxLen;
}

static String hexOf(const uint8_t* p, size_t n, bool upper) {
  const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  String s;
  s.reserve(2 * n);
  for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}

static Bytes digest(mbedtls_md_type_t t, const uint8_t* p, size_t n) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(t);
  Bytes out(mbedtls_md_get_size(md));
  mbedtls_md(md, p, n, out.data());
  return out;
}
static String hexDigest(mbedtls_md_type_t t, const char* s, bool upper) {
  Bytes d = digest(t, (const uint8_t*)s, strlen(s));
  String h = hexOf(d.data(), d.size(), upper);
  wipe(d);
  return h;
}

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

// ---------- HTTP ----------
// POST to http://ip:port/path. Returns the HTTP status (negative = transport error);
// body gets the reply (at most MAX_BODY bytes) and ctype its Content-Type.
static int post(const char* ip, uint16_t port, const String& path, const char* ctypeOut,
                const uint8_t* data, size_t len, Bytes& body, String* ctype = nullptr) {
  WiFiClient wc;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  body.clear();
  if (!http.begin(wc, ip, port, path)) return -100;
  const char* keys[] = {"Content-Type"};
  http.collectHeaders(keys, 1);
  http.addHeader("Content-Type", ctypeOut);
  int code = http.POST(const_cast<uint8_t*>(data), len);
  if (code > 0) {
    int n = http.getSize();
    if (n < 0 || (size_t)n > MAX_BODY) code = -102;   // the plug always sends Content-Length
    else if (n > 0) {
      body.resize(n);
      if (http.getStreamPtr()->readBytes(body.data(), n) != (size_t)n) code = -101;
    }
    if (ctype) *ctype = http.header("Content-Type");
  }
  http.end();
  return code;
}

// One login step: POST {"method":"login","params":...} to "/". On success reply["result"] is an object.
static bool login(const char* ip, uint16_t port, JsonDocument& req, JsonDocument& reply,
                  const char* step, String& err) {
  String json;
  serializeJson(req, json);
  Bytes body;
  int code = post(ip, port, "/", "application/json", (const uint8_t*)json.c_str(), json.length(), body);
  if (code != 200) { err = String("TPAP ") + step + " HTTP " + code; return false; }
  reply.clear();
  if (deserializeJson(reply, (const char*)body.data(), body.size())) {
    err = String("TPAP ") + step + " reply is not JSON";
    return false;
  }
  int ec = reply["error_code"] | -1;
  if (ec != 0) { err = String("TPAP ") + step + " error_code " + ec; return false; }
  if (!reply["result"].is<JsonObject>()) { err = String("TPAP ") + step + " reply has no result"; return false; }
  return true;
}

// ---------- SPAKE2+ (client side, cipher suite 1) ----------
struct ShareOut {
  uint8_t L[65];             // user_share
  uint8_t userConfirm[32];
  uint8_t devConfirm[32];    // what the plug must answer
  uint8_t sharedKey[32];
  void wipe() { mbedtls_platform_zeroize(this, sizeof(*this)); }
};

static void len8le(Bytes& t, const uint8_t* p, size_t n) {
  for (int i = 0; i < 8; i++) t.push_back((uint8_t)((uint64_t)n >> (8 * i)));
  t.insert(t.end(), p, p + n);
}

// python-kasa _encode_w: minimal big-endian; odd lengths get a 0x00 prefix when the top bit is set
static Bytes encodeW(const mbedtls_mpi& w) {
  size_t n = mbedtls_mpi_size(&w);
  if (n == 0) n = 1;
  Bytes out(n);
  mbedtls_mpi_write_binary(&w, out.data(), n);
  if (n % 2 == 1 && (out[0] & 0x80)) out.insert(out.begin(), 0);
  return out;
}

static int hkdf256(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen,
                   const char* info, uint8_t* out, size_t outLen) {
  return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, saltLen, ikm, ikmLen,
                      (const uint8_t*)info, strlen(info), out, outLen);
}

static int writePoint(const mbedtls_ecp_group& g, const mbedtls_ecp_point& P, uint8_t out[65]) {
  size_t olen = 0;
  int rc = mbedtls_ecp_point_write_binary(&g, &P, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, out, 65);
  return rc ? rc : (olen == 65 ? 0 : -1);
}

// Computes L, the confirmations and the shared key. xFixed is only for the known-answer test.
// Secret scalars (w0, w1, x) are only ever used with the constant-time mbedtls_ecp_mul;
// mbedtls_ecp_muladd is used with scalar 1 purely to add points.
static bool computeShare(const Bytes& cred, const Bytes& salt, uint32_t iterations,
                         const uint8_t userRandom[32], const Bytes& devRandom, const Bytes& devShare,
                         const mbedtls_mpi* xFixed, ShareOut& out, String& err) {
  mbedtls_ecp_group g;
  mbedtls_ecp_point M, N, R, L, P1, P2, T, Rp, Z, V;
  mbedtls_mpi w0, w1, x, one;
  mbedtls_ecp_point* pts[] = {&M, &N, &R, &L, &P1, &P2, &T, &Rp, &Z, &V};
  mbedtls_mpi* mpis[] = {&w0, &w1, &x, &one};
  mbedtls_ecp_group_init(&g);
  for (auto p : pts) mbedtls_ecp_point_init(p);
  for (auto m : mpis) mbedtls_mpi_init(m);

  uint8_t derived[80], encR[65], encZ[65], encV[65], encM[65], encN[65], ctx[32], tt[32], kc[64];
  Bytes transcript, w0enc;
  const mbedtls_md_info_t* sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  bool ok = false;
  int rc = 0;
  do {
    if ((rc = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1))) break;
    if ((rc = mbedtls_ecp_point_read_binary(&g, &M, M_POINT, sizeof(M_POINT)))) break;
    if ((rc = mbedtls_ecp_point_read_binary(&g, &N, N_POINT, sizeof(N_POINT)))) break;

    // w0 || w1 = PBKDF2-SHA256(cred, salt, iterations, 80), each half reduced mod n
    if ((rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, cred.data(), cred.size(), salt.data(),
                                            salt.size(), iterations, sizeof(derived), derived))) break;
    if ((rc = mbedtls_mpi_read_binary(&w0, derived, 40))) break;
    if ((rc = mbedtls_mpi_mod_mpi(&w0, &w0, &g.N))) break;
    if ((rc = mbedtls_mpi_read_binary(&w1, derived + 40, 40))) break;
    if ((rc = mbedtls_mpi_mod_mpi(&w1, &w1, &g.N))) break;

    if (xFixed) rc = mbedtls_mpi_copy(&x, xFixed);
    else rc = mbedtls_ecp_gen_privkey(&g, &x, rng, nullptr);    // uniform in [1, n-1]
    if (rc) break;
    if ((rc = mbedtls_mpi_lset(&one, 1))) break;

    // L = x*G + w0*M
    if ((rc = mbedtls_ecp_mul(&g, &P1, &x, &g.G, rng, nullptr))) break;
    if ((rc = mbedtls_ecp_mul(&g, &P2, &w0, &M, rng, nullptr))) break;
    if ((rc = mbedtls_ecp_muladd(&g, &L, &one, &P1, &one, &P2))) break;

    // R = device share; must be a valid curve point
    if (devShare.empty() || mbedtls_ecp_point_read_binary(&g, &R, devShare.data(), devShare.size()) ||
        mbedtls_ecp_check_pubkey(&g, &R)) {
      err = "TPAP: device share is not a valid P-256 point";
      break;
    }

    // R' = R - w0*N, must not be the point at infinity
    if ((rc = mbedtls_ecp_mul(&g, &T, &w0, &N, rng, nullptr))) break;
    if ((rc = mbedtls_mpi_sub_mpi(&T.MBEDTLS_PRIVATE(Y), &g.P, &T.MBEDTLS_PRIVATE(Y)))) break;   // negate
    if ((rc = mbedtls_ecp_muladd(&g, &Rp, &one, &R, &one, &T))) break;
    if (mbedtls_ecp_is_zero(&Rp)) { err = "TPAP: degenerate device share"; break; }

    // Z = x*R', V = w1*R'
    if ((rc = mbedtls_ecp_mul(&g, &Z, &x, &Rp, rng, nullptr))) break;
    if ((rc = mbedtls_ecp_mul(&g, &V, &w1, &Rp, rng, nullptr))) break;

    if ((rc = writePoint(g, L, out.L)) || (rc = writePoint(g, R, encR)) || (rc = writePoint(g, Z, encZ)) ||
        (rc = writePoint(g, V, encV)) || (rc = writePoint(g, M, encM)) || (rc = writePoint(g, N, encN))) break;

    // context = SHA256("PAKE V1" || user_random || dev_random)
    Bytes c = {'P', 'A', 'K', 'E', ' ', 'V', '1'};
    c.insert(c.end(), userRandom, userRandom + 32);
    c.insert(c.end(), devRandom.begin(), devRandom.end());
    mbedtls_md(sha, c.data(), c.size(), ctx);

    w0enc = encodeW(w0);
    len8le(transcript, ctx, 32);
    len8le(transcript, nullptr, 0);
    len8le(transcript, nullptr, 0);
    len8le(transcript, encM, 65);
    len8le(transcript, encN, 65);
    len8le(transcript, out.L, 65);
    len8le(transcript, encR, 65);
    len8le(transcript, encZ, 65);
    len8le(transcript, encV, 65);
    len8le(transcript, w0enc.data(), w0enc.size());
    mbedtls_md(sha, transcript.data(), transcript.size(), tt);

    const uint8_t zeroSalt[32] = {0};
    if ((rc = hkdf256(zeroSalt, 32, tt, 32, "ConfirmationKeys", kc, 64))) break;
    if ((rc = hkdf256(zeroSalt, 32, tt, 32, "SharedKey", out.sharedKey, 32))) break;
    if ((rc = mbedtls_md_hmac(sha, kc, 32, encR, 65, out.userConfirm))) break;
    if ((rc = mbedtls_md_hmac(sha, kc + 32, 32, out.L, 65, out.devConfirm))) break;
    ok = true;
  } while (0);

  if (!ok && err.isEmpty()) err = String("TPAP SPAKE2+ mbedtls error -0x") + String(-rc, HEX);
  mbedtls_platform_zeroize(derived, sizeof(derived));
  mbedtls_platform_zeroize(encZ, sizeof(encZ));
  mbedtls_platform_zeroize(encV, sizeof(encV));
  mbedtls_platform_zeroize(tt, sizeof(tt));
  mbedtls_platform_zeroize(kc, sizeof(kc));
  wipe(transcript);
  wipe(w0enc);
  for (auto m : mpis) mbedtls_mpi_free(m);        // mbedtls zeroizes MPIs on free
  for (auto p : pts) mbedtls_ecp_point_free(p);
  mbedtls_ecp_group_free(&g);
  if (!ok) out.wipe();
  return ok;
}

// ---------- session ----------
static bool deriveSession(const uint8_t sharedKey[32], Session& s) {
  static const char ks[] = "tp-kdf-salt-aes128-key", ns[] = "tp-kdf-salt-aes128-iv";
  return hkdf256((const uint8_t*)ks, strlen(ks), sharedKey, 32, "tp-kdf-info-aes128-key", s.key, 16) == 0 &&
         hkdf256((const uint8_t*)ns, strlen(ns), sharedKey, 32, "tp-kdf-info-aes128-iv", s.nonce, 12) == 0;
}

static void seqNonce(const Session& s, uint32_t seq, uint8_t n[12]) {
  memcpy(n, s.nonce, 8);
  be32(n + 8, seq);
}

// out = be32(seq) || AES-128-CCM(plaintext) || tag16
static bool seal(const Session& s, uint32_t seq, const uint8_t* pt, size_t n, Bytes& out) {
  uint8_t nonce[12];
  seqNonce(s, seq, nonce);
  out.assign(4 + n + 16, 0);
  be32(out.data(), seq);
  mbedtls_ccm_context c;
  mbedtls_ccm_init(&c);
  bool ok = mbedtls_ccm_setkey(&c, MBEDTLS_CIPHER_ID_AES, s.key, 128) == 0 &&
            mbedtls_ccm_encrypt_and_tag(&c, n, nonce, 12, nullptr, 0, pt, out.data() + 4,
                                        out.data() + 4 + n, 16) == 0;
  mbedtls_ccm_free(&c);
  return ok;
}

// Inverse of seal(); fails if the reply's sequence number is not `seq` or the tag doesn't verify
static bool unseal(const Session& s, uint32_t seq, const Bytes& in, Bytes& pt, String& err) {
  if (in.size() < 4 + 16) { err = "TPAP reply too short"; return false; }
  uint32_t rseq = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
  if (rseq != seq) { err = String("TPAP reply sequence ") + rseq + " != " + seq; return false; }
  size_t n = in.size() - 4 - 16;
  uint8_t nonce[12];
  seqNonce(s, seq, nonce);
  pt.assign(n, 0);
  mbedtls_ccm_context c;
  mbedtls_ccm_init(&c);
  bool ok = mbedtls_ccm_setkey(&c, MBEDTLS_CIPHER_ID_AES, s.key, 128) == 0 &&
            mbedtls_ccm_auth_decrypt(&c, n, nonce, 12, nullptr, 0, in.data() + 4, pt.data(),
                                     in.data() + 4 + n, 16) == 0;
  mbedtls_ccm_free(&c);
  if (!ok) { wipe(pt); err = "TPAP reply failed authentication"; }
  return ok;
}

// ---------- DAC attestation ----------
// Certificates arrive as PEM, base64 DER or base64-encoded PEM (python-kasa _load_certificate_value
// base64-decodes first, then accepts PEM or DER)
static bool loadCert(const char* value, mbedtls_x509_crt& crt) {
  Bytes raw;
  if (unb64(value, raw, 4096)) {
    if (raw.size() >= 10 && memcmp(raw.data(), "-----BEGIN", 10) == 0) {
      raw.push_back(0);   // mbedtls PEM parsing needs the terminating NUL counted in the length
      return mbedtls_x509_crt_parse(&crt, raw.data(), raw.size()) == 0;
    }
    return mbedtls_x509_crt_parse_der(&crt, raw.data(), raw.size()) == 0;
  }
  if (strncmp(value, "-----BEGIN", 10) == 0)
    return mbedtls_x509_crt_parse(&crt, (const uint8_t*)value, strlen(value) + 1) == 0;
  return false;
}

static bool inValidity(const mbedtls_x509_crt& c) {
  return !mbedtls_x509_time_is_past(&c.valid_to) && !mbedtls_x509_time_is_future(&c.valid_from);
}

static bool signedBy(const mbedtls_x509_crt& child, mbedtls_x509_crt& issuer) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(child.MBEDTLS_PRIVATE(sig_md));
  if (!md) return false;
  uint8_t h[MBEDTLS_MD_MAX_SIZE];
  if (mbedtls_md(md, child.tbs.p, child.tbs.len, h)) return false;
  return mbedtls_pk_verify_ext(child.MBEDTLS_PRIVATE(sig_pk), child.MBEDTLS_PRIVATE(sig_opts), &issuer.pk,
                               child.MBEDTLS_PRIVATE(sig_md), h, mbedtls_md_get_size(md),
                               child.MBEDTLS_PRIVATE(sig).p, child.MBEDTLS_PRIVATE(sig).len) == 0;
}

// dac_ica signed by the TP-Link root (dac_ca directly if there is no ICA), dac_ca by dac_ica,
// both within validity, and dac_proof = ECDSA-SHA256 by dac_ca over sharedKey || dacNonce.
static bool verifyDac(const char* caStr, const char* icaStr, const char* proofB64,
                      const uint8_t sharedKey[32], const uint8_t dacNonce[32], String& err) {
  mbedtls_x509_crt root, ca, ica;
  mbedtls_x509_crt_init(&root);
  mbedtls_x509_crt_init(&ca);
  mbedtls_x509_crt_init(&ica);
  bool haveIca = *icaStr != 0, ok = false;
  Bytes proof;
  do {
    if (mbedtls_x509_crt_parse(&root, (const uint8_t*)ROOT_CA_PEM, sizeof(ROOT_CA_PEM))) { err = "TPAP: bad embedded root CA"; break; }
    if (!*caStr || !loadCert(caStr, ca)) { err = "TPAP: missing/invalid dac_ca"; break; }
    if (haveIca && !loadCert(icaStr, ica)) { err = "TPAP: invalid dac_ica"; break; }
    if (!inValidity(ca) || (haveIca && !inValidity(ica))) { err = "TPAP: DAC certificate outside its validity period"; break; }
    if (haveIca ? !(signedBy(ca, ica) && signedBy(ica, root)) : !signedBy(ca, root)) {
      err = "TPAP: DAC chain not signed by the TP-Link root";
      break;
    }
    if (mbedtls_pk_get_type(&ca.pk) != MBEDTLS_PK_ECKEY) { err = "TPAP: DAC key is not EC"; break; }
    if (!unb64(proofB64, proof, 256)) { err = "TPAP: missing/invalid dac_proof"; break; }
    uint8_t msg[64], h[32];
    memcpy(msg, sharedKey, 32);
    memcpy(msg + 32, dacNonce, 32);
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), msg, sizeof(msg), h);
    mbedtls_platform_zeroize(msg, sizeof(msg));
    if (mbedtls_pk_verify(&ca.pk, MBEDTLS_MD_SHA256, h, 32, proof.data(), proof.size())) {
      err = "TPAP: DAC proof signature invalid";
      break;
    }
    ok = true;
  } while (0);
  mbedtls_x509_crt_free(&root);
  mbedtls_x509_crt_free(&ca);
  mbedtls_x509_crt_free(&ica);
  return ok;
}

// ---------- handshake ----------
static bool okStok(const String& s) {
  if (s.isEmpty() || s.length() > 128) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
  }
  return true;
}

// Full login: discover -> pake_register -> pake_share (+ DAC check). Fills s on success.
static bool handshake(const char* ip, const char* user, const char* pass, Session& s, String& err) {
  dropSession(s);
  if (!*pass) { err = "TPAP needs KASA_PASS in secrets.h"; return false; }
  if (!clockReady()) { err = "TPAP waiting for NTP time (needed to check the plug's certificates)"; return false; }

  // 1. discover
  JsonDocument req, rep;
  req["method"] = "login";
  req["params"]["sub_method"] = "discover";
  if (!login(ip, 80, req, rep, "discover", err)) return false;
  JsonObject tp = rep["result"]["tpap"];
  if (tp.isNull()) { err = "TPAP discover: plug did not offer TPAP"; return false; }
  int tls = tp["tls"] | -1;
  if (tls != 0) { err = String("TPAP: TLS mode ") + tls + " not supported"; return false; }
  JsonVariant dac = tp["dac"];
  if (!(dac.is<bool>() ? dac.as<bool>() : (dac | 0) != 0)) { err = "TPAP: plug offers no DAC attestation; refusing"; return false; }
  bool pake2 = false;
  for (JsonVariant v : tp["pake"].as<JsonArray>()) pake2 |= (v.as<int>() == 2);
  if (!pake2) { err = "TPAP: plug does not offer password PAKE (pake 2)"; return false; }
  uint16_t port = tp["port"] | 80;
  if (port == 0) port = 80;
  String username = (tp["user_hash_type"] | -1) == 1 ? hexDigest(MBEDTLS_MD_SHA256, "admin", true)
                                                     : hexDigest(MBEDTLS_MD_MD5, "admin", false);

  // 2. pake_register
  uint8_t userRandom[32];
  rng(nullptr, userRandom, sizeof(userRandom));
  req.clear();
  req["method"] = "login";
  JsonObject p = req["params"].to<JsonObject>();
  p["sub_method"] = "pake_register";
  p["username"] = username;
  p["user_random"] = b64(userRandom, 32);
  p["cipher_suites"].to<JsonArray>().add(1);
  p["encryption"].to<JsonArray>().add("aes_128_ccm");
  p["passcode_type"] = "userpw";
  p["stok"] = nullptr;
  if (!login(ip, port, req, rep, "pake_register", err)) return false;
  JsonObject res = rep["result"];
  int suite = res["cipher_suites"] | -1;
  if (suite != 1) { err = String("TPAP: cipher suite ") + suite + " not supported"; return false; }
  String enc = res["encryption"] | "";
  enc.toLowerCase();
  enc.replace("-", "_");
  if (enc != "aes_128_ccm") { err = "TPAP: session cipher " + enc + " not supported"; return false; }
  long iterations = res["iterations"] | 0L;
  if (iterations < 1 || iterations > MAX_ITERATIONS) { err = String("TPAP: iterations ") + iterations + " out of range"; return false; }
  Bytes devRandom, devSalt, devShare;
  if (!unb64(res["dev_random"] | "", devRandom, 64) || !unb64(res["dev_salt"] | "", devSalt, 64) ||
      !unb64(res["dev_share"] | "", devShare, 65)) {
    err = "TPAP: register reply missing/invalid dev_random, dev_salt or dev_share";
    return false;
  }

  // Password processing the plug asked for (python-kasa _build_credentials)
  String cred;
  JsonObject xc = res["extra_crypt"];
  if (xc.isNull() || xc.size() == 0) {
    cred = *user ? String(user) + "/" + pass : String(pass);
  } else {
    String type = xc["type"] | "";
    type.toLowerCase();
    JsonVariant pid = xc["params"]["passwd_id"];
    int passwdId = pid.is<const char*>() ? atoi(pid.as<const char*>()) : (pid | -1);
    if (type == "password_shadow" && passwdId == 2) {
      cred = hexDigest(MBEDTLS_MD_SHA1, pass, false);
    } else {
      err = "TPAP: password scheme " + type + " passwd_id " + passwdId + " not supported";
      return false;
    }
  }
  Bytes credBytes((const uint8_t*)cred.c_str(), (const uint8_t*)cred.c_str() + cred.length());
  wipe(cred);

  ShareOut so;
  bool shareOk = computeShare(credBytes, devSalt, iterations, userRandom, devRandom, devShare, nullptr, so, err);
  wipe(credBytes);
  if (!shareOk) return false;

  // 3. pake_share
  uint8_t dacNonce[32];
  rng(nullptr, dacNonce, sizeof(dacNonce));
  req.clear();
  req["method"] = "login";
  p = req["params"].to<JsonObject>();
  p["sub_method"] = "pake_share";
  p["user_share"] = b64(so.L, sizeof(so.L));
  p["user_confirm"] = b64(so.userConfirm, sizeof(so.userConfirm));
  p["dac_nonce"] = b64(dacNonce, sizeof(dacNonce));
  bool ok = login(ip, port, req, rep, "pake_share", err);
  if (ok) {
    res = rep["result"];
    Bytes devConfirm;
    if (!unb64(res["dev_confirm"] | "", devConfirm, 64) || devConfirm.size() != 32 ||
        !ctEqual(devConfirm.data(), so.devConfirm, 32)) {
      err = "TPAP: plug confirmation mismatch (wrong KASA_PASS?)";
      ok = false;
    }
  }
  ok = ok && verifyDac(res["dac_ca"] | "", res["dac_ica"] | "", res["dac_proof"] | "", so.sharedKey, dacNonce, err);
  if (ok) {
    String stok = res["stok"] | "";
    if (stok.isEmpty()) stok = res["sessionId"] | "";
    if (!okStok(stok) || !res["start_seq"].is<long>()) {
      err = "TPAP: share reply has no usable stok/start_seq";
      ok = false;
    } else if (!deriveSession(so.sharedKey, s)) {
      err = "TPAP: session key derivation failed";
      ok = false;
    } else {
      s.stok = stok;
      s.port = port;
      s.seq = (uint32_t)res["start_seq"].as<long>();
      s.active = true;
      Serial.printf("TPAP session with %s (DAC verified)\n", ip);
    }
  }
  so.wipe();
  if (!ok) dropSession(s);
  return ok;
}

// ---------- requests ----------
static bool request(const char* ip, Session& s, const char* json, String& reply, String& err) {
  uint32_t seq = s.seq;
  Bytes body, resp, pt;
  if (!seal(s, seq, (const uint8_t*)json, strlen(json), body)) { err = "TPAP encrypt failed"; dropSession(s); return false; }
  s.seq = seq + 1;
  String ctype;
  int code = post(ip, s.port, "/stok=" + s.stok + "/ds", "application/octet-stream", body.data(), body.size(), resp, &ctype);
  if (code != 200) { err = String("TPAP request HTTP ") + code; dropSession(s); return false; }
  if (ctype.startsWith("application/json")) {   // plain JSON here means an error, e.g. session expired
    JsonDocument d;
    int ec = deserializeJson(d, (const char*)resp.data(), resp.size()) ? -1 : (d["error_code"] | -1);
    err = String("TPAP request error_code ") + ec;
    dropSession(s);
    return false;
  }
  if (!unseal(s, seq, resp, pt, err)) { dropSession(s); return false; }
  reply = String();
  reply.concat((const char*)pt.data(), pt.size());
  wipe(pt);
  return true;
}

// Send one SMART JSON request, logging in first if needed. A failure on an existing session
// (e.g. it expired) gets one fresh handshake and retry.
static bool query(const char* ip, const char* user, const char* pass, Session& s, const char* json,
                  String& reply, String& err) {
  bool fresh = false;
  if (!s.active) {
    if (!handshake(ip, user, pass, s, err)) return false;
    fresh = true;
  }
  if (request(ip, s, json, reply, err)) return true;
  if (fresh) return false;
  if (!handshake(ip, user, pass, s, err)) return false;
  return request(ip, s, json, reply, err);
}

}  // namespace tpap
