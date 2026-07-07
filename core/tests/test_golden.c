#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "golden_vectors.h"
#include "mbedtls/md.h"
#include "usmp_crypto.h"
#include "usmp_frame.h"

void test_golden(void) {
  printf("[TEST] Running golden vector tests...\n");

  // 1. CRC Cases
  for (size_t i = 0; i < crc_cases_count; i++) {
    uint16_t computed = usmp_crc16(crc_cases[i].input, (uint16_t)crc_cases[i].input_len);
    assert(computed == crc_cases[i].expected_crc);
  }
  printf("  - CRC cases passed (%zu)\n", crc_cases_count);

  // 2. Nonce Cases (derived via seq & session_id)
  for (size_t i = 0; i < nonce_cases_count; i++) {
    uint8_t expected_nonce[USMP_GCM_NONCE_LEN];
    expected_nonce[0] = (uint8_t)(nonce_cases[i].seq & 0xFF);
    expected_nonce[1] = (uint8_t)((nonce_cases[i].seq >> 8) & 0xFF);
    expected_nonce[2] = (uint8_t)((nonce_cases[i].seq >> 16) & 0xFF);
    expected_nonce[3] = (uint8_t)((nonce_cases[i].seq >> 24) & 0xFF);
    memcpy(expected_nonce + 4, nonce_cases[i].session_id, 8);

    assert(memcmp(expected_nonce, nonce_cases[i].expected_nonce, USMP_GCM_NONCE_LEN) == 0);
  }
  printf("  - Nonce cases passed (%zu)\n", nonce_cases_count);

  // 3. Encryption / Decryption Cases
  for (size_t i = 0; i < encryption_cases_count; i++) {
    const encryption_case_t* c = &encryption_cases[i];

    uint8_t out[1024];
    size_t out_len = 0;

    int ret = usmp_gcm_encrypt(c->key, c->expected_nonce, c->expected_aad, 10, c->plaintext,
                               c->plaintext_len, out, &out_len);
    assert(ret == 0);
    assert(out_len == c->expected_ciphertext_tag_len);
    assert(memcmp(out, c->expected_ciphertext_tag, out_len) == 0);

    uint8_t dec[1024];
    size_t dec_len = 0;
    ret =
        usmp_gcm_decrypt(c->key, c->expected_nonce, c->expected_aad, 10, c->expected_ciphertext_tag,
                         c->expected_ciphertext_tag_len, dec, &dec_len);
    assert(ret == 0);
    assert(dec_len == c->plaintext_len);
    if (dec_len > 0) {
      assert(memcmp(dec, c->plaintext, dec_len) == 0);
    }
  }
  printf("  - Encryption/Decryption cases passed (%zu)\n", encryption_cases_count);

  // 4. Frame encoding and parsing cases
  for (size_t i = 0; i < frame_cases_count; i++) {
    const frame_case_t* c = &frame_cases[i];

    usmp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = c->magic;
    pkt.version = c->version;
    pkt.type = c->type;
    pkt.seq = c->seq;
    pkt.length = (uint16_t)c->payload_len;
    memcpy(pkt.payload, c->payload, c->payload_len);

    uint8_t out[1024];
    uint16_t out_len = 0;
    int ret = usmp_build_packet(&pkt, out, &out_len);
    assert(ret > 0);
    assert((size_t)out_len == c->expected_frame_len);
    assert(pkt.crc == c->expected_crc);
    assert(memcmp(out, c->expected_frame, out_len) == 0);

    usmp_packet_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    ret = usmp_parse_packet(out, out_len, &parsed);
    assert(ret == 0);
    assert(parsed.magic == c->magic);
    assert(parsed.version == c->version);
    assert(parsed.type == c->type);
    assert(parsed.seq == c->seq);
    assert(parsed.length == c->payload_len);
    assert(memcmp(parsed.payload, c->payload, c->payload_len) == 0);
  }
  printf("  - Frame building/parsing cases passed (%zu)\n", frame_cases_count);

  // 5. S3 UTACK-MAC golden vector. The 8-byte truncated HMAC-SHA256 over a UTACK's
  //    7-byte header must match the Python stack byte-for-byte (see the same vector in
  //    sdk/python tests, test_udp_utack_authentication_s3), or C<->Python UDP ACKs break.
  {
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i + 1);
    const uint8_t header[7] = {0xAC, 0xAC, 0x05, 0x07, 0x00, 0x00, 0x00};
    const uint8_t expected_mac[8] = {0x40, 0x79, 0xB9, 0x59, 0x9A, 0x16, 0x0B, 0x87};

    uint8_t full[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    assert(info != NULL);
    assert(mbedtls_md_hmac(info, key, 32, header, sizeof(header), full) == 0);
    assert(memcmp(full, expected_mac, 8) == 0);
  }
  printf("  - S3 UTACK-MAC golden vector passed\n");

  printf("[TEST] Golden vector tests completed successfully!\n");
}
