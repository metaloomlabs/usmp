#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Magic ─────────────────────────────────────────────────────────────────────
#define DXP_MAGIC        0xABCD
#define DXP_VERSION      0x01

// ── Packet types ──────────────────────────────────────────────────────────────
#define DXP_TYPE_HELLO      0x01
#define DXP_TYPE_CHALLENGE  0x02
#define DXP_TYPE_HELLO_ACK  0x03
#define DXP_TYPE_SESSION_OK 0x04
#define DXP_TYPE_DATA       0x05
#define DXP_TYPE_PING       0x06
#define DXP_TYPE_PONG       0x07
#define DXP_TYPE_BYE        0x08
#define DXP_TYPE_ERROR      0xFF

// ── Frame sizes ───────────────────────────────────────────────────────────────
#define DXP_HEADER_SIZE  12
#define DXP_MAX_PAYLOAD  480    // matches Python SDK
#define DXP_GCM_TAG_LEN  16

// ── Packet struct ─────────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint32_t seq;
    uint16_t length;
    uint16_t crc;
    uint8_t  payload[DXP_MAX_PAYLOAD];
} dxp_packet_t;

// ── Functions ─────────────────────────────────────────────────────────────────
uint16_t dxp_crc16(const uint8_t *data, uint16_t len);
int      dxp_build_packet(dxp_packet_t *pkt, uint8_t *out, uint16_t *out_len);
int      dxp_parse_packet(uint8_t *data, int len, dxp_packet_t *pkt);

#ifdef __cplusplus
}
#endif