#include "dxp_handshake.h"
#include "dxp_frame.h"
#include "dxp_transport.h"
#include "dxp_port.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/constant_time.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static const char *TAG = "DXP_HS";

#define PUB_KEY_LEN 32

// ── HKDF-SHA256 ───────────────────────────────────────────────────────────────
static int derive_session_key(
    const uint8_t *shared_secret, size_t secret_len,
    const uint8_t *nonce, size_t nonce_len,
    const uint8_t *pub_c,
    const uint8_t *pub_s,
    uint8_t *out, size_t out_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md)
        return -1;

    uint8_t info[6 + PUB_KEY_LEN + PUB_KEY_LEN];
    memcpy(info, "dxp-v1", 6);
    memcpy(info + 6, pub_c, PUB_KEY_LEN);
    memcpy(info + 6 + PUB_KEY_LEN, pub_s, PUB_KEY_LEN);

    return mbedtls_hkdf(md,
                        nonce, nonce_len,
                        shared_secret, secret_len,
                        info, sizeof(info),
                        out, out_len);
}

// ── HMAC-SHA256(PSK, data) ────────────────────────────────────────────────────
static int compute_hmac(
    const uint8_t *psk, size_t psk_len,
    const uint8_t *data, size_t data_len,
    uint8_t *out)
{
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info)
        return -1;

    int ret = 0;
    if (mbedtls_md_setup(&ctx, info, 1) != 0)
    {
        ret = -1;
        goto done;
    }
    if (mbedtls_md_hmac_starts(&ctx, psk, psk_len) != 0)
    {
        ret = -1;
        goto done;
    }
    if (mbedtls_md_hmac_update(&ctx, data, data_len) != 0)
    {
        ret = -1;
        goto done;
    }
    if (mbedtls_md_hmac_finish(&ctx, out) != 0)
    {
        ret = -1;
        goto done;
    }
done:
    mbedtls_md_free(&ctx);
    return ret;
}

