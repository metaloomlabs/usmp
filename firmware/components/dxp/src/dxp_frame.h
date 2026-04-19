#pragma once
#include <stdint.h>

#define DXP_MAGIC 0xABCD

// Packet types
#define DXP_TYPE_HELLO      0x01
#define DXP_TYPE_CHALLENGE  0x02
#define DXP_TYPE_HELLO_ACK  0x03
#define DXP_TYPE_SESSION_OK 0x04
#define DXP_TYPE_DATA       0x05
#define DXP_TYPE_PING       0x06
#define DXP_TYPE_PONG       0x07

// Pre-shared key - TODO: move to secure config before production
#define DXP_PSK "dxp-dev-psk-change-me-before-prod"

// Frame sizes
#define DXP_HEADER_SIZE  12   // magic(2) + ver(1) + type(1) + seq(4) + len(2) + crc(2)
#define DXP_TAG_LEN      16   // AES-GCM tag
#define DXP_MAX_PAYLOAD  512  // max ciphertext + tag

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint32_t seq;
    uint16_t length;   // length of payload field (ciphertext + tag for encrypted frames)
    uint16_t crc;      // CRC-16 over header excluding crc field + plaintext payload
    uint8_t  payload[DXP_MAX_PAYLOAD];
} dxp_packet_t;

uint16_t dxp_crc16(const uint8_t *data, uint16_t len);
int      dxp_build_packet(dxp_packet_t *pkt, uint8_t *out, uint16_t *out_len);
int      dxp_parse_packet(uint8_t *data, int len, dxp_packet_t *pkt);