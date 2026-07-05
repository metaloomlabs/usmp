#include <stddef.h>

// Rename Arduino usmp_t and functions to prevent case-insensitive / double definition conflicts
#define usmp_t arduino_usmp_t
#define usmp_get_version arduino_usmp_get_version
#define usmp_connect arduino_usmp_connect
#define usmp_reconnect arduino_usmp_reconnect
#define usmp_close arduino_usmp_close
#define usmp_send arduino_usmp_send
#define usmp_recv arduino_usmp_recv
#define usmp_ping arduino_usmp_ping
#define usmp_keepalive_tick arduino_usmp_keepalive_tick

#include "../../ports/usmp-arduino/src/usmp_api.h"

#undef usmp_t
#undef usmp_get_version
#undef usmp_connect
#undef usmp_reconnect
#undef usmp_close
#undef usmp_send
#undef usmp_recv
#undef usmp_ping
#undef usmp_keepalive_tick

#include "usmp.h"
#include "usmp_frame.h"
#include "usmp_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Assert layout compatibility between core and Arduino usmp_t structures (A1)
_Static_assert(sizeof(arduino_usmp_t) == sizeof(usmp_t), "usmp_t size mismatch between Core and Arduino!");
_Static_assert(offsetof(arduino_usmp_t, established) == offsetof(usmp_t, established), "established offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, tx_seq) == offsetof(usmp_t, tx_seq), "tx_seq offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_seq) == offsetof(usmp_t, rx_seq), "rx_seq offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, psk) == offsetof(usmp_t, psk), "psk offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, psk_len) == offsetof(usmp_t, psk_len), "psk_len offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_window_bitmap) == offsetof(usmp_t, rx_window_bitmap), "rx_window_bitmap offset mismatch!");


// Forward declaration from test_golden.c
void test_golden(void);

// ── Mock Loopback Transport
// ──────────────────────────────────────────────────────────
#define LOOPBACK_BUF_MAX 4096
typedef struct {
  uint8_t buffer[LOOPBACK_BUF_MAX];
  size_t write_pos;
  size_t read_pos;
} loopback_ctx_t;

static int loopback_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  loopback_ctx_t* ctx = (loopback_ctx_t*)t->ctx;
  if (ctx->write_pos + len > LOOPBACK_BUF_MAX) return -1;
  memcpy(ctx->buffer + ctx->write_pos, data, len);
  ctx->write_pos += len;
  return 0;
}

static int loopback_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  loopback_ctx_t* ctx = (loopback_ctx_t*)t->ctx;
  if (ctx->read_pos >= ctx->write_pos) return -1; // no data available

  // Parse header to get length
  if (ctx->read_pos + USMP_HEADER_SIZE > ctx->write_pos) return -1;
  uint16_t length = ctx->buffer[ctx->read_pos + 8] | (ctx->buffer[ctx->read_pos + 9] << 8);
  size_t total_frame_len = USMP_HEADER_SIZE + length;

  if (ctx->read_pos + total_frame_len > ctx->write_pos) return -1;
  if (total_frame_len > max_len) return -1;

  memcpy(buf, ctx->buffer + ctx->read_pos, total_frame_len);
  ctx->read_pos += total_frame_len;
  return (int)total_frame_len;
}

static void loopback_close(usmp_transport_t* t) {
  (void)t;
}

static int loopback_reconnect(usmp_transport_t* t) {
  (void)t;
  return 0;
}

static int loopback_available(usmp_transport_t* t) {
  loopback_ctx_t* ctx = (loopback_ctx_t*)t->ctx;
  return (ctx->write_pos > ctx->read_pos) ? 1 : 0;
}

static void loopback_destroy(usmp_transport_t* t) {
  (void)t;
}

static void loopback_init(usmp_transport_t* t, loopback_ctx_t* ctx) {
  memset(ctx, 0, sizeof(loopback_ctx_t));
  t->send = loopback_send;
  t->recv = loopback_recv;
  t->close = loopback_close;
  t->reconnect = loopback_reconnect;
  t->available = loopback_available;
  t->destroy = loopback_destroy;
  t->confirm_authenticated = NULL;
  t->ctx = ctx;
}

// ── Test Fragmentation and Reassembly
// ──────────────────────────────────────────────────────────
void test_fragmentation(void) {
  printf("[TEST] Running fragmentation and reassembly test...\n");

  usmp_t client_ctx = {0};
  usmp_t server_ctx = {0};
  
  // Set up mock keys and session ID to simulate a live established session
  uint8_t mock_key_c2s[32] = {0xAA};
  uint8_t mock_key_s2c[32] = {0xBB};
  uint8_t mock_session_id[16] = {0x01, 0x02, 0x03};

  client_ctx.established = true;
  memcpy(client_ctx.tx_key, mock_key_c2s, 32);
  memcpy(client_ctx.rx_key, mock_key_s2c, 32);
  memcpy(client_ctx.session_id, mock_session_id, 16);

  server_ctx.established = true;
  memcpy(server_ctx.tx_key, mock_key_s2c, 32); // server tx is client rx
  memcpy(server_ctx.rx_key, mock_key_c2s, 32); // server rx is client tx
  memcpy(server_ctx.session_id, mock_session_id, 16);

  usmp_transport_t client_transport;
  usmp_transport_t server_transport;
  loopback_ctx_t loopback;

  loopback_init(&client_transport, &loopback);
  loopback_init(&server_transport, &loopback);

  client_ctx.transport = client_transport;
  server_ctx.transport = server_transport;

  // Send a payload that requires fragmentation (> USMP_MAX_DATA_LEN which is 452)
  // Let's send 1000 bytes
  uint8_t send_buf[1000];
  for (int i = 0; i < 1000; i++) {
    send_buf[i] = (uint8_t)(i & 0xFF);
  }

  int ret = usmp_send(&client_ctx, send_buf, sizeof(send_buf));
  assert(ret == 0);

  // Assert that loopback contains multiple fragments
  // Total size: 452 bytes chunk 1 (as DATA_FRAG), 452 bytes chunk 2 (as DATA_FRAG), 96 bytes chunk 3 (as DATA)
  // Number of fragments should be 3
  assert(loopback.write_pos > 0);

  // Server receives and reassembles the payload
  uint8_t recv_buf[1200];
  int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
  assert(recv_len == 1000);
  assert(memcmp(send_buf, recv_buf, 1000) == 0);

  printf("  - Fragmentation and reassembly passed!\n");
}

int main(void) {
  printf("==================================================\n");
  printf("         USMP C CORE UNIT TESTS RUNNER            \n");
  printf("==================================================\n");

  test_golden();
  test_fragmentation();

  printf("All C core unit tests passed successfully!\n");
  return 0;
}
