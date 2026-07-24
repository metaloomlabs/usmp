#include "usmp_frame.h"

#include <string.h>

#include "mbedtls/constant_time.h"

/* Helper to process data for CRC-16-ANSI step-by-step */
static uint16_t usmp_crc16_step(uint16_t crc, const uint8_t* data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

uint16_t usmp_crc16(const uint8_t* data, uint16_t len) {
  return usmp_crc16_step(0xFFFF, data, len);
}

void usmp_serialize_header(uint16_t magic, uint8_t version, uint8_t type, uint32_t seq,
                           uint16_t length, uint8_t out[10]) {
  out[0] = (uint8_t)(magic & 0xFF);
  out[1] = (uint8_t)((magic >> 8) & 0xFF);
  out[2] = version;
  out[3] = type;
  out[4] = (uint8_t)(seq & 0xFF);
  out[5] = (uint8_t)((seq >> 8) & 0xFF);
  out[6] = (uint8_t)((seq >> 16) & 0xFF);
  out[7] = (uint8_t)((seq >> 24) & 0xFF);
  out[8] = (uint8_t)(length & 0xFF);
  out[9] = (uint8_t)((length >> 8) & 0xFF);
}

// CRC over header bytes [0..9] + payload (matches Python SDK)
static uint16_t compute_crc(usmp_packet_t* pkt) {
  uint8_t header[10];
  usmp_serialize_header(pkt->magic, pkt->version, pkt->type, pkt->seq, pkt->length, header);

  uint16_t crc = usmp_crc16_step(0xFFFF, header, 10);
  crc = usmp_crc16_step(crc, pkt->payload, pkt->length);
  return crc;
}

int usmp_build_packet(usmp_packet_t* pkt, uint8_t* out, uint16_t* out_len) {
  pkt->crc = compute_crc(pkt);

  // Write header (packed, little-endian) followed by the trailing CRC
  usmp_serialize_header(pkt->magic, pkt->version, pkt->type, pkt->seq, pkt->length, out);
  out[10] = (uint8_t)(pkt->crc & 0xFF);
  out[11] = (uint8_t)((pkt->crc >> 8) & 0xFF);

  memcpy(out + USMP_HEADER_SIZE, pkt->payload, pkt->length);

  uint16_t total = USMP_HEADER_SIZE + pkt->length;
  if (out_len) *out_len = total;
  return total;
}

int usmp_parse_packet(uint8_t* data, int len, usmp_packet_t* pkt) {
  if (len < 0 || (size_t)len < USMP_HEADER_SIZE) return -1;

  pkt->magic = data[0] | (data[1] << 8);
  pkt->version = data[2];
  pkt->type = data[3];
  pkt->seq =
      data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
  pkt->length = data[8] | (data[9] << 8);
  pkt->crc = data[10] | (data[11] << 8);

  if (pkt->magic != USMP_MAGIC) return -1;

  if (pkt->version != USMP_VERSION) return -1;

  if (pkt->length > USMP_MAX_PAYLOAD) return -1;

  if ((size_t)len < (size_t)USMP_HEADER_SIZE + pkt->length) return -1;

  memcpy(pkt->payload, data + USMP_HEADER_SIZE, pkt->length);

  // Verify CRC (non-secret integrity check — no need for constant-time comparison)
  uint16_t expected = compute_crc(pkt);
  if (memcmp(&pkt->crc, &expected, sizeof(uint16_t)) != 0) return -1;

  return 0;
}