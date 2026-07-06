#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/md.h"
#include "usmp_frame.h"
#include "usmp_port.h"
#include "usmp_transport.h"

#define UTACK_MAGIC 0xACAC

// S3: session-phase UTACKs (frame type >= 5) carry an 8-byte truncated HMAC-SHA256 over
// their 7-byte header so an off-path attacker cannot forge an ACK. Handshake-phase UTACKs
// (types 1-4) predate the session keys and stay unauthenticated.
#define UTACK_HEADER_LEN 7
#define UTACK_MAC_LEN 8

typedef struct {
  int sock;
  char server_ip[64];
  int port;
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int rx_len;
  uint32_t last_rx_seq;
  bool last_rx_seq_set;
  uint8_t last_rx_type;
  uint8_t tx_key[32];  // S3: authenticates ACKs we receive (peer signs with its rx_key)
  uint8_t rx_key[32];  // S3: signs ACKs we send for frames we received
  bool keys_set;
} usmp_udp_ctx_t;

// S3: 8-byte truncated HMAC-SHA256 over the 7-byte UTACK header.
static void utack_mac(const uint8_t* key, const uint8_t* header, uint8_t out[UTACK_MAC_LEN]) {
  uint8_t full[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, 32, header, UTACK_HEADER_LEN, full);
  memcpy(out, full, UTACK_MAC_LEN);
}

static void usmp_udp_set_session_keys(usmp_transport_t* t, const uint8_t* tx_key,
                                      const uint8_t* rx_key) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp) return;
  memcpy(udp->tx_key, tx_key, 32);
  memcpy(udp->rx_key, rx_key, 32);
  udp->keys_set = true;
}

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
static int is_transient_error(int err) {
  return err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == ECONNREFUSED ||
         err == EHOSTUNREACH || err == ENETUNREACH || err == ECONNRESET;
}

static int usmp_udp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return -1;

  uint8_t type = 0;
  uint32_t seq = 0;
  bool expect_ack = false;

  if (len >= 8) {
    type = data[3];
    seq =
        data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    expect_ack = true;
  }

  if (!expect_ack) {
    send(udp->sock, data, len, 0);
    return 0;
  }

  // Stop-and-wait ARQ
  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  for (int attempt = 0; attempt < 5; attempt++) {
    send(udp->sock, data, len, 0);

    uint32_t start_ms = usmp_port_millis();
    while (usmp_port_millis() - start_ms < 500) {
      ssize_t n = recv(udp->sock, temp, sizeof(temp), 0);
      if (n < 0) {
        usmp_port_delay_ms(5);
        continue;
      }

      // Check if it is a transport UTACK
      if (n >= UTACK_HEADER_LEN && temp[0] == 0xAC && temp[1] == 0xAC) {
        uint8_t ack_type = temp[2];
        uint32_t ack_seq = temp[3] | ((uint32_t)temp[4] << 8) | ((uint32_t)temp[5] << 16) |
                           ((uint32_t)temp[6] << 24);
        if (ack_type == type && ack_seq == seq) {
          // S3: a session-phase ACK (type >= 5) must carry a valid MAC keyed by tx_key;
          // drop forged or unauthenticated ACKs so an off-path attacker can't spoof one.
          if (type >= 5) {
            if (!udp->keys_set || n < UTACK_HEADER_LEN + UTACK_MAC_LEN) continue;
            uint8_t expected[UTACK_MAC_LEN];
            utack_mac(udp->tx_key, temp, expected);
            if (mbedtls_ct_memcmp(expected, temp + UTACK_HEADER_LEN, UTACK_MAC_LEN) != 0) continue;
          }
          return 0;  // Success! ACK received (and authenticated for session frames)
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

  return -1;  // Retries exhausted
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
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(udp->sock, &rfds);
      struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
      int select_ret = select(udp->sock + 1, &rfds, NULL, NULL, &tv);
      if (select_ret == 0) {
        // Timeout. If socket has been closed / set to negative by another thread, exit
        if (udp->sock < 0) return -1;
        continue;
      }
      if (select_ret < 0) {
        if (errno == EINTR) continue;
        return -1;
      }
      n = recv(udp->sock, temp, sizeof(temp), 0);
      if (n < 0) {
        if (!is_transient_error(errno)) {
          return -1;  // Truly fatal error (e.g. EBADF, ENOTSOCK)
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
    uint32_t seq =
        temp[4] | ((uint32_t)temp[5] << 8) | ((uint32_t)temp[6] << 16) | ((uint32_t)temp[7] << 24);

    // Send UTACK back immediately. S3: authenticate session-phase UTACKs (type >= 5)
    // with rx_key once keys are established; handshake UTACKs stay plaintext.
    uint8_t utack[UTACK_HEADER_LEN + UTACK_MAC_LEN] = {
        0xAC, 0xAC, type, seq & 0xFF, (seq >> 8) & 0xFF, (seq >> 16) & 0xFF, (seq >> 24) & 0xFF};
    size_t utack_len = UTACK_HEADER_LEN;
    if (type >= 5 && udp->keys_set) {
      utack_mac(udp->rx_key, utack, utack + UTACK_HEADER_LEN);
      utack_len = UTACK_HEADER_LEN + UTACK_MAC_LEN;
    }
    send(udp->sock, utack, utack_len, 0);

    // Duplicate detection
    if (type < 5) {
      if (udp->last_rx_type > 0 && type <= udp->last_rx_type) {
        continue;  // Discard duplicate/old handshake packet
      }
      udp->last_rx_type = type;
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
  udp->last_rx_type = 0;
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

static void usmp_udp_confirm_authenticated(usmp_transport_t* t, uint32_t seq) {
  usmp_udp_ctx_t* udp = (usmp_udp_ctx_t*)t->ctx;
  if (udp) {
    udp->last_rx_seq = seq;
    udp->last_rx_seq_set = true;
  }
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
  t->confirm_authenticated = usmp_udp_confirm_authenticated;
  t->set_session_keys = usmp_udp_set_session_keys;
  t->ctx = udp;

  return 0;
}
