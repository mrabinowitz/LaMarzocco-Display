#include "lamarzocco_auth.h"
#include <WiFi.h>
#include <esp_random.h>
#include <string.h>
#include "config.h"
#include "mbedtls/md.h"

// Base64 encoding table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

String LaMarzoccoAuth::base64_encode(const uint8_t* data, size_t len) {
    String result = "";
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                result += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (j = 0; j < i + 1; j++) {
            result += base64_chars[char_array_4[j]];
        }
        
        while (i++ < 3) {
            result += '=';
        }
    }
    
    return result;
}

bool LaMarzoccoAuth::base64_decode(const String& encoded, uint8_t* output, size_t* output_len) {
    const char* in = encoded.c_str();
    size_t in_len = encoded.length();
    size_t i = 0;
    size_t j = 0;
    int in_idx = 0;
    uint8_t char_array_4[4], char_array_3[3];
    
    *output_len = 0;
    
    while (in_len-- && (in[in_idx] != '=') && is_base64((unsigned char)in[in_idx])) {
        char_array_4[i++] = in[in_idx];
        in_idx++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                const char* pos = strchr(base64_chars, char_array_4[i]);
                if (pos) {
                    char_array_4[i] = pos - base64_chars;
                } else {
                    char_array_4[i] = 0;
                }
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++) {
                output[(*output_len)++] = char_array_3[i];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }
        
        for (j = 0; j < 4; j++) {
            const char* pos = strchr(base64_chars, char_array_4[j]);
            if (pos) {
                char_array_4[j] = pos - base64_chars;
            } else {
                char_array_4[j] = 0;
            }
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (j = 0; j < i - 1; j++) {
            output[(*output_len)++] = char_array_3[j];
        }
    }
    
    return true;
}

String LaMarzoccoAuth::generate_uuid() {
    uint8_t uuid_bytes[16];
    esp_fill_random(uuid_bytes, 16);
    
    // Set version (4) and variant bits
    uuid_bytes[6] = (uuid_bytes[6] & 0x0F) | 0x40;  // Version 4
    uuid_bytes[8] = (uuid_bytes[8] & 0x3F) | 0x80;  // Variant 10
    
    String uuid = "";
    char hex_chars[] = "0123456789abcdef";
    
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            uuid += "-";
        }
        uuid += hex_chars[(uuid_bytes[i] >> 4) & 0x0F];
        uuid += hex_chars[uuid_bytes[i] & 0x0F];
    }
    
    return uuid;
}

void LaMarzoccoAuth::derive_secret_bytes(const String& installation_id, const uint8_t* pub_der_bytes, size_t pub_len, uint8_t* secret_out) {
    String pub_b64 = base64_encode(pub_der_bytes, pub_len);
    
    uint8_t inst_hash[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, (const unsigned char*)installation_id.c_str(), installation_id.length());
    mbedtls_sha256_finish(&sha256_ctx, inst_hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    String inst_hash_b64 = base64_encode(inst_hash, 32);
    String triple = installation_id + "." + pub_b64 + "." + inst_hash_b64;
    
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, (const unsigned char*)triple.c_str(), triple.length());
    mbedtls_sha256_finish(&sha256_ctx, secret_out);
    mbedtls_sha256_free(&sha256_ctx);
}

