#include "usmp_frame.h"
#include <string.h>

uint16_t usmp_crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
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

// CRC over header bytes [0..9] + payload (matches Python SDK)
static uint16_t compute_crc(usmp_packet_t *pkt) {
  uint8_t header[10];
  header[0] = pkt->magic & 0xFF;
  header[1] = (pkt->magic >> 8) & 0xFF;
  header[2] = pkt->version;
  header[3] = pkt->type;
  header[4] = pkt->seq & 0xFF;
  header[5] = (pkt->seq >> 8) & 0xFF;
  header[6] = (pkt->seq >> 16) & 0xFF;
  header[7] = (pkt->seq >> 24) & 0xFF;
  header[8] = pkt->length & 0xFF;
  header[9] = (pkt->length >> 8) & 0xFF;

  // Single pass: init CRC, run over header, continue over payload
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < 10; i++) {
    crc ^= header[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  for (uint16_t i = 0; i < pkt->length; i++) {
    crc ^= pkt->payload[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)

        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

int usmp_build_packet(usmp_packet_t *pkt, uint8_t *out, uint16_t *out_len) {
  pkt->crc = compute_crc(pkt);

  // Write header manually (packed, little-endian)
  out[0] = pkt->magic & 0xFF;
  out[1] = (pkt->magic >> 8) & 0xFF;
  out[2] = pkt->version;
  out[3] = pkt->type;
  out[4] = pkt->seq & 0xFF;
  out[5] = (pkt->seq >> 8) & 0xFF;
  out[6] = (pkt->seq >> 16) & 0xFF;
  out[7] = (pkt->seq >> 24) & 0xFF;
  out[8] = pkt->length & 0xFF;
  out[9] = (pkt->length >> 8) & 0xFF;
  out[10] = pkt->crc & 0xFF;
  out[11] = (pkt->crc >> 8) & 0xFF;

  memcpy(out + USMP_HEADER_SIZE, pkt->payload, pkt->length);

  uint16_t total = USMP_HEADER_SIZE + pkt->length;
  if (out_len)
    *out_len = total;
  return total;
}

int usmp_parse_packet(uint8_t *data, int len, usmp_packet_t *pkt) {
  if (len < USMP_HEADER_SIZE)
    return -1;

  pkt->magic = data[0] | (data[1] << 8);
  pkt->version = data[2];
  pkt->type = data[3];
  pkt->seq = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
  pkt->length = data[8] | (data[9] << 8);
  pkt->crc = data[10] | (data[11] << 8);

  if (pkt->magic != USMP_MAGIC)
    return -1;

  if (len < USMP_HEADER_SIZE + pkt->length)
    return -1;

  if (pkt->length > USMP_MAX_PAYLOAD)
    return -1;

  memcpy(pkt->payload, data + USMP_HEADER_SIZE, pkt->length);

  // Verify CRC
  uint16_t expected = compute_crc(pkt);
  if (pkt->crc != expected)
    return -1;

  return 0;
}