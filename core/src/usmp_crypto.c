#include "usmp_crypto.h"
#include "mbedtls/gcm.h"
#include <string.h>

// nonce = seq(4 LE) || session_id(4) || 0x000000(4)
static void build_nonce(uint32_t seq, const uint8_t *session_id, uint8_t *nonce)
{
    nonce[0] = seq & 0xFF;
    nonce[1] = (seq >> 8) & 0xFF;
    nonce[2] = (seq >> 16) & 0xFF;
    nonce[3] = (seq >> 24) & 0xFF;
    memcpy(nonce + 4, session_id, 4);
    memset(nonce + 8, 0, 4);
}

int usmp_gcm_encrypt(
    const uint8_t *key,
    uint32_t seq,
    const uint8_t *session_id,
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t plain_len,
    uint8_t *out,
    size_t *out_len)
{
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = -1;
    uint8_t nonce[USMP_GCM_NONCE_LEN];
    build_nonce(seq, session_id, nonce);

    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        goto done;

    if (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                  plain_len, nonce, USMP_GCM_NONCE_LEN,
                                  aad, aad_len,
                                  plaintext, out,
                                  USMP_GCM_TAG_LEN, out + plain_len) != 0)
        goto done;

    *out_len = plain_len + USMP_GCM_TAG_LEN;
    ret = 0;

done:
    mbedtls_gcm_free(&gcm);
    return ret;
}

int usmp_gcm_decrypt(
    const uint8_t *key,
    uint32_t seq,
    const uint8_t *session_id,
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext_and_tag,
    size_t ct_len,
    uint8_t *out,
    size_t *out_len)
{
    if (ct_len < USMP_GCM_TAG_LEN)
        return -1;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = -1;
    uint8_t nonce[USMP_GCM_NONCE_LEN];
    build_nonce(seq, session_id, nonce);

    size_t cipher_len = ct_len - USMP_GCM_TAG_LEN;
    const uint8_t *tag = ciphertext_and_tag + cipher_len;

    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        goto done;

    if (mbedtls_gcm_auth_decrypt(&gcm, cipher_len,
                                 nonce, USMP_GCM_NONCE_LEN,
                                 aad, aad_len,
                                 tag, USMP_GCM_TAG_LEN,
                                 ciphertext_and_tag, out) != 0)
        goto done;

    *out_len = cipher_len;
    ret = 0;

done:
    mbedtls_gcm_free(&gcm);
    return ret;
}