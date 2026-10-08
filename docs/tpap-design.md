# TPAP client for the ESP32 — design

Status: implemented in `firmware/*/tpap.h`; self-test (known-answer + rejection paths) passes and both sketches read both KP125M plugs live (2026-10-06). Reviewed by the local LLM gateway on 2026-10-06 — see "Review" at the end.

## Why
The owner's plugs are KP125M(US) on firmware 1.4.1. They refuse the legacy protocol (TCP 9999) and KLAP (HTTP 403) and only accept **TPAP**, TP-Link's SPAKE2+ based local protocol. No released library supports it; python-kasa [PR #1592](https://github.com/python-kasa/python-kasa/pull/1592) (`ZeliardM/python-kasa@feature/tpap`) does, and was verified against both plugs on 2026-10-06. That branch's `kasa/transports/tpaptransport.py` is the reference for this port.

## Scope
Only the variant these plugs negotiate (identical on both). Anything else fails closed with an error naming the unsupported parameter, so it can be added deliberately.

| Parameter | Supported | Plugs send |
|---|---|---|
| `tpap.tls` | 0 (plain HTTP) | 0 |
| `tpap.pake` | must contain 2 → passcode type `userpw` | [2] |
| `cipher_suites` | 1: P-256, SHA-256, HKDF-SHA256, HMAC-SHA256 | 1 |
| `encryption` | `aes_128_ccm` | `aes_128_ccm` |
| `extra_crypt` | `password_shadow` `passwd_id=2` (secret = `sha1_hex(password)`), or none (`user/password`) | `password_shadow`, 2 |
| `iterations` | 1 … 20000 (cap bounds PBKDF2 time) | 3000 |
| DAC attestation | required: a plug that does not offer it is refused | yes |

Out of scope: TLS modes 1/2, NOC certificates, camera/robot passcode types, SHA-512/CMAC suites, AES-256-CCM, ChaCha20.

## Protocol (all JSON over `POST http://<ip>/`, `{"method":"login","params":{...}}`)
1. **discover** — `sub_method: "discover"` → `mac`, `tpap{tls,dac,pake,port,user_hash_type}`.
2. **pake_register** — send `username = md5_hex("admin")` (`sha256_hex_upper` if `user_hash_type==1`), 32 random bytes `user_random`, `cipher_suites:[1]`, `encryption:["aes_128_ccm"]`, `passcode_type:"userpw"`, `stok:null`. Reply: `dev_random`, `dev_salt`, `dev_share` (SEC1 point, compressed or not), `cipher_suites`, `iterations`, `encryption`, `extra_crypt`.
3. **SPAKE2+ (client side)**, P-256, fixed points M/N (embedded uncompressed):
   - `w0‖w1 = PBKDF2-SHA256(cred, dev_salt, iterations, 80)`, each 40-byte half reduced mod n.
   - `x` random in [1, n-1]; `L = x·G + w0·M`.
   - `R` = device share, validated on the curve; `R' = R − w0·N` (must not be the identity).
   - `Z = x·R'`, `V = w1·R'`.
   - `TT = SHA256(len8le(SHA256("PAKE V1"‖user_random‖dev_random)) ‖ len8le("") ‖ len8le("") ‖ len8le(M) ‖ len8le(N) ‖ len8le(L) ‖ len8le(R) ‖ len8le(Z) ‖ len8le(V) ‖ len8le(enc(w0)))`, points uncompressed, `enc()` = python-kasa's `_encode_w`.
   - `KcA‖KcB = HKDF-SHA256(TT, salt=0³², info="ConfirmationKeys", 64)`; `K = HKDF(TT, 0³², "SharedKey", 32)`.
   - `user_confirm = HMAC(KcA, R)`; expected `dev_confirm = HMAC(KcB, L)`.
4. **pake_share** — send `user_share=L`, `user_confirm`, `dac_nonce` (32 random bytes). Reply: `dev_confirm`, `dac_ca`, `dac_ica`, `dac_proof`, `stok`, `start_seq`.
5. **Session** — `key = HKDF(K, salt="tp-kdf-salt-aes128-key", info="tp-kdf-info-aes128-key", 16)`, `base_nonce = HKDF(K, "tp-kdf-salt-aes128-iv", "tp-kdf-info-aes128-iv", 12)`. Each request: `POST /stok=<stok>/ds`, body `be32(seq) ‖ AES-128-CCM(key, base_nonce[0:8]‖be32(seq), json) ‖ tag16`; `seq` starts at `start_seq` and increments. Reply has the same framing.
6. **Energy** — `{"method":"get_emeter_data"}` → `power_mw`, `voltage_mv`, `current_ma`, `energy_wh`.

## mbedtls mapping (core 3.1.1, mbedtls 3.6.2)
`mbedtls_pkcs5_pbkdf2_hmac_ext`, `mbedtls_ecp_mul` (constant time, RNG blinding) for every secret-scalar multiplication, `mbedtls_ecp_muladd` only with scalar 1 to add points, `mbedtls_ecp_check_pubkey` on `R`, `mbedtls_hkdf`, `mbedtls_md_hmac`, `mbedtls_ccm_*`, `mbedtls_x509_crt_parse*`, `mbedtls_pk_verify`, `mbedtls_platform_zeroize`. RNG: `esp_fill_random` (hardware TRNG; true random while Wi-Fi is on, which it always is here).

## Security decisions
- **Fail closed.** Any unexpected parameter, failed confirmation, failed DAC check, bad point or CCM tag failure aborts the handshake/request and drops the session. Nothing falls back to a weaker protocol once TPAP is selected.
- **Mutual authentication.** SPAKE2+ confirms both sides know the password-derived verifier; `dev_confirm` is compared in constant time.
- **Device attestation (DAC).** `dac_ica` must be signed by the embedded TP-Link root CA, `dac_ca` by `dac_ica` (or directly by the root when no ICA), both inside their validity period, and `dac_proof` must be a valid ECDSA-SHA256 signature by `dac_ca` over `K ‖ dac_nonce`. Same checks as the reference.
- **Clock.** Validity checks need real time, so the sketches start SNTP after Wi-Fi. Until the clock is set, TPAP refuses to connect ("waiting for NTP") rather than skipping the date check.
- **Secrets in memory.** `w0`, `w1`, `x`, the PBKDF2 output, the credential string and `K` are zeroized as soon as they are no longer needed; the session key and nonce are zeroized when a session is dropped.
- **Credentials at rest.** `KASA_USER`/`KASA_PASS` (and the Wi-Fi password) stay in the git-ignored `secrets.h` and are compiled into flash in plain text, so anyone holding the board can read them with esptool. Accepted for a desk gadget at home. Hardening option, not enabled: ESP32-S3 flash encryption (plus NVS encryption), which protects the image at rest but is a one-way eFuse change on the chip, so it is left to the owner. Storing only `sha1_hex(password)` was considered and rejected: it would still be a working credential for these plugs and would break KLAP and other `passwd_id`s.
- **Back-off.** A failed handshake (wrong password, misbehaving plug, network error) is not retried every poll: each plug waits 3 s, doubling to 5 min, before the next login; a success resets it. Waiting for NTP is not counted (no request is sent). This avoids hammering the plug with logins, which TP-Link devices may answer by locking the account.
- **Input bounds.** HTTP bodies capped at 16 KB, base64 fields length-checked, `iterations` capped, reply sequence number must equal the request's.
- **Replay/ordering.** CCM nonce binds `seq`; a reply whose `seq` differs from the request is rejected and the session reset.

## Integration
`kasa.h` picks the protocol per plug on first contact: legacy (TCP 9999) → if refused, `POST /` discover: a `tpap` object with `tpap_preferred:true` → TPAP; any other JSON reply → KLAP with SMART requests; no JSON → KLAP with legacy IOT requests. `kasaReadEnergy()` returns watts/volts/amps/kWh regardless of protocol, so both sketches stay protocol-agnostic.

## Tests
- **Known-answer test** (`tpap_selftest.h`, built with `-DTPAP_SELFTEST=1`): fixed `x`, randoms, salt, test password and a compressed device share; vectors produced by python-kasa's own `TpapEncryptionSession`. Checks `L`, `user_confirm`, expected `dev_confirm`, `K`, session key, base nonce and one encrypted request byte-for-byte. Runs on the board with no network.
- **Live** against a spare KP125M first, then the plugs in use.

- **Rejection paths** (same self-test, after SNTP): a one-byte-different confirmation fails `ctEqual`; a garbage `dac_ca` is rejected; a forged `dac_proof` on an otherwise valid chain (the self-signed root as its own chain) is rejected by the proof check specifically.

## Review (local LLM gateway, 2026-10-06)
- `architect` (deepseek-r1:32b) on this document: 10 findings, most not applicable because the protocol fixes them (iteration count, "admin" username, SHA-1 credential, key derivation) or out of scope (encrypted RAM, CRL/OCSP for DAC). Accepted: **no back-off on repeated handshake failures** (fixed, see Security decisions), **credentials readable from flash** (documented above), **add negative tests** (added, see Tests).
- `security-review` (deepseek-r1:32b, with and without reasoning) on `computeShare()` and the handshake: no valid findings; each was checked against the code (e.g. "unchecked return values" that are checked). A manual pass found one: `encodeW()` could reallocate and leave `w0` in freed heap; fixed by reserving the buffer (the transcript is reserved too).
- `coder-pro` (qwen2.5-coder:32b) on the back-off and `encodeW()` changes: no bugs found.
