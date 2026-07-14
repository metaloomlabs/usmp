#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "usmp_frame.h"
#include "usmp_port.h"
#include "usmp_transport.h"

// ── TCP context
// ─────────────────────────────────────────────────────────────── Allocated
// once in usmp_transport_tcp_init, lives for the lifetime of the transport. Not
// freed on close — allows reconnect without losing ip/port.
typedef struct {
  int sock;
  char server_ip[64];
  int port;
  bool session_active;
} usmp_tcp_ctx_t;

// ── Internal helpers
// ──────────────────────────────────────────────────────────

static int tcp_dial(usmp_tcp_ctx_t* tcp) {
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", tcp->port);

  if (getaddrinfo(tcp->server_ip, port_str, &hints, &res) != 0) {
    return -1;
  }

  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(res);
    return -1;
  }

  int flag = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
    freeaddrinfo(res);
    close(sock);
    return -1;
  }

  freeaddrinfo(res);
  tcp->sock = sock;
  return 0;
}

// ── Transport hooks
// ───────────────────────────────────────────────────────────

static int usmp_tcp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(tcp->sock, data + sent, len - sent, 0);
    if (n < 0) return -1;
    sent += (size_t)n;
  }
  return 0;
}

#ifndef USMP_TCP_RECV_TIMEOUT_MS
#define USMP_TCP_RECV_TIMEOUT_MS 2000
#endif

static int usmp_tcp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  if (!tcp || tcp->sock < 0) return -1;

  // Step 1: read header exactly
  if (max_len < USMP_HEADER_SIZE) return -1;
  size_t received = 0;
  uint32_t last_progress = usmp_port_millis();
  const bool bounded = tcp->session_active;

  while (received < USMP_HEADER_SIZE) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(tcp->sock, &rfds);

    struct timeval tv;
    struct timeval* p_tv = NULL;

    if (bounded) {
      uint32_t now = usmp_port_millis();
      uint32_t elapsed = now - last_progress;
      if (elapsed >= USMP_TCP_RECV_TIMEOUT_MS) {
        // Stall before any byte is a non-fatal empty read; stall after partial
        // bytes were consumed forces a resync (we can't un-read a TCP stream).
        return received == 0 ? 0 : -1;
      }
      uint32_t remaining = USMP_TCP_RECV_TIMEOUT_MS - elapsed;
      tv.tv_sec = (long)(remaining / 1000);
      tv.tv_usec = (long)((remaining % 1000) * 1000);
      p_tv = &tv;
    }

    int ret = select(tcp->sock + 1, &rfds, NULL, NULL, p_tv);
    if (ret < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (ret == 0) {
      if (bounded) {
        return received == 0 ? 0 : -1;
      }
      continue;
    }

    ssize_t n = recv(tcp->sock, buf + received, USMP_HEADER_SIZE - received, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (n == 0) {
      return -1; // Connection closed
    }
    received += (size_t)n;
    last_progress = usmp_port_millis();
  }

  // Step 2: parse payload length from header
  uint16_t payload_len = (uint16_t)(buf[8] | (buf[9] << 8));
  if (payload_len > USMP_MAX_PAYLOAD) return -1;
  if (USMP_HEADER_SIZE + payload_len > max_len) return -1;

  // Step 3: read payload exactly
  while (received < USMP_HEADER_SIZE + payload_len) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(tcp->sock, &rfds);

    struct timeval tv;
    struct timeval* p_tv = NULL;

    if (bounded) {
      uint32_t now = usmp_port_millis();
      uint32_t elapsed = now - last_progress;
      if (elapsed >= USMP_TCP_RECV_TIMEOUT_MS) {
        return -1; // stalled mid-frame (fatal)
      }
      uint32_t remaining = USMP_TCP_RECV_TIMEOUT_MS - elapsed;
      tv.tv_sec = (long)(remaining / 1000);
      tv.tv_usec = (long)((remaining % 1000) * 1000);
      p_tv = &tv;
    }

    int ret = select(tcp->sock + 1, &rfds, NULL, NULL, p_tv);
    if (ret < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (ret == 0) {
      return -1; // stalled mid-frame (fatal)
    }

    ssize_t n = recv(tcp->sock, buf + received, USMP_HEADER_SIZE + payload_len - received, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (n == 0) {
      return -1; // Connection closed
    }
    received += (size_t)n;
    last_progress = usmp_port_millis();
  }

  return (int)received;
}

static void usmp_tcp_close(usmp_transport_t* t) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  if (tcp && tcp->sock >= 0) {
    close(tcp->sock);
    tcp->sock = -1;
  }
  // ctx intentionally NOT freed — ip/port retained for reconnect
}

static void usmp_tcp_destroy(usmp_transport_t* t) {
  if (t && t->ctx) {
    usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
    if (tcp->sock >= 0) {
      close(tcp->sock);
    }
    free(tcp);
    t->ctx = NULL;
  }
}

static int usmp_tcp_reconnect(usmp_transport_t* t) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  if (!tcp) return -1;

  // Close existing socket if still open
  if (tcp->sock >= 0) {
    close(tcp->sock);
    tcp->sock = -1;
  }

  tcp->session_active = false;
  return tcp_dial(tcp);
}

static int usmp_tcp_available(usmp_transport_t* t) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  if (!tcp || tcp->sock < 0) return 0;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(tcp->sock, &rfds);

  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};

  int ret = select(tcp->sock + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0) {
    return 0;
  }
  return (ret > 0 && FD_ISSET(tcp->sock, &rfds)) ? 1 : 0;
}

// ── Factory
// ───────────────────────────────────────────────────────────────────

static void usmp_tcp_set_session_keys(usmp_transport_t* t, const uint8_t* tx_key,
                                      const uint8_t* rx_key) {
  (void)tx_key;
  (void)rx_key;
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)t->ctx;
  if (tcp) {
    tcp->session_active = true;
  }
}

int usmp_transport_tcp_init(usmp_transport_t* t, const char* server_ip, int port) {
  usmp_tcp_ctx_t* tcp = (usmp_tcp_ctx_t*)malloc(sizeof(usmp_tcp_ctx_t));
  if (!tcp) return -1;

  tcp->sock = -1;
  tcp->port = port;
  tcp->session_active = false;
  strncpy(tcp->server_ip, server_ip, sizeof(tcp->server_ip) - 1);
  tcp->server_ip[sizeof(tcp->server_ip) - 1] = '\0';

  if (tcp_dial(tcp) != 0) {
    free(tcp);
    return -1;
  }

  t->send = usmp_tcp_send;
  t->recv = usmp_tcp_recv;
  t->close = usmp_tcp_close;
  t->reconnect = usmp_tcp_reconnect;
  t->available = usmp_tcp_available;
  t->destroy = usmp_tcp_destroy;
  t->confirm_authenticated = NULL;
  t->set_session_keys = usmp_tcp_set_session_keys;
  t->ctx = tcp;

  return 0;
}