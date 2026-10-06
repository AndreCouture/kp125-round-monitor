// Known-answer test for tpap.h; no network or real credentials needed.
// Vectors computed by python-kasa's own TpapEncryptionSession (PR #1592 branch) with fixed
// randomness, test password "kat-password", passwd_id 2, iterations 3000 and a compressed
// device share. Build with -DTPAP_SELFTEST=1 and call tpap::selfTest() from setup().
#pragma once
#include "tpap.h"

namespace tpap {

static Bytes katHex(const char* h) {
  Bytes out;
  for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
    char b[3] = {h[i], h[i + 1], 0};
    out.push_back((uint8_t)strtoul(b, nullptr, 16));
  }
  return out;
}

static bool katCheck(const char* name, const uint8_t* got, size_t n, const char* wantHex) {
  Bytes want = katHex(wantHex);
  bool ok = want.size() == n && memcmp(got, want.data(), n) == 0;
  Serial.printf("  %-22s %s\n", name, ok ? "ok" : "MISMATCH");
  if (!ok) Serial.printf("    got  %s\n    want %s\n", hexOf(got, n, false).c_str(), wantHex);
  return ok;
}

static bool selfTest() {
  Serial.println("TPAP self-test (known-answer vectors from python-kasa)");
  bool ok = true;

  String cred = hexDigest(MBEDTLS_MD_SHA1, "kat-password", false);
  ok &= katCheck("cred sha1_hex", (const uint8_t*)cred.c_str(), cred.length(),
                 "30373837313965653061363961663333383061663164623164653733643637346364666436666639");

  Bytes credB((const uint8_t*)cred.c_str(), (const uint8_t*)cred.c_str() + cred.length());
  Bytes salt = katHex("c8c9cacbcccdcecfd0d1d2d3d4d5d6d7");
  Bytes userRandom = katHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  Bytes devRandom = katHex("6465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f80818283");
  Bytes devShare = katHex("02d360332fad9bc83afaff4a740de8a516bf1b8fb3fde360ff1d03979c1f943ee2");
  Bytes xb = katHex("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
  mbedtls_mpi x;
  mbedtls_mpi_init(&x);
  mbedtls_mpi_read_binary(&x, xb.data(), xb.size());

  ShareOut so;
  String err;
  uint32_t t0 = millis();
  bool shared = computeShare(credB, salt, 3000, userRandom.data(), devRandom, devShare, &x, so, err);
  uint32_t ms = millis() - t0;
  mbedtls_mpi_free(&x);
  if (!shared) {
    Serial.printf("  computeShare failed: %s\n", err.c_str());
    return false;
  }
  Serial.printf("  computeShare took %lu ms\n", (unsigned long)ms);
  ok &= katCheck("user_share (L)", so.L, 65,
                 "04cb0ff67c54b833e50fb4e6c8e73ae9f4e90876213bfce7e7eb7cccd068fb4295415b9bec631ff2cbe4c5a3adb5b31bc3f0959cd81a5addeb1b27eaba8d4b4ea3");
  ok &= katCheck("user_confirm", so.userConfirm, 32, "8d8bcdd42ec1ba1f5d78beeeb8e58655ce27718165a93d60d83bf8144485acde");
  ok &= katCheck("expected dev_confirm", so.devConfirm, 32, "0322ee7d25aa416aff569a45d8c12d0fb9dac6a483a8c67413a31bb3e00044c0");
  ok &= katCheck("shared key", so.sharedKey, 32, "c2f1d42a4ef92cbf2f2bcaeaa95bc6436d26d0061513c6735b0ce2129d7b6d48");

  Session s;
  ok &= deriveSession(so.sharedKey, s);
  ok &= katCheck("session key", s.key, 16, "5ff38df711f14cdb2e7b6478660e55f5");
  ok &= katCheck("base nonce", s.nonce, 12, "c74d3ec0d1bdc71e7e7dd567");

  const char* req = "{\"method\":\"get_emeter_data\"}";
  Bytes sealed, opened;
  ok &= seal(s, 7, (const uint8_t*)req, strlen(req), sealed);
  ok &= katCheck("request seq 7 ct+tag", sealed.data() + 4, sealed.size() - 4,
                 "41cf16aaa099612fa32f23507f2d5d69d5ded3861e16fc5c4ff33d512e611ce265028f4bb7dc916b2f0fcbb5");
  bool rt = unseal(s, 7, sealed, opened, err) && opened.size() == strlen(req) && memcmp(opened.data(), req, opened.size()) == 0;
  Serial.printf("  %-22s %s\n", "seal/unseal roundtrip", rt ? "ok" : "FAIL");
  sealed[10] ^= 1;   // tampered reply must be rejected
  bool tamper = !unseal(s, 7, sealed, opened, err);
  Serial.printf("  %-22s %s\n", "tamper rejected", tamper ? "ok" : "FAIL");
  bool wrongSeq = !unseal(s, 8, sealed, opened, err);
  Serial.printf("  %-22s %s\n", "wrong seq rejected", wrongSeq ? "ok" : "FAIL");
  ok &= rt && tamper && wrongSeq;

  mbedtls_x509_crt root;
  mbedtls_x509_crt_init(&root);
  bool rootOk = mbedtls_x509_crt_parse(&root, (const uint8_t*)ROOT_CA_PEM, sizeof(ROOT_CA_PEM)) == 0 && inValidity(root);
  mbedtls_x509_crt_free(&root);
  Serial.printf("  %-22s %s%s\n", "root CA parses", rootOk ? "ok" : "FAIL", clockReady() ? "" : " (clock not set: validity unchecked)");
  ok &= rootOk || !clockReady();

  // Rejection paths: a wrong confirmation, a garbage certificate, a forged attestation proof
  uint8_t a[32] = {0}, b[32] = {0}, key[32], nonce[32];
  b[31] = 1;
  bool ct = ctEqual(a, a, 32) && !ctEqual(a, b, 32);
  Serial.printf("  %-22s %s\n", "confirm mismatch caught", ct ? "ok" : "FAIL");
  rng(nullptr, key, sizeof(key));
  rng(nullptr, nonce, sizeof(nonce));
  String e1, e2;
  bool garbage = !verifyDac("bm90IGEgY2VydGlmaWNhdGU=", "", "AAAA", key, nonce, e1);   // "not a certificate"
  Serial.printf("  %-22s %s (%s)\n", "garbage DAC rejected", garbage ? "ok" : "FAIL", e1.c_str());
  // The self-signed root passes as its own chain, so only the proof check can reject this
  uint8_t fake[70];
  rng(nullptr, fake, sizeof(fake));
  bool forged = !verifyDac(ROOT_CA_PEM, "", b64(fake, sizeof(fake)).c_str(), key, nonce, e2);
  bool viaProof = e2.indexOf("proof") >= 0;
  Serial.printf("  %-22s %s (%s)\n", "forged proof rejected", forged && (viaProof || !clockReady()) ? "ok" : "FAIL", e2.c_str());
  ok &= ct && garbage && forged && (viaProof || !clockReady());

  so.wipe();
  dropSession(s);
  Serial.println(ok ? "TPAP self-test PASSED" : "TPAP self-test FAILED");
  return ok;
}

}  // namespace tpap