String LaMarzoccoAuth::generate_base_string(const InstallationKey& key) {
    uint8_t pub_hash[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, key.public_key_der, key.public_key_len);
    mbedtls_sha256_finish(&sha256_ctx, pub_hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    String pub_hash_b64 = base64_encode(pub_hash, 32);
    return key.installation_id + "." + pub_hash_b64;
}

String LaMarzoccoAuth::generate_request_proof(const String& base_string, const uint8_t* secret32) {
    uint8_t work[32];
    memcpy(work, secret32, 32);
    
    const char* base_str = base_string.c_str();
    size_t len = base_string.length();
    
    for (size_t i = 0; i < len; i++) {
        uint8_t byte_val = (uint8_t)base_str[i];
        size_t idx = byte_val % 32;
        size_t shift_idx = (idx + 1) % 32;
        uint8_t shift_amount = work[shift_idx] & 7;
        
        uint8_t xor_result = byte_val ^ work[idx];
        uint8_t rotated = ((xor_result << shift_amount) | (xor_result >> (8 - shift_amount))) & 0xFF;
        work[idx] = rotated;
    }
    
    uint8_t hash[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, work, 32);
    mbedtls_sha256_finish(&sha256_ctx, hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    return base64_encode(hash, 32);
}

void LaMarzoccoAuth::generate_extra_request_headers(const InstallationKey& key, String& installation_id, String& timestamp, String& nonce, String& signature) {
    installation_id = key.installation_id;
    nonce = generate_uuid();
    // Timestamp in milliseconds
    timestamp = String((unsigned long)millis());
    
    // Build strings more efficiently to avoid stack overflow
    String proof_input;
    proof_input.reserve(200);  // Pre-allocate to avoid reallocations
    proof_input = installation_id;
    proof_input += ".";
    proof_input += nonce;
    proof_input += ".";
    proof_input += timestamp;
    
    String proof = generate_request_proof(proof_input, key.secret);
    
    String signature_data;
    signature_data.reserve(300);  // Pre-allocate
    signature_data = proof_input;
    signature_data += ".";
    signature_data += proof;
    
    // Sign with ECDSA
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    int ret = mbedtls_pk_parse_key(&pk, key.private_key_der, key.private_key_len, NULL, 0);
    if (ret != 0) {
        debugln("Failed to parse private key");
        mbedtls_pk_free(&pk);
        signature = "";
        return;
    }
    
    // ECDSA signature buffer - DER format can be up to 72 bytes for SECP256R1
    uint8_t sig[80];  // Increased buffer size
    size_t sig_len = sizeof(sig);
    
    mbedtls_ecdsa_context* ecdsa = mbedtls_pk_ec(pk);
    if (!ecdsa) {
        debugln("Failed to get ECDSA context");
        mbedtls_pk_free(&pk);
        signature = "";
        return;
    }
    
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        debugln("Failed to get MD info");
        mbedtls_pk_free(&pk);
        signature = "";
        return;
    }
    
    ret = mbedtls_md_setup(&md_ctx, md_info, 0);
    if (ret != 0) {
        debugln("Failed to setup MD context");
        mbedtls_md_free(&md_ctx);
        mbedtls_pk_free(&pk);
        signature = "";
        return;
    }
    
    uint8_t hash[32];
    mbedtls_md_starts(&md_ctx);
    mbedtls_md_update(&md_ctx, (const unsigned char*)signature_data.c_str(), signature_data.length());
    mbedtls_md_finish(&md_ctx, hash);
    mbedtls_md_free(&md_ctx);
    
    ret = mbedtls_ecdsa_write_signature(ecdsa, MBEDTLS_MD_SHA256,
                                         hash, 32,
                                         sig, &sig_len, NULL, NULL);
    
    mbedtls_pk_free(&pk);
    
    if (ret == 0 && sig_len > 0 && sig_len <= sizeof(sig)) {
        signature = base64_encode(sig, sig_len);
    } else {
        debug("ECDSA signing failed, ret=");
        debugln(ret);
        signature = "";
    }
}

bool LaMarzoccoAuth::generate_installation_key(const String& installation_id, InstallationKey& key) {
    key.installation_id = installation_id;
    
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char* pers = "lamarzocco_keygen";
    
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        debugln("Failed to seed RNG");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }
    
    ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) {
        debugln("Failed to setup PK context");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }
    
    mbedtls_ecp_group_id grp_id = MBEDTLS_ECP_DP_SECP256R1;
    ret = mbedtls_ecp_gen_key(grp_id, mbedtls_pk_ec(pk), mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        debugln("Failed to generate EC key");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }
    
    // Export private key in DER format
    ret = mbedtls_pk_write_key_der(&pk, key.private_key_der, sizeof(key.private_key_der));
    if (ret < 0) {
        debugln("Failed to write private key");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }
    // mbedtls_pk_write_key_der writes from the end, so we need to adjust
    key.private_key_len = ret;
    memmove(key.private_key_der, key.private_key_der + sizeof(key.private_key_der) - ret, ret);
    
    // Export public key in DER format
    ret = mbedtls_pk_write_pubkey_der(&pk, key.public_key_der, sizeof(key.public_key_der));
    if (ret < 0) {
        debugln("Failed to write public key");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&pk);
        return false;
    }
    // mbedtls_pk_write_pubkey_der writes from the end, so we need to adjust
    key.public_key_len = ret;
    memmove(key.public_key_der, key.public_key_der + sizeof(key.public_key_der) - ret, ret);
    
    // Derive secret bytes
    derive_secret_bytes(installation_id, key.public_key_der, key.public_key_len, key.secret);
    
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_pk_free(&pk);
    
    return true;
}

bool LaMarzoccoAuth::save_installation_key(Preferences& prefs, const InstallationKey& key) {
    prefs.putString("INST_ID", key.installation_id);
    prefs.putBytes("INST_SECRET", key.secret, 32);
    prefs.putBytes("INST_PRIVKEY", key.private_key_der, key.private_key_len);
    prefs.putBytes("INST_PUBKEY", key.public_key_der, key.public_key_len);
    prefs.putUInt("INST_PRIVLEN", key.private_key_len);
    prefs.putUInt("INST_PUBLEN", key.public_key_len);
    return true;
}

bool LaMarzoccoAuth::load_installation_key(Preferences& prefs, InstallationKey& key) {
    key.installation_id = prefs.getString("INST_ID", "");
    if (key.installation_id.length() == 0) {
        return false;
    }
    
    // Check if secret exists first (to avoid error messages)
    if (!prefs.isKey("INST_SECRET")) {
        debugln("Installation secret not found");
        return false;
    }
    
    size_t secret_len = prefs.getBytes("INST_SECRET", key.secret, 32);
    if (secret_len != 32) {
        debugln("Invalid installation secret length");
        return false;
    }
    
    if (!prefs.isKey("INST_PRIVKEY")) {
        debugln("Installation private key not found");
        return false;
    }
    
    key.private_key_len = prefs.getBytes("INST_PRIVKEY", key.private_key_der, sizeof(key.private_key_der));
    if (key.private_key_len == 0) {
        debugln("Failed to load private key");
        return false;
    }
    
    if (!prefs.isKey("INST_PUBKEY")) {
        debugln("Installation public key not found");
        return false;
    }
    
    key.public_key_len = prefs.getBytes("INST_PUBKEY", key.public_key_der, sizeof(key.public_key_der));
    if (key.public_key_len == 0) {
        debugln("Failed to load public key");
        return false;
    }
    
    return true;
}