// ── Handshake ─────────────────────────────────────────────────────────────────
int dxp_handshake(int sock, dxp_session_t *session)
{
    int ret = -1;
    char _msg[128];

    mbedtls_ecdh_context ecdh;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_ecdh_init(&ecdh);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    uint8_t tx_buf[512];
    uint8_t rx_buf[512];
    dxp_packet_t pkt;
    int len;

    const uint8_t *psk = (const uint8_t *)DXP_PSK;
    size_t psk_len = strlen(DXP_PSK);

    // ── Seed RNG ──────────────────────────────────────────────────────────────
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const uint8_t *)"dxp", 3) != 0)
    {
        DXP_LOGE(TAG, "RNG seed failed");
        goto cleanup;
    }

    // ── Setup Curve25519 ──────────────────────────────────────────────────────
    if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_CURVE25519) != 0)
    {
        DXP_LOGE(TAG, "ECDH setup failed");
        goto cleanup;
    }

    // ── Generate keypair + export public key ──────────────────────────────────
    uint8_t pub_buf[65];
    size_t pub_buf_len = 0;
    if (mbedtls_ecdh_make_public(&ecdh, &pub_buf_len, pub_buf, sizeof(pub_buf),
                                 mbedtls_ctr_drbg_random, &ctr_drbg) != 0)
    {
        DXP_LOGE(TAG, "ECDH make_public failed");
        goto cleanup;
    }

    uint8_t *pub_c = pub_buf + (pub_buf_len - PUB_KEY_LEN);

    // ── Step 1: Send HELLO [device_id(6) || pub_C(32)] ───────────────────────
    dxp_port_get_device_id(session->device_id, DXP_DEVICE_ID_LEN);

    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = DXP_MAGIC;
    pkt.version = 1;
    pkt.type = DXP_TYPE_HELLO;
    pkt.seq = 0;
    pkt.length = DXP_DEVICE_ID_LEN + PUB_KEY_LEN;
    memcpy(pkt.payload, session->device_id, DXP_DEVICE_ID_LEN);
    memcpy(pkt.payload + DXP_DEVICE_ID_LEN, pub_c, PUB_KEY_LEN);

    len = dxp_build_packet(&pkt, tx_buf, NULL);
    if (dxp_tcp_send(sock, tx_buf, len) < 0)
    {
        DXP_LOGE(TAG, "Failed to send HELLO");
        goto cleanup;
    }
    snprintf(_msg, sizeof(_msg),
             "HELLO sent (device_id: %02x:%02x:%02x:%02x:%02x:%02x)",
             session->device_id[0], session->device_id[1], session->device_id[2],
             session->device_id[3], session->device_id[4], session->device_id[5]);
    DXP_LOGI(TAG, _msg);

    // ── Step 2: Receive CHALLENGE [nonce(32) || pub_S(32)] ───────────────────
    len = dxp_tcp_recv(sock, rx_buf, sizeof(rx_buf));
    if (len < 0)
    {
        DXP_LOGE(TAG, "Failed to receive CHALLENGE");
        goto cleanup;
    }

    if (dxp_parse_packet(rx_buf, len, &pkt) != 0 ||
        pkt.type != DXP_TYPE_CHALLENGE ||
        pkt.length != DXP_NONCE_LEN + PUB_KEY_LEN)
    {
        snprintf(_msg, sizeof(_msg),
                 "Bad CHALLENGE frame (type=0x%02x len=%u)", pkt.type, pkt.length);
        DXP_LOGE(TAG, _msg);
        goto cleanup;
    }

    uint8_t nonce[DXP_NONCE_LEN];
    uint8_t pub_s[PUB_KEY_LEN];
    memcpy(nonce, pkt.payload, DXP_NONCE_LEN);
    memcpy(pub_s, pkt.payload + DXP_NONCE_LEN, PUB_KEY_LEN);
    DXP_LOGI(TAG, "CHALLENGE received");

    // ── Compute X25519 shared secret ──────────────────────────────────────────
    uint8_t peer_buf[33];
    peer_buf[0] = 32;
    memcpy(peer_buf + 1, pub_s, PUB_KEY_LEN);

    if (mbedtls_ecdh_read_public(&ecdh, peer_buf, sizeof(peer_buf)) != 0)
    {
        DXP_LOGE(TAG, "Failed to load server public key");
        goto cleanup;
    }

    uint8_t shared_secret[PUB_KEY_LEN];
    size_t shared_len = 0;
    if (mbedtls_ecdh_calc_secret(&ecdh, &shared_len, shared_secret, sizeof(shared_secret),
                                 mbedtls_ctr_drbg_random, &ctr_drbg) != 0)
    {
        DXP_LOGE(TAG, "X25519 shared secret failed");
        goto cleanup;
    }
    snprintf(_msg, sizeof(_msg), "X25519 shared secret computed (%d bytes)", (int)shared_len);
    DXP_LOGI(TAG, _msg);

    // ── Derive session key ────────────────────────────────────────────────────
    if (derive_session_key(shared_secret, shared_len,
                           nonce, DXP_NONCE_LEN,
                           pub_c, pub_s,
                           session->session_key, DXP_SESSION_KEY_LEN) != 0)
    {
        DXP_LOGE(TAG, "HKDF failed");
        goto cleanup;
    }
    DXP_LOGI(TAG, "Session key derived");

    // ── Step 3: Send HELLO_ACK [hmac_client(32)] ─────────────────────────────
    uint8_t hmac_client[DXP_HMAC_LEN];
    {
        uint8_t input[DXP_NONCE_LEN + DXP_DEVICE_ID_LEN];
        memcpy(input, nonce, DXP_NONCE_LEN);
        memcpy(input + DXP_NONCE_LEN, session->device_id, DXP_DEVICE_ID_LEN);
        if (compute_hmac(psk, psk_len, input, sizeof(input), hmac_client) != 0)
        {
            DXP_LOGE(TAG, "Client HMAC computation failed");
            goto cleanup;
        }
    }

    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = DXP_MAGIC;
    pkt.version = 1;
    pkt.type = DXP_TYPE_HELLO_ACK;
    pkt.seq = 0;
    pkt.length = DXP_HMAC_LEN;
    memcpy(pkt.payload, hmac_client, DXP_HMAC_LEN);

    len = dxp_build_packet(&pkt, tx_buf, NULL);
    if (dxp_tcp_send(sock, tx_buf, len) < 0)
    {
        DXP_LOGE(TAG, "Failed to send HELLO_ACK");
        goto cleanup;
    }
    DXP_LOGI(TAG, "HELLO_ACK sent");

    // ── Step 4: Receive SESSION_OK [session_id(4) || hmac_server(32)] ────────
    len = dxp_tcp_recv(sock, rx_buf, sizeof(rx_buf));
    if (len < 0)
    {
        DXP_LOGE(TAG, "Failed to receive SESSION_OK");
        goto cleanup;
    }

    if (dxp_parse_packet(rx_buf, len, &pkt) != 0 ||
        pkt.type != DXP_TYPE_SESSION_OK ||
        pkt.length != DXP_SESSION_ID_LEN + DXP_HMAC_LEN)
    {
        snprintf(_msg, sizeof(_msg),
                 "Bad SESSION_OK frame (type=0x%02x len=%u)", pkt.type, pkt.length);
        DXP_LOGE(TAG, _msg);
        goto cleanup;
    }

    uint8_t hmac_server_received[DXP_HMAC_LEN];
    memcpy(session->session_id, pkt.payload, DXP_SESSION_ID_LEN);
    memcpy(hmac_server_received, pkt.payload + DXP_SESSION_ID_LEN, DXP_HMAC_LEN);

    // ── Verify server HMAC ────────────────────────────────────────────────────
    uint8_t hmac_server_expected[DXP_HMAC_LEN];
    {
        uint8_t input[DXP_NONCE_LEN + DXP_SESSION_ID_LEN];
        memcpy(input, nonce, DXP_NONCE_LEN);
        memcpy(input + DXP_NONCE_LEN, session->session_id, DXP_SESSION_ID_LEN);
        if (compute_hmac(psk, psk_len, input, sizeof(input), hmac_server_expected) != 0)
        {
            DXP_LOGE(TAG, "Server HMAC computation failed");
            goto cleanup;
        }
    }

    if (mbedtls_ct_memcmp(hmac_server_received, hmac_server_expected, DXP_HMAC_LEN) != 0)
    {
        DXP_LOGE(TAG, "Server HMAC verification FAILED — possible rogue server");
        goto cleanup;
    }

    DXP_LOGI(TAG, "Server authenticated OK");
    session->established = true;
    ret = 0;

    snprintf(_msg, sizeof(_msg), "SESSION_OK — session_id: %02x%02x%02x%02x",
             session->session_id[0], session->session_id[1],
             session->session_id[2], session->session_id[3]);
    DXP_LOGI(TAG, _msg);

cleanup:
    mbedtls_ecdh_free(&ecdh);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return ret;
}