#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "usmp_frame.h"
#include "usmp_port.h"
#include "usmp_transport.h"

#define UTACK_MAGIC 0xACAC

typedef struct {
  int sock;
  char server_ip[64];
  int port;
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int rx_len;
  uint32_t last_rx_seq;
  bool last_rx_seq_set;
} usmp_udp_ctx_t;

static int udp_dial(usmp_udp_ctx_t* udp) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return -1;

  // Set receive timeout to 10ms for quick ACK polling
  struct timeval tv = {.tv_sec = 0, .tv_usec = 10000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(udp->port),
  };

  if (inet_pton(AF_INET, udp->server_ip, &addr.sin_addr) != 1) {
    close(sock);
    return -1;
  }

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    close(sock);
    return -1;
  }

  udp->sock = sock;
  return 0;
}

static int usmp_udp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return -1;

  uint8_t type = 0;
  uint32_t seq = 0;
  bool expect_ack = false;

  if (len >= 8) {
    type = data[3];
    seq = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    expect_ack = true;
  }

  if (!expect_ack) {
    if (send(udp->sock, data, len, 0) < 0) return -1;
    return 0;
  }

  // Stop-and-wait ARQ
  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  for (int attempt = 0; attempt < 5; attempt++) {
    if (send(udp->sock, data, len, 0) < 0) return -1;

    uint32_t start_ms = usmp_port_millis();
    while (usmp_port_millis() - start_ms < 100) {
      ssize_t n = recv(udp->sock, temp, sizeof(temp), 0);
      if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
          return -1; // Fatal socket error
        }
        usmp_port_delay_ms(5);
        continue;
      }

      // Check if it is a transport UTACK
      if (n >= 7 && temp[0] == 0xAC && temp[1] == 0xAC) {
        uint8_t ack_type = temp[2];
        uint32_t ack_seq = temp[3] | (temp[4] << 8) | (temp[5] << 16) | (temp[6] << 24);
        if (ack_type == type && ack_seq == seq) {
          return 0; // Success! ACK received
        }
        continue;
      }

      // Buffer data packet received while waiting for UTACK
      if (n >= (ssize_t)USMP_HEADER_SIZE && udp->rx_len == 0) {
        memcpy(udp->rx_buf, temp, n);
        udp->rx_len = (int)n;
      }
    }
  }

  return -1; // Retries exhausted
}

static int usmp_udp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return -1;

  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  ssize_t n = 0;

  while (1) {
    if (udp->rx_len > 0) {
      memcpy(temp, udp->rx_buf, udp->rx_len);
      n = udp->rx_len;
      udp->rx_len = 0;
    } else {
      n = recv(udp->sock, temp, sizeof(temp), 0);
      if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
          return -1; // Fatal error
        }
        usmp_port_delay_ms(5);
        continue;
      }
    }

    if (n < (ssize_t)USMP_HEADER_SIZE) continue;

    // Discard unexpected transport UTACKs
    if (temp[0] == 0xAC && temp[1] == 0xAC) {
      continue;
    }

    uint16_t magic = temp[0] | (temp[1] << 8);
    if (magic != 0xABCD) continue;

    uint8_t type = temp[3];
    uint32_t seq = temp[4] | (temp[5] << 8) | (temp[6] << 16) | (temp[7] << 24);

    // Send UTACK back immediately
    uint8_t utack[7] = {0xAC, 0xAC, type, seq & 0xFF, (seq >> 8) & 0xFF, (seq >> 16) & 0xFF, (seq >> 24) & 0xFF};
    send(udp->sock, utack, sizeof(utack), 0);

    // Duplicate detection (only for active sessions, type >= 5)
    if (type >= 5) {
      if (udp->last_rx_seq_set && seq <= udp->last_rx_seq) {
        continue; // Discard duplicate
      }
      udp->last_rx_seq = seq;
      udp->last_rx_seq_set = true;
    }

    if ((size_t)n > max_len) return -1;
    memcpy(buf, temp, n);
    return (int)n;
  }
}

static void usmp_udp_close(usmp_transport_t* t) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (udp && udp->sock >= 0) {
    close(udp->sock);
    udp->sock = -1;
  }
}

static void usmp_udp_destroy(usmp_transport_t* t) {
  if (t && t->ctx) {
    usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
    if (udp->sock >= 0) {
      close(udp->sock);
    }
    free(udp);
    t->ctx = NULL;
  }
}

static int usmp_udp_reconnect(usmp_transport_t* t) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp) return -1;

  if (udp->sock >= 0) {
    close(udp->sock);
    udp->sock = -1;
  }

  udp->rx_len = 0;
  udp->last_rx_seq_set = false;
  return udp_dial(udp);
}

static int usmp_udp_available(usmp_transport_t* t) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return 0;
  if (udp->rx_len > 0) return 1;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(udp->sock, &rfds);

  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};

  int ret = select(udp->sock + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0) {
    return 0;
  }
  return (ret > 0 && FD_ISSET(udp->sock, &rfds)) ? 1 : 0;
}

int usmp_transport_udp_init(usmp_transport_t* t, const char* server_ip, int port) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)malloc(sizeof(usmp_udp_ctx_t));
  if (!udp) return -1;

  memset(udp, 0, sizeof(usmp_udp_ctx_t));
  udp->sock = -1;
  udp->port = port;
  strncpy(udp->server_ip, server_ip, sizeof(udp->server_ip) - 1);
  udp->server_ip[sizeof(udp->server_ip) - 1] = '\0';

  if (udp_dial(udp) != 0) {
    free(udp);
    return -1;
  }

  t->send = usmp_udp_send;
  t->recv = usmp_udp_recv;
  t->close = usmp_udp_close;
  t->reconnect = usmp_udp_reconnect;
  t->available = usmp_udp_available;
  t->destroy = usmp_udp_destroy;
  t->ctx = udp;

  return 0;
}
