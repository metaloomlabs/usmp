#include "usmp_crypto.h"
#include "usmp_port.h"
#include "mbedtls/gcm.h"
#include <string.h>

/*
 * AES-256-GCM encrypt.
 *
 * Wire format of output:  nonce(12) || ciphertext(plain_len) || tag(16)
 *
 * The 12-byte nonce is generated fresh by usmp_port_random() for every call,
 * guaranteeing that the (key, nonce) pair is never reused regardless of
 * sequence number, session length, or key material.
 */
int usmp_gcm_encrypt(const uint8_t *key,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plaintext, size_t plain_len,
                     uint8_t *out, size_t *out_len) {
  /* Generate a fresh random nonce — never derived from seq/session_id */
  uint8_t nonce[USMP_GCM_NONCE_LEN];
  if (usmp_port_random(nonce, USMP_GCM_NONCE_LEN) != 0)
    return -1;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = -1;

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
    goto done;

  /* Layout: out = nonce || ciphertext || tag */
  uint8_t *ct_out  = out + USMP_GCM_NONCE_LEN;
  uint8_t *tag_out = ct_out + plain_len;

  const uint8_t *in_ptr = plaintext;
  uint8_t dummy_in[1] = {0};
  if (in_ptr == NULL) {
    in_ptr = dummy_in;
  }

  if (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                 plain_len,
                                 nonce, USMP_GCM_NONCE_LEN,
                                 aad, aad_len,
                                 in_ptr, ct_out,
                                 USMP_GCM_TAG_LEN, tag_out) != 0)
    goto done;

  /* Prepend nonce so receiver can extract it */
  memcpy(out, nonce, USMP_GCM_NONCE_LEN);

  *out_len = USMP_GCM_NONCE_LEN + plain_len + USMP_GCM_TAG_LEN;
  ret = 0;

done:
  mbedtls_gcm_free(&gcm);
  return ret;
}

/*
 * AES-256-GCM decrypt and authenticate.
 *
 * Expects input as:  nonce(12) || ciphertext || tag(16)
 * Extracts the nonce from the first 12 bytes of nonce_ct_tag.
 * Returns -1 if authentication tag does not match (tampered or wrong key).
 */
int usmp_gcm_decrypt(const uint8_t *key,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *nonce_ct_tag, size_t nct_len,
                     uint8_t *out, size_t *out_len) {
  /* Must have at least nonce + tag, even with empty plaintext */
  if (nct_len < USMP_GCM_NONCE_LEN + USMP_GCM_TAG_LEN)
    return -1;

  const uint8_t *nonce      = nonce_ct_tag;
  const uint8_t *ciphertext = nonce_ct_tag + USMP_GCM_NONCE_LEN;
  size_t cipher_len         = nct_len - USMP_GCM_NONCE_LEN - USMP_GCM_TAG_LEN;
  const uint8_t *tag        = ciphertext + cipher_len;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = -1;

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
    goto done;

  const uint8_t *ct_ptr = ciphertext;
  uint8_t *out_ptr = out;
  uint8_t dummy[1] = {0};
  if (ct_ptr == NULL) {
    ct_ptr = dummy;
  }
  if (out_ptr == NULL) {
    out_ptr = dummy;
  }

  if (mbedtls_gcm_auth_decrypt(&gcm, cipher_len,
                                nonce, USMP_GCM_NONCE_LEN,
                                aad, aad_len,
                                tag, USMP_GCM_TAG_LEN,
                                ct_ptr, out_ptr) != 0)
    goto done;

  *out_len = cipher_len;
  ret = 0;

done:
  mbedtls_gcm_free(&gcm);
  return ret;
}