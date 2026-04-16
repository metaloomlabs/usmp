#include "dxp_frame.h"
#include <string.h>

uint16_t dxp_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// Build packet into out buffer, sets out_len to actual bytes written
// Header is 10 bytes (before crc) + 2 bytes crc + payload
int dxp_build_packet(dxp_packet_t *pkt, uint8_t *out, uint16_t *out_len)
{
    // Compute CRC over header fields (excluding crc field) + payload
    // Header without crc: magic(2)+ver(1)+type(1)+seq(4)+len(2) = 10 bytes
    uint8_t header_no_crc[10];
    header_no_crc[0] = pkt->magic & 0xFF;
    header_no_crc[1] = (pkt->magic >> 8) & 0xFF;
    header_no_crc[2] = pkt->version;
    header_no_crc[3] = pkt->type;
    header_no_crc[4] = pkt->seq & 0xFF;
    header_no_crc[5] = (pkt->seq >> 8) & 0xFF;
    header_no_crc[6] = (pkt->seq >> 16) & 0xFF;
    header_no_crc[7] = (pkt->seq >> 24) & 0xFF;
    header_no_crc[8] = pkt->length & 0xFF;
    header_no_crc[9] = (pkt->length >> 8) & 0xFF;

    uint16_t crc = dxp_crc16(header_no_crc, sizeof(header_no_crc));
    crc = dxp_crc16(pkt->payload, pkt->length); // chain over payload
    pkt->crc = crc;

    // Write full frame to out
    uint16_t total = DXP_HEADER_SIZE + pkt->length;
    memcpy(out, pkt, DXP_HEADER_SIZE);                        // header
    memcpy(out + DXP_HEADER_SIZE, pkt->payload, pkt->length); // payload only

    if (out_len)
        *out_len = total;
    return total;
}

int dxp_parse_packet(uint8_t *data, int len, dxp_packet_t *pkt)
{
    if (len < DXP_HEADER_SIZE)
        return -1;

    // Parse header fields manually (packed, little-endian)
    pkt->magic = data[0] | (data[1] << 8);
    pkt->version = data[2];
    pkt->type = data[3];
    pkt->seq = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    pkt->length = data[8] | (data[9] << 8);
    pkt->crc = data[10] | (data[11] << 8);

    if (pkt->magic != DXP_MAGIC)
        return -1;

    if (len < DXP_HEADER_SIZE + pkt->length)
        return -1;

    if (pkt->length > DXP_MAX_PAYLOAD)
        return -1;

    memcpy(pkt->payload, data + DXP_HEADER_SIZE, pkt->length);
    return 0;
}