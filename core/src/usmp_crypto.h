#pragma once
#include <stddef.h>
#include <stdint.h>

#define USMP_GCM_NONCE_LEN 12
#define USMP_GCM_TAG_LEN   16

/**
 * Encrypt plaintext with AES-256-GCM.
 *
 * Generates a fresh 12-byte random nonce via usmp_port_random() and
 * prepends it to the output: out = nonce(12) || ciphertext || tag(16)
 *
 * @param key        32-byte AES-256 session key
 * @param aad        Additional authenticated data (frame header, 10 bytes)
 * @param aad_len    Length of AAD
 * @param plaintext  Data to encrypt
 * @param plain_len  Length of plaintext (0 is valid — produces tag only)
 * @param out        Output buffer: must hold plain_len + USMP_GCM_NONCE_LEN + USMP_GCM_TAG_LEN bytes
 * @param out_len    Set to actual output length on success
 * @return           0 on success, -1 on failure
 */
int usmp_gcm_encrypt(const uint8_t *key,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plaintext, size_t plain_len,
                     uint8_t *out, size_t *out_len);

/**
 * Decrypt and authenticate an AES-256-GCM frame.
 *
 * Expects input as: nonce(12) || ciphertext || tag(16)
 *
 * @param key                  32-byte AES-256 session key
 * @param aad                  Additional authenticated data (frame header, 10 bytes)
 * @param aad_len              Length of AAD
 * @param nonce_ct_tag         Input: nonce || ciphertext || tag
 * @param nct_len              Total length of nonce_ct_tag
 * @param out                  Decrypted plaintext output
 * @param out_len              Set to plaintext length on success
 * @return                     0 on success, -1 on authentication failure or error
 */
int usmp_gcm_decrypt(const uint8_t *key,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *nonce_ct_tag, size_t nct_len,
                     uint8_t *out, size_t *out_len);