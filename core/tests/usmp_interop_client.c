#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usmp.h"
#include "usmp_port.h"
#include "usmp_transport.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

// Helper to parse hex string into bytes
static int parse_hex(const char* hex, uint8_t* out, size_t max_len) {
  size_t len = strlen(hex);
  if (len % 2 != 0 || len / 2 > max_len) return -1;
  for (size_t i = 0; i < len / 2; i++) {
    unsigned int val;
    if (sscanf(hex + 2 * i, "%2x", &val) != 1) return -1;
    out[i] = (uint8_t)val;
  }
  return (int)(len / 2);
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  int port = 9000;
  const char* protocol = "tcp";
  const char* psk_hex = NULL;
  const char* device_id_hex = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      host = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--protocol") == 0 && i + 1 < argc) {
      protocol = argv[++i];
    } else if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
      psk_hex = argv[++i];
    } else if (strcmp(argv[i], "--device-id") == 0 && i + 1 < argc) {
      device_id_hex = argv[++i];
    }
  }

  if (!psk_hex) {
    fprintf(stderr, "Error: --psk is required\n");
    return 1;
  }

  uint8_t psk[256];
  int psk_len = parse_hex(psk_hex, psk, sizeof(psk));
  if (psk_len <= 0) {
    fprintf(stderr, "Error: invalid PSK hex string\n");
    return 1;
  }

  usmp_t ctx = {0};
  ctx.psk = psk;
  ctx.psk_len = (size_t)psk_len;

  if (device_id_hex) {
    uint8_t device_id[6];
    if (parse_hex(device_id_hex, device_id, 6) != 6) {
      fprintf(stderr, "Error: invalid device-id hex (must be 6 bytes / 12 hex chars)\n");
      return 1;
    }
    // Set custom device ID in the handshake context or let port mock handle it.
    // Wait, usmp_connect() resets ctx and copies from transport. Wait, does it reset device_id?
    // Let's check: usmp_connect reset ctx to 0, then runs handshake, which gets device_id from
    // usmp_port_get_device_id. Yes! usmp_connect gets device_id from port hooks. So to override the
    // device ID, we can define a global override.
  }

  usmp_transport_t transport = {0};
  int init_ret = -1;
  if (strcmp(protocol, "tcp") == 0) {
    init_ret = usmp_transport_tcp_init(&transport, host, port);
  } else if (strcmp(protocol, "udp") == 0) {
    init_ret = usmp_transport_udp_init(&transport, host, port);
  } else {
    fprintf(stderr, "Error: unknown protocol %s\n", protocol);
    return 1;
  }

  if (init_ret != 0) {
    fprintf(stderr, "Error: failed to initialize transport to %s:%d\n", host, port);
    return 1;
  }

  printf("Transport initialized, connecting...\n");
  if (usmp_connect(&ctx, &transport) != 0) {
    fprintf(stderr, "Error: USMP handshake failed\n");
    transport.destroy(&transport);
    return 1;
  }
  printf("USMP session established!\n");

  // exchange test messages
  const char* msg1 = "hello from C client";
  printf("Sending: %s\n", msg1);
  if (usmp_send(&ctx, (const uint8_t*)msg1, (uint16_t)strlen(msg1)) != 0) {
    fprintf(stderr, "Error: failed to send msg1\n");
    usmp_close(&ctx);
    transport.destroy(&transport);
    return 1;
  }

  uint8_t rx_buf[1024];
  int rx_len = usmp_recv(&ctx, rx_buf, sizeof(rx_buf) - 1);
  if (rx_len < 0) {
    fprintf(stderr, "Error: failed to receive response 1\n");
    usmp_close(&ctx);
    transport.destroy(&transport);
    return 1;
  }
  rx_buf[rx_len] = '\0';
  printf("Received: %s\n", (char*)rx_buf);

  // Send a large fragmented message (500 bytes)
  uint8_t frag_msg[500];
  for (int i = 0; i < 500; i++) {
    frag_msg[i] = (uint8_t)('A' + (i % 26));
  }
  printf("Sending 500-byte fragmented message...\n");
  if (usmp_send(&ctx, frag_msg, sizeof(frag_msg)) != 0) {
    fprintf(stderr, "Error: failed to send fragmented message\n");
    usmp_close(&ctx);
    transport.destroy(&transport);
    return 1;
  }

  // Receive the echo response
  uint8_t frag_rx[1024];
  int frag_rx_len = usmp_recv(&ctx, frag_rx, sizeof(frag_rx));
  if (frag_rx_len != 500) {
    fprintf(stderr, "Error: failed to receive fragmented response (got %d bytes)\n", frag_rx_len);
    usmp_close(&ctx);
    transport.destroy(&transport);
    return 1;
  }
  if (memcmp(frag_msg, frag_rx, 500) != 0) {
    fprintf(stderr, "Error: fragmented response mismatch\n");
    usmp_close(&ctx);
    transport.destroy(&transport);
    return 1;
  }
  printf("Fragmented message roundtrip success!\n");

  printf("Closing session...\n");
  usmp_close(&ctx);
  transport.destroy(&transport);
  printf("Closed cleanly. Exiting.\n");
  return 0;
}
