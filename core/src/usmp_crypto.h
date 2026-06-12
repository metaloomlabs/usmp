#pragma once
#include <stddef.h>
#include <stdint.h>

#define USMP_GCM_NONCE_LEN 12
#define USMP_GCM_TAG_LEN 16

// Encrypt plaintext into out (ciphertext + tag appended)
// out must be at least plain_len + USMP_GCM_TAG_LEN bytes
// seq and session_id are used to construct the nonce
int usmp_gcm_encrypt(
    const uint8_t *key, // 32 bytes
    uint32_t seq,
    const uint8_t *session_id, // 4 bytes
    const uint8_t *aad,        // additional authenticated data (header)
    size_t aad_len, const uint8_t *plaintext, size_t plain_len,
    uint8_t *out, // ciphertext + tag
    size_t *out_len);

// Decrypt and verify
// ciphertext_and_tag is ciphertext followed by 16-byte tag
int usmp_gcm_decrypt(const uint8_t *key, uint32_t seq,
                     const uint8_t *session_id, const uint8_t *aad,
                     size_t aad_len, const uint8_t *ciphertext_and_tag,
                     size_t ct_len, // includes tag
                     uint8_t *out, size_t *out_len);