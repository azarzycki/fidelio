#include "RaopCrypto.h"

#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/rsa.h>

#include "airport_key.h"
#include "ios3_trace.h"

RaopCrypto::RaopCrypto() {
  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  const char* pers = "fidelio-raop";
  int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
  if (rc != 0) { IOS3_TRACE("ctr_drbg_seed failed -0x%04x", -rc); return; }
  rc = mbedtls_pk_parse_key(&pk, (const unsigned char*)kAirportExpressKeyPem, sizeof(kAirportExpressKeyPem), nullptr, 0,
                            mbedtls_ctr_drbg_random, &drbg);
  if (rc != 0) { IOS3_TRACE("AirPort key parse failed -0x%04x", -rc); return; }
  keyOk = true;
}

RaopCrypto::~RaopCrypto() {
  mbedtls_pk_free(&pk);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
}

std::vector<uint8_t> RaopCrypto::b64decode(const std::string& in) {
  std::string s = in;
  while (s.size() % 4) s += '=';  // RAOP strips the padding
  std::vector<uint8_t> out(s.size());
  size_t olen = 0;
  if (mbedtls_base64_decode(out.data(), out.size(), &olen, (const unsigned char*)s.data(), s.size()) != 0) return {};
  out.resize(olen);
  return out;
}

std::string RaopCrypto::b64encode(const uint8_t* d, size_t n, bool pad) {
  std::string out((n + 2) / 3 * 4 + 1, '\0');
  size_t olen = 0;
  if (mbedtls_base64_encode((unsigned char*)out.data(), out.size(), &olen, d, n) != 0) return "";
  out.resize(olen);
  if (!pad) while (!out.empty() && out.back() == '=') out.pop_back();
  return out;
}

std::string RaopCrypto::appleResponse(const std::string& challengeB64, const uint8_t ipv4[4], const uint8_t hwaddr[6]) {
  if (!keyOk) return "";
  std::vector<uint8_t> chall = b64decode(challengeB64);
  if (chall.empty() || chall.size() > 16) return "";
  uint8_t buf[32];
  memset(buf, 0, sizeof buf);
  size_t n = 0;
  memcpy(buf + n, chall.data(), chall.size()); n += chall.size();
  memcpy(buf + n, ipv4, 4); n += 4;
  memcpy(buf + n, hwaddr, 6); n += 6;
  if (n < 32) n = 32;
  unsigned char sig[512];
  size_t siglen = 0;
  mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
  int rc = mbedtls_pk_sign(&pk, MBEDTLS_MD_NONE, buf, n, sig, sizeof sig, &siglen, mbedtls_ctr_drbg_random, &drbg);
  if (rc != 0) { IOS3_TRACE("Apple-Response sign failed -0x%04x", -rc); return ""; }
  return b64encode(sig, siglen, false);
}

bool RaopCrypto::unwrapAesKey(const std::string& b64, std::vector<uint8_t>& key) {
  if (!keyOk) return false;
  std::vector<uint8_t> enc = b64decode(b64);
  if (enc.empty()) return false;
  mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
  unsigned char out[512];
  size_t olen = 0;
  int rc = mbedtls_pk_decrypt(&pk, enc.data(), enc.size(), out, &olen, sizeof out, mbedtls_ctr_drbg_random, &drbg);
  if (rc != 0 || olen != 16) { IOS3_TRACE("rsaaeskey unwrap failed -0x%04x (len %d)", -rc, (int)olen); return false; }
  key.assign(out, out + 16);
  return true;
}

AesCbcDecryptor::AesCbcDecryptor() { mbedtls_aes_init(&aes); memset(iv, 0, sizeof iv); }
AesCbcDecryptor::~AesCbcDecryptor() { mbedtls_aes_free(&aes); }

void AesCbcDecryptor::setKey(const std::vector<uint8_t>& key, const std::vector<uint8_t>& ivIn) {
  if (key.size() != 16 || ivIn.size() != 16) { active = false; return; }
  mbedtls_aes_setkey_dec(&aes, key.data(), 128);
  memcpy(iv, ivIn.data(), 16);
  active = true;
}

void AesCbcDecryptor::decrypt(uint8_t* data, size_t len) {
  if (!active) return;
  size_t whole = len / 16 * 16;
  if (whole == 0) return;
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, whole, ivCopy, data, data);
}
