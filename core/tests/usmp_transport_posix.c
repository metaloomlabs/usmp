#include "usmp_transport.h"
#include "usmp_frame.h"
#include "usmp_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
typedef int socklen_t;
static void socket_init(void) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
}
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
static void socket_init(void) {}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// TCP Transport
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
  int sock;
  char server_ip[64];
  int port;
} posix_tcp_ctx_t;

static int tcp_dial(posix_tcp_ctx_t* tcp) {
  int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return -1;

  int flag = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));

  // Set receive timeout to 500ms
  struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)tcp->port);

  if (inet_pton(AF_INET, tcp->server_ip, &addr.sin_addr) != 1) {
    close(sock);
    return -1;
  }

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    close(sock);
    return -1;
  }

  tcp->sock = sock;
  return 0;
}

static int posix_tcp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)t->ctx;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(tcp->sock, (const char*)data + sent, (int)(len - sent), 0);
    if (n < 0) return -1;
    sent += (size_t)n;
  }
  return 0;
}

static int posix_tcp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)t->ctx;

  if (max_len < USMP_HEADER_SIZE) return -1;
  size_t received = 0;
  while (received < USMP_HEADER_SIZE) {
    ssize_t n = recv(tcp->sock, (char*)buf + received, (int)(USMP_HEADER_SIZE - received), 0);
    if (n <= 0) return -1;
    received += (size_t)n;
  }

  uint16_t payload_len = (uint16_t)(buf[8] | (buf[9] << 8));
  if (payload_len > USMP_MAX_PAYLOAD) return -1;
  if (USMP_HEADER_SIZE + payload_len > max_len) return -1;

  while (received < USMP_HEADER_SIZE + payload_len) {
    ssize_t n = recv(tcp->sock, (char*)buf + received, (int)(USMP_HEADER_SIZE + payload_len - received), 0);
    if (n <= 0) return -1;
    received += (size_t)n;
  }

  return (int)received;
}

static void posix_tcp_close(usmp_transport_t* t) {
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)t->ctx;
  if (tcp && tcp->sock >= 0) {
    close(tcp->sock);
    tcp->sock = -1;
  }
}

static int posix_tcp_reconnect(usmp_transport_t* t) {
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)t->ctx;
  posix_tcp_close(t);
  usmp_port_delay_ms(100);
  return tcp_dial(tcp);
}

static int posix_tcp_available(usmp_transport_t* t) {
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)t->ctx;
  if (!tcp || tcp->sock < 0) return 0;
  
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET((unsigned int)tcp->sock, &rfds);
  struct timeval tv = {0, 0};
  int ret = select(tcp->sock + 1, &rfds, NULL, NULL, &tv);
  return (ret > 0) ? 1 : 0;
}

static void posix_tcp_destroy(usmp_transport_t* t) {
  posix_tcp_close(t);
  free(t->ctx);
  t->ctx = NULL;
}

int usmp_transport_tcp_init(usmp_transport_t* t, const char* server_ip, int port) {
  socket_init();
  posix_tcp_ctx_t* tcp = (posix_tcp_ctx_t*)malloc(sizeof(posix_tcp_ctx_t));
  if (!tcp) return -1;

  tcp->sock = -1;
  tcp->port = port;
  strncpy(tcp->server_ip, server_ip, sizeof(tcp->server_ip) - 1);
  tcp->server_ip[sizeof(tcp->server_ip) - 1] = '\0';

  if (tcp_dial(tcp) != 0) {
    free(tcp);
    return -1;
  }

  t->send = posix_tcp_send;
  t->recv = posix_tcp_recv;
  t->close = posix_tcp_close;
  t->reconnect = posix_tcp_reconnect;
  t->available = posix_tcp_available;
  t->destroy = posix_tcp_destroy;
  t->confirm_authenticated = NULL;
  t->ctx = tcp;

  return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP Transport (with Stop-and-Wait ARQ)
// ─────────────────────────────────────────────────────────────────────────────

#define UTACK_MAGIC 0xACAC

typedef struct {
  int sock;
  char server_ip[64];
  int port;
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int rx_len;
  uint32_t last_rx_seq;
  bool last_rx_seq_set;
  uint8_t last_rx_type;
} posix_udp_ctx_t;

static int udp_dial(posix_udp_ctx_t* udp) {
  int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return -1;

  // Set receive timeout to 10ms
  struct timeval tv = {.tv_sec = 0, .tv_usec = 10000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)udp->port);

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
#ifdef _WIN32
  return err == WSAEWOULDBLOCK || err == WSAEINTR || err == WSAECONNRESET;
#else
  return err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == ECONNRESET;
#endif
}

