#include "usmp_frame.h"
#include "usmp_port.h"
#include "usmp_transport.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

// ── TCP context
// ─────────────────────────────────────────────────────────────── Allocated
// once in usmp_transport_tcp_init, lives for the lifetime of the transport. Not
// freed on close — allows reconnect without losing ip/port.
typedef struct {
  int sock;
  char server_ip[64];
  int port;
} usmp_tcp_ctx_t;

// ── Internal helpers
// ──────────────────────────────────────────────────────────

static int tcp_dial(usmp_tcp_ctx_t *tcp) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  int flag = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(tcp->port),
  };

  if (inet_pton(AF_INET, tcp->server_ip, &addr.sin_addr) != 1) {
    close(sock);
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(sock);
    return -1;
  }

  tcp->sock = sock;
  return 0;
}

// ── Transport hooks
// ───────────────────────────────────────────────────────────

static int usmp_tcp_send(usmp_transport_t *t, const uint8_t *data, size_t len) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(tcp->sock, data + sent, len - sent, 0);
    if (n < 0)
      return -1;
    sent += n;
  }
  return 0;
}

static int usmp_tcp_recv(usmp_transport_t *t, uint8_t *buf, size_t max_len) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;

  // Step 1: read header exactly
  if (max_len < USMP_HEADER_SIZE)
    return -1;
  size_t received = 0;
  while (received < USMP_HEADER_SIZE) {
    ssize_t n = recv(tcp->sock, buf + received, USMP_HEADER_SIZE - received, 0);
    if (n <= 0)
      return -1;
    received += n;
  }

  // Step 2: parse payload length from header
  uint16_t payload_len = buf[8] | (buf[9] << 8);
  if (payload_len > USMP_MAX_PAYLOAD)
    return -1;
  if (USMP_HEADER_SIZE + payload_len > max_len)
    return -1;

  // Step 3: read payload exactly
  while (received < USMP_HEADER_SIZE + payload_len) {
    ssize_t n = recv(tcp->sock, buf + received,
                     USMP_HEADER_SIZE + payload_len - received, 0);
    if (n <= 0)
      return -1;
    received += n;
  }

  return (int)received;
}

static void usmp_tcp_close(usmp_transport_t *t) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;
  if (tcp && tcp->sock >= 0) {
    close(tcp->sock);
    tcp->sock = -1;
  }
  // ctx intentionally NOT freed — ip/port retained for reconnect
}

static void usmp_tcp_destroy(usmp_transport_t *t) {
  if (t && t->ctx) {
    usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;
    if (tcp->sock >= 0) {
      close(tcp->sock);
    }
    free(tcp);
    t->ctx = NULL;
  }
}

static int usmp_tcp_reconnect(usmp_transport_t *t) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;
  if (!tcp)
    return -1;

  // Close existing socket if still open
  if (tcp->sock >= 0) {
    close(tcp->sock);
    tcp->sock = -1;
  }

  return tcp_dial(tcp);
}

static int usmp_tcp_available(usmp_transport_t *t) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)t->ctx;
  if (!tcp || tcp->sock < 0)
    return 0;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(tcp->sock, &rfds);

  struct timeval tv = {
      .tv_sec = 0,
      .tv_usec = 0
  };

  int ret = select(tcp->sock + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0) {
    return 0;
  }
  return (ret > 0 && FD_ISSET(tcp->sock, &rfds)) ? 1 : 0;
}

// ── Factory
// ───────────────────────────────────────────────────────────────────

int usmp_transport_tcp_init(usmp_transport_t *t, const char *server_ip,
                            int port) {
  usmp_tcp_ctx_t *tcp = (usmp_tcp_ctx_t *)malloc(sizeof(usmp_tcp_ctx_t));
  if (!tcp)
    return -1;

  tcp->sock = -1;
  tcp->port = port;
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
  t->ctx = tcp;

  return 0;
}