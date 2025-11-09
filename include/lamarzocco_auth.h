#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

struct InstallationKey {
    String installation_id;
    uint8_t secret[32];  // 32 bytes
    uint8_t private_key_der[121];  // SECP256R1 private key in DER format (max 121 bytes)
    size_t private_key_len;
    uint8_t public_key_der[91];  // SECP256R1 public key in DER format (max 91 bytes)
    size_t public_key_len;
    
    bool isValid() const {
        return installation_id.length() > 0 && private_key_len > 0;
    }
};

class LaMarzoccoAuth {
public:
    // Generate installation key from installation_id
    static bool generate_installation_key(const String& installation_id, InstallationKey& key);
    
    // Load installation key from preferences
    static bool load_installation_key(Preferences& prefs, InstallationKey& key);
    
    // Save installation key to preferences
    static bool save_installation_key(Preferences& prefs, const InstallationKey& key);
    
    // Generate request proof (Y5.e algorithm)
    static String generate_request_proof(const String& base_string, const uint8_t* secret32);
    
    // Generate extra request headers for API calls
    static void generate_extra_request_headers(const InstallationKey& key, String& installation_id, String& timestamp, String& nonce, String& signature);
    
    // Generate base string: installation_id.sha256(public_key_der_bytes)
    static String generate_base_string(const InstallationKey& key);
    
    // Base64 encode/decode helpers
    static String base64_encode(const uint8_t* data, size_t len);
    static bool base64_decode(const String& encoded, uint8_t* output, size_t* output_len);
    
    // Generate UUID v4
    static String generate_uuid();
    
private:
    // Derive secret bytes from installation_id and public key
    static void derive_secret_bytes(const String& installation_id, const uint8_t* pub_der_bytes, size_t pub_len, uint8_t* secret_out);
};