static int posix_udp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
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
    send(udp->sock, (const char*)data, (int)len, 0);
    return 0;
  }

  // Stop-and-wait ARQ
  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  for (int attempt = 0; attempt < 5; attempt++) {
    send(udp->sock, (const char*)data, (int)len, 0);

    uint32_t start_ms = usmp_port_millis();
    while (usmp_port_millis() - start_ms < 500) {
      ssize_t n = recv(udp->sock, (char*)temp, sizeof(temp), 0);
      if (n < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        if (is_transient_error(err)) {
          usmp_port_delay_ms(5);
          continue;
        }
        return -1;
      }

      if (n >= 7 && temp[0] == 0xAC && temp[1] == 0xAC) {
        uint8_t ack_type = temp[2];
        uint32_t ack_seq = temp[3] | (temp[4] << 8) | (temp[5] << 16) | (temp[6] << 24);
        if (ack_type == type && ack_seq == seq) {
          return 0; // ACK matched
        }
        continue;
      }
      
      // Buffer other data packet
      if (n >= USMP_HEADER_SIZE) {
        memcpy(udp->rx_buf, temp, (size_t)n);
        udp->rx_len = (int)n;
      }
    }
  }
  
  return -1; // timed out waiting for ACK
}

static int posix_udp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return -1;

  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  ssize_t n = 0;

  while (1) {
    if (udp->rx_len > 0) {
      memcpy(temp, udp->rx_buf, (size_t)udp->rx_len);
      n = udp->rx_len;
      udp->rx_len = 0;
    } else {
      n = recv(udp->sock, (char*)temp, sizeof(temp), 0);
      if (n < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        if (is_transient_error(err)) {
          usmp_port_delay_ms(5);
          continue;
        }
        return -1;
      }
    }

    if (n < (ssize_t)USMP_HEADER_SIZE) continue;

    // Discard transport UTACKs
    if (temp[0] == 0xAC && temp[1] == 0xAC) continue;

    uint16_t length = (uint16_t)(temp[8] | (temp[9] << 8));
    if (n != (ssize_t)(USMP_HEADER_SIZE + length)) continue; // enforce one-frame-per-datagram

    if ((size_t)n > max_len) return -1;
    memcpy(buf, temp, (size_t)n);

    // Save info for UTACK sending on confirmation
    udp->last_rx_type = temp[3];
    udp->last_rx_seq = temp[4] | (temp[5] << 8) | (temp[6] << 16) | (temp[7] << 24);
    udp->last_rx_seq_set = true;

    return (int)n;
  }
}

static void posix_udp_close(usmp_transport_t* t) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
  if (udp && udp->sock >= 0) {
    close(udp->sock);
    udp->sock = -1;
  }
}

static int posix_udp_reconnect(usmp_transport_t* t) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
  posix_udp_close(t);
  usmp_port_delay_ms(100);
  return udp_dial(udp);
}

static int posix_udp_available(usmp_transport_t* t) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return 0;
  if (udp->rx_len > 0) return 1;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET((unsigned int)udp->sock, &rfds);
  struct timeval tv = {0, 0};
  int ret = select(udp->sock + 1, &rfds, NULL, NULL, &tv);
  return (ret > 0) ? 1 : 0;
}

static void posix_udp_confirm_authenticated(usmp_transport_t* t, uint32_t seq) {
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)t->ctx;
  if (!udp || udp->sock < 0) return;
  if (!udp->last_rx_seq_set || udp->last_rx_seq != seq) return;

  uint8_t utack[7];
  utack[0] = 0xAC;
  utack[1] = 0xAC;
  utack[2] = udp->last_rx_type;
  utack[3] = seq & 0xFF;
  utack[4] = (seq >> 8) & 0xFF;
  utack[5] = (seq >> 16) & 0xFF;
  utack[6] = (seq >> 24) & 0xFF;

  send(udp->sock, (const char*)utack, sizeof(utack), 0);
  udp->last_rx_seq_set = false;
}

static void posix_udp_destroy(usmp_transport_t* t) {
  posix_udp_close(t);
  free(t->ctx);
  t->ctx = NULL;
}

int usmp_transport_udp_init(usmp_transport_t* t, const char* server_ip, int port) {
  socket_init();
  posix_udp_ctx_t* udp = (posix_udp_ctx_t*)malloc(sizeof(posix_udp_ctx_t));
  if (!udp) return -1;

  memset(udp, 0, sizeof(posix_udp_ctx_t));
  udp->sock = -1;
  udp->port = port;
  strncpy(udp->server_ip, server_ip, sizeof(udp->server_ip) - 1);
  udp->server_ip[sizeof(udp->server_ip) - 1] = '\0';

  if (udp_dial(udp) != 0) {
    free(udp);
    return -1;
  }

  t->send = posix_udp_send;
  t->recv = posix_udp_recv;
  t->close = posix_udp_close;
  t->reconnect = posix_udp_reconnect;
  t->available = posix_udp_available;
  t->confirm_authenticated = posix_udp_confirm_authenticated;
  t->destroy = posix_udp_destroy;
  t->ctx = udp;

  return 0;
}
