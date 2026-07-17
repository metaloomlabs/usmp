#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Magic
#define USMP_MAGIC 0xABCD
#define USMP_VERSION 0x02

// Packet types
#define USMP_TYPE_HELLO 0x01
#define USMP_TYPE_CHALLENGE 0x02
#define USMP_TYPE_HELLO_ACK 0x03
#define USMP_TYPE_SESSION_OK 0x04
#define USMP_TYPE_DATA 0x05
#define USMP_TYPE_PING 0x06
#define USMP_TYPE_PONG 0x07
#define USMP_TYPE_BYE 0x08
#define USMP_TYPE_DATA_FRAG 0x09
#define USMP_TYPE_HELLO_RETRY 0x0A
#define USMP_TYPE_ERROR 0xFF  // Unused (reserved for future error reporting)

// Frame sizes
#define USMP_HEADER_SIZE 12
#define USMP_MAX_PAYLOAD 480  // matches Python SDK
#define USMP_MAX_FRAMES 4
#define USMP_GCM_TAG_LEN 16

// Packet struct
typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t seq;
  uint16_t length;
  uint16_t crc;
  uint8_t payload[USMP_MAX_PAYLOAD];
} usmp_packet_t;

// Functions
uint16_t usmp_crc16(const uint8_t* data, uint16_t len);
int usmp_build_packet(usmp_packet_t* pkt, uint8_t* out, uint16_t* out_len);
int usmp_parse_packet(uint8_t* data, int len, usmp_packet_t* pkt);

/*
 * Serialize the 10-byte little-endian frame header (all fields except the
 * trailing CRC) into out[0..9]. This is the single source of truth for the
 * on-wire header layout — CRC input, build_packet output, and the AES-GCM AAD
 * all derive from it, so they can never drift apart.
 */
void usmp_serialize_header(uint16_t magic, uint8_t version, uint8_t type, uint32_t seq,
                           uint16_t length, uint8_t out[10]);

#ifdef __cplusplus
}
#endif