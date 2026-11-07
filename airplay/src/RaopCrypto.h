#pragma once
// RAOP crypto on mbedTLS: Apple-Challenge signing and rsaaeskey unwrapping with the AirPort Express
// key, AES-128-CBC payload decryption, base64.
#include <stdint.h>

#include <string>
#include <vector>

#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>

class RaopCrypto {
 public:
  RaopCrypto();
  ~RaopCrypto();
  bool ok() const { return keyOk; }

  // Apple-Response for an Apple-Challenge: RSA PKCS#1 v1.5 "raw" signature (no DigestInfo) over
  // challenge(16) | server IPv4 (4) | hw addr (6), zero padded to 32 bytes. Returns base64 without '='.
  std::string appleResponse(const std::string& challengeB64, const uint8_t ipv4[4], const uint8_t hwaddr[6]);
  // rsaaeskey (base64, RSA-OAEP/SHA1 with the AirPort key) -> 16-byte AES key.
  bool unwrapAesKey(const std::string& rsaaeskeyB64, std::vector<uint8_t>& key);

  static std::vector<uint8_t> b64decode(const std::string& s);
  static std::string b64encode(const uint8_t* d, size_t n, bool pad);

 private:
  mbedtls_pk_context pk;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  bool keyOk = false;
};

// AES-128-CBC decryption of RTP payloads: whole 16-byte blocks only, the trailing partial block
// is left in the clear (RAOP convention). The IV is reset for every packet.
class AesCbcDecryptor {
 public:
  AesCbcDecryptor();
  ~AesCbcDecryptor();
  void setKey(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv);
  bool enabled() const { return active; }
  void decrypt(uint8_t* data, size_t len);

 private:
  mbedtls_aes_context aes;
  uint8_t iv[16];
  bool active = false;
};
