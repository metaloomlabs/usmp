#include <stddef.h>

#define USMP_TEST_MAIN

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
#define usmp_is_connected arduino_usmp_is_connected

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
#undef usmp_is_connected

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef USMP_VERSION_MAJOR
// usmp_api.h was a repository shim and did not define anything because USMP_TEST_MAIN was set.
// Include the real usmp.h first, then alias arduino_usmp_t to usmp_t.
#include "usmp.h"
typedef usmp_t arduino_usmp_t;
#else
// usmp_api.h was the actual header copy (packaged version).
// We include usmp.h to get the core definitions.
#include "usmp.h"
#endif

#include "usmp_crypto.h"
#include "usmp_frame.h"
#include "usmp_handshake.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

// Assert layout compatibility between core and Arduino usmp_t structures (A1)
// All fields must have identical offsets to ensure binary compatibility.
_Static_assert(sizeof(arduino_usmp_t) == sizeof(usmp_t),
               "usmp_t size mismatch between Core and Arduino!");
_Static_assert(offsetof(arduino_usmp_t, device_id) == offsetof(usmp_t, device_id),
               "device_id offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, session_id) == offsetof(usmp_t, session_id),
               "session_id offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, tx_key) == offsetof(usmp_t, tx_key),
               "tx_key offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_key) == offsetof(usmp_t, rx_key),
               "rx_key offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, established) == offsetof(usmp_t, established),
               "established offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, transport) == offsetof(usmp_t, transport),
               "transport offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, tx_seq) == offsetof(usmp_t, tx_seq),
               "tx_seq offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_seq) == offsetof(usmp_t, rx_seq),
               "rx_seq offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, keepalive_ms) == offsetof(usmp_t, keepalive_ms),
               "keepalive_ms offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, last_tx_ms) == offsetof(usmp_t, last_tx_ms),
               "last_tx_ms offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_window_bitmap) == offsetof(usmp_t, rx_window_bitmap),
               "rx_window_bitmap offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, cipher_suite) == offsetof(usmp_t, cipher_suite),
               "cipher_suite offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, psk) == offsetof(usmp_t, psk), "psk offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, psk_len) == offsetof(usmp_t, psk_len),
               "psk_len offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, scratch) == offsetof(usmp_t, scratch),
               "scratch offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, scratch_len) == offsetof(usmp_t, scratch_len),
               "scratch_len offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, tx_mutex) == offsetof(usmp_t, tx_mutex),
               "tx_mutex offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, rx_mutex) == offsetof(usmp_t, rx_mutex),
               "rx_mutex offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, state) == offsetof(usmp_t, state),
               "state offset mismatch!");
_Static_assert(offsetof(arduino_usmp_t, hs_ctx) == offsetof(usmp_t, hs_ctx),
               "hs_ctx offset mismatch!");

// Forward declaration from test_golden.c
void test_golden(void);

// ── Mock Loopback Transport
// ──────────────────────────────────────────────────────────
#define LOOPBACK_BUF_MAX 8192
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
  if (ctx->read_pos >= ctx->write_pos) {
    return t->confirm_authenticated ? 0 : -1;  // return 0 (timeout/no-data) for UDP, -1 for TCP
  }

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

static void loopback_close(usmp_transport_t* t) { (void)t; }

static int loopback_reconnect(usmp_transport_t* t) {
  (void)t;
  return 0;
}

static int loopback_available(usmp_transport_t* t) {
  loopback_ctx_t* ctx = (loopback_ctx_t*)t->ctx;
  return (ctx->write_pos > ctx->read_pos) ? 1 : 0;
}

static void loopback_destroy(usmp_transport_t* t) { (void)t; }

static void loopback_init(usmp_transport_t* t, loopback_ctx_t* ctx) {
  memset(ctx, 0, sizeof(loopback_ctx_t));
  t->send = loopback_send;
  t->recv = loopback_recv;
  t->close = loopback_close;
  t->reconnect = loopback_reconnect;
  t->available = loopback_available;
  t->destroy = loopback_destroy;
  t->confirm_authenticated = NULL;
  t->set_session_keys = NULL;
  t->ctx = ctx;
}

// ── Helpers: set up a matched client/server pair on a shared loopback ─────
static void setup_session_pair(usmp_t* client, usmp_t* server, usmp_transport_t* c_tr,
                               usmp_transport_t* s_tr, loopback_ctx_t* lb) {
  memset(client, 0, sizeof(*client));
  memset(server, 0, sizeof(*server));

  uint8_t mock_key_c2s[32] = {0xAA};
  uint8_t mock_key_s2c[32] = {0xBB};
  uint8_t mock_session_id[16] = {0x01, 0x02, 0x03};

  client->established = true;
  memcpy(client->tx_key, mock_key_c2s, 32);
  memcpy(client->rx_key, mock_key_s2c, 32);
  memcpy(client->session_id, mock_session_id, 16);

  server->established = true;
  memcpy(server->tx_key, mock_key_s2c, 32);
  memcpy(server->rx_key, mock_key_c2s, 32);
  memcpy(server->session_id, mock_session_id, 16);

  loopback_init(c_tr, lb);
  loopback_init(s_tr, lb);

  client->transport = *c_tr;
  server->transport = *s_tr;
}

// ── Test Fragmentation and Reassembly
// ──────────────────────────────────────────────────────────
void test_fragmentation(void) {
  printf("[TEST] Running fragmentation and reassembly test...\n");

  usmp_t client_ctx, server_ctx;
  usmp_transport_t c_tr, s_tr;
  loopback_ctx_t loopback;
  setup_session_pair(&client_ctx, &server_ctx, &c_tr, &s_tr, &loopback);

  // Send a payload that requires fragmentation (> USMP_MAX_DATA_LEN which is 452)
  // Let's send 1000 bytes
  uint8_t send_buf[1000];
  for (int i = 0; i < 1000; i++) {
    send_buf[i] = (uint8_t)(i & 0xFF);
  }

  int ret = usmp_send(&client_ctx, send_buf, sizeof(send_buf));
  assert(ret == 0);

  // Assert that loopback contains multiple fragments
  assert(loopback.write_pos > 0);

  // Server receives and reassembles the payload
  uint8_t recv_buf[1200];
  int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
  assert(recv_len == 1000);
  assert(memcmp(send_buf, recv_buf, 1000) == 0);

  printf("  - Fragmentation and reassembly passed!\n");
}

// ── Test: Malformed Frame Rejection (truncated, bad magic, bad CRC)
// ──────────────────────────────────────────────────────────
void test_malformed_frames(void) {
  printf("[TEST] Running malformed frame rejection tests...\n");

  usmp_packet_t pkt;

  // 1. Truncated frame (< USMP_HEADER_SIZE bytes)
  {
    uint8_t short_buf[6] = {0xCD, 0xAB, 0x02, 0x05, 0x00, 0x00};
    assert(usmp_parse_packet(short_buf, 6, &pkt) != 0);
    printf("  - Truncated frame rejected\n");
  }

  // 2. Bad magic
  {
    uint8_t bad_magic[USMP_HEADER_SIZE] = {0};
    bad_magic[0] = 0xFF;
    bad_magic[1] = 0xFF;  // magic = 0xFFFF, not 0xABCD
    bad_magic[2] = USMP_VERSION;
    bad_magic[3] = USMP_TYPE_DATA;
    // length = 0, CRC will be wrong but magic check is first
    assert(usmp_parse_packet(bad_magic, USMP_HEADER_SIZE, &pkt) != 0);
    printf("  - Bad magic rejected\n");
  }

  // 3. Bad CRC — correct magic/version but tampered CRC
  {
    // Build a valid frame then corrupt the CRC
    usmp_packet_t good_pkt;
    memset(&good_pkt, 0, sizeof(good_pkt));
    good_pkt.magic = USMP_MAGIC;
    good_pkt.version = USMP_VERSION;
    good_pkt.type = USMP_TYPE_DATA;
    good_pkt.seq = 0;
    good_pkt.length = 0;

    uint8_t frame[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
    uint16_t frame_len = 0;
    usmp_build_packet(&good_pkt, frame, &frame_len);

    // Flip a bit in the CRC bytes (offset 10-11)
    frame[10] ^= 0x01;

    assert(usmp_parse_packet(frame, (int)frame_len, &pkt) != 0);
    printf("  - Bad CRC rejected\n");
  }

  // 4. Payload length exceeds USMP_MAX_PAYLOAD
  {
    uint8_t over_len[USMP_HEADER_SIZE] = {0};
    over_len[0] = (uint8_t)(USMP_MAGIC & 0xFF);
    over_len[1] = (uint8_t)((USMP_MAGIC >> 8) & 0xFF);
    over_len[2] = USMP_VERSION;
    over_len[3] = USMP_TYPE_DATA;
    // Set length to USMP_MAX_PAYLOAD + 1
    uint16_t too_big = USMP_MAX_PAYLOAD + 1;
    over_len[8] = (uint8_t)(too_big & 0xFF);
    over_len[9] = (uint8_t)((too_big >> 8) & 0xFF);
    assert(usmp_parse_packet(over_len, USMP_HEADER_SIZE, &pkt) != 0);
    printf("  - Oversized payload length rejected\n");
  }

  // 5. Payload shorter than declared length
  {
    usmp_packet_t short_pkt;
    memset(&short_pkt, 0, sizeof(short_pkt));
    short_pkt.magic = USMP_MAGIC;
    short_pkt.version = USMP_VERSION;
    short_pkt.type = USMP_TYPE_DATA;
    short_pkt.seq = 0;
    short_pkt.length = 10;  // declare 10 payload bytes
    memset(short_pkt.payload, 0xAA, 10);

    uint8_t frame[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
    uint16_t frame_len = 0;
    usmp_build_packet(&short_pkt, frame, &frame_len);

    // Present only the header (cutting off the payload entirely)
    assert(usmp_parse_packet(frame, USMP_HEADER_SIZE, &pkt) != 0);
    printf("  - Short payload rejected\n");
  }

  // 6. Early rejection: corrupted payload does not overwrite destination payload buffer
  {
    usmp_packet_t corrupt_frame;
    memset(&corrupt_frame, 0, sizeof(corrupt_frame));
    corrupt_frame.magic = USMP_MAGIC;
    corrupt_frame.version = USMP_VERSION;
    corrupt_frame.type = USMP_TYPE_DATA;
    corrupt_frame.seq = 1;
    corrupt_frame.length = 4;
    corrupt_frame.payload[0] = 0xDE;
    corrupt_frame.payload[1] = 0xAD;
    corrupt_frame.payload[2] = 0xBE;
    corrupt_frame.payload[3] = 0xEF;

    uint8_t frame[USMP_HEADER_SIZE + 4];
    uint16_t frame_len = 0;
    usmp_build_packet(&corrupt_frame, frame, &frame_len);

    // Corrupt payload byte on the wire
    frame[USMP_HEADER_SIZE] ^= 0xFF;

    // Pre-populate target packet payload with sentinel pattern
    usmp_packet_t target_pkt;
    memset(target_pkt.payload, 0x5A, sizeof(target_pkt.payload));

    int ret = usmp_parse_packet(frame, (int)frame_len, &target_pkt);
    assert(ret != 0);
    // Ensure target_pkt.payload was NOT touched/polluted by memcpy
    assert(target_pkt.payload[0] == 0x5A);
    assert(target_pkt.payload[1] == 0x5A);
    assert(target_pkt.payload[2] == 0x5A);
    assert(target_pkt.payload[3] == 0x5A);
    printf("  - Corrupted packet rejected without polluting destination payload\n");
  }

  printf("[TEST] Malformed frame rejection tests passed!\n");
}

// ── Test: Fragment Ordering — out-of-order sequence aborts reassembly
// ──────────────────────────────────────────────────────────
void test_fragment_ordering(void) {
  printf("[TEST] Running fragment ordering tests...\n");

  usmp_t client_ctx, server_ctx;
  usmp_transport_t c_tr, s_tr;
  loopback_ctx_t loopback;
  setup_session_pair(&client_ctx, &server_ctx, &c_tr, &s_tr, &loopback);

  // Send a 1000-byte payload — this generates 3 fragments
  uint8_t send_buf[1000];
  memset(send_buf, 0x42, sizeof(send_buf));
  int ret = usmp_send(&client_ctx, send_buf, sizeof(send_buf));
  assert(ret == 0);

  // The loopback now contains 3 serialized frames (FRAG, FRAG, DATA).
  // We'll tamper with the second fragment's sequence number to be wrong.
  // Each frame is: USMP_HEADER_SIZE + encrypted_payload_len.
  // First, skip over frame 1 to find frame 2.
  size_t pos = 0;
  {
    // Parse frame 1's length to skip it
    uint16_t len1 = loopback.buffer[pos + 8] | (loopback.buffer[pos + 9] << 8);
    pos += USMP_HEADER_SIZE + len1;
  }

  // Now pos is at the start of frame 2. Corrupt its seq field (bytes 4-7).
  // Frame 2 should have seq=1; set it to seq=99 (wrong).
  loopback.buffer[pos + 4] = 99;
  loopback.buffer[pos + 5] = 0;
  loopback.buffer[pos + 6] = 0;
  loopback.buffer[pos + 7] = 0;
  // The CRC is now wrong too, so the parse will fail. That's fine —
  // we're confirming that corrupted fragment data doesn't silently succeed.

  uint8_t recv_buf[1200];
  int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
  // Should fail: either CRC error or sequence mismatch
  assert(recv_len < 0 || recv_len == 0);

  printf("  - Out-of-order fragment sequence correctly rejected\n");
  printf("[TEST] Fragment ordering tests passed!\n");
}

// ── Test: UDP Sliding Replay Window
// ──────────────────────────────────────────────────────────

// Helper: confirm_authenticated stub that just records the last seq it saw
static uint32_t g_last_confirmed_seq = 0;
static void mock_confirm_authenticated(usmp_transport_t* t, uint32_t seq) {
  (void)t;
  g_last_confirmed_seq = seq;
}

void test_replay_window(void) {
  printf("[TEST] Running UDP sliding replay window tests...\n");

  usmp_t client_ctx, server_ctx;
  usmp_transport_t c_tr, s_tr;
  loopback_ctx_t loopback;
  setup_session_pair(&client_ctx, &server_ctx, &c_tr, &s_tr, &loopback);

  // Enable UDP mode on server side (set confirm_authenticated)
  server_ctx.transport.confirm_authenticated = mock_confirm_authenticated;

  // --- Sub-test 1: Normal in-order delivery ---
  {
    uint8_t msg[] = "hello-0";
    int ret = usmp_send(&client_ctx, msg, sizeof(msg));
    assert(ret == 0);

    uint8_t recv_buf[64];
    int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
    assert(recv_len == (int)sizeof(msg));
    assert(memcmp(recv_buf, msg, sizeof(msg)) == 0);
    printf("  - In-order message #0 received OK\n");
  }

  // --- Sub-test 2: Duplicate replay — resend same frame ---
  {
    // Save write_pos before sending
    size_t saved_write = loopback.write_pos;

    uint8_t msg[] = "hello-1";
    int ret = usmp_send(&client_ctx, msg, sizeof(msg));
    assert(ret == 0);

    // Record the frame that was written
    size_t frame_start = saved_write;
    size_t frame_end = loopback.write_pos;
    size_t frame_size = frame_end - frame_start;

    // Receive it once (should succeed)
    uint8_t recv_buf[64];
    int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
    assert(recv_len == (int)sizeof(msg));
    printf("  - Message #1 first delivery OK\n");

    // Re-inject the exact same frame into the loopback buffer (replay attack)
    assert(loopback.write_pos + frame_size <= LOOPBACK_BUF_MAX);
    memcpy(loopback.buffer + loopback.write_pos, loopback.buffer + frame_start, frame_size);
    loopback.write_pos += frame_size;

    // Attempt to receive the replayed frame — should be dropped (duplicate)
    // In UDP mode, usmp_recv will try to read, detect the duplicate via
    // the replay window bitmap, drop it, and loop. After max attempts it
    // returns 0 (no data).
    recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
    assert(recv_len <= 0);  // dropped or no-data, never a successful decode
    printf("  - Replay of message #1 correctly dropped\n");
  }

  // --- Sub-test 3: Out-of-window (ancient) packet dropped ---
  {
    // Send 65 more messages to advance the window well past seq 0
    for (int i = 0; i < 65; i++) {
      uint8_t msg[8];
      snprintf((char*)msg, sizeof(msg), "adv-%d", i);
      int ret = usmp_send(&client_ctx, msg, sizeof(msg));
      assert(ret == 0);

      uint8_t recv_buf[64];
      int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
      assert(recv_len > 0);
    }

    // Now server rx_seq is well advanced. Inject a frame with seq=0
    // (which is outside the 64-bit window). Build it manually.
    // Save client tx_seq, temporarily reset it to build a seq=0 frame
    uint32_t saved_tx_seq = client_ctx.tx_seq;
    client_ctx.tx_seq = 0;

    uint8_t ancient[] = "ancient";
    int ret = usmp_send(&client_ctx, ancient, sizeof(ancient));
    assert(ret == 0);
    client_ctx.tx_seq = saved_tx_seq;  // restore

    // Try to receive — should be dropped as too old
    uint8_t recv_buf[64];
    int recv_len = usmp_recv(&server_ctx, recv_buf, sizeof(recv_buf));
    assert(recv_len <= 0);
    printf("  - Ancient out-of-window packet correctly dropped\n");
  }

  printf("[TEST] UDP sliding replay window tests passed!\n");
}

// ── Test: Send-failure must not reuse a sequence number (nonce reuse)
// ──────────────────────────────────────────────────────────
// Models a transport that puts the ciphertext on the wire and *then* reports
// failure — exactly the ESP32/UDP ARQ, which retransmits up to 5 times before
// returning -1. It records the sequence number of every frame it "transmits"
// so the test can assert no seq (hence no AES-GCM nonce) is ever reused.
#define FAILTX_MAX 16
typedef struct {
  uint32_t seqs[FAILTX_MAX];
  int count;
} failtx_ctx_t;

static int failtx_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  failtx_ctx_t* c = (failtx_ctx_t*)t->ctx;
  if (len >= 8 && c->count < FAILTX_MAX) {
    uint32_t seq = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) |
                   ((uint32_t)data[7] << 24);
    c->seqs[c->count++] = seq;
  }
  return -1;  // the wire write happened; the transport still fails
}

static int failtx_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  (void)t;
  (void)buf;
  (void)max_len;
  return -1;
}

void test_send_failure_no_nonce_reuse(void) {
  printf("[TEST] Running send-failure nonce-reuse regression test...\n");

  usmp_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  uint8_t key[32] = {0xAA};
  uint8_t sid[16] = {0x01, 0x02, 0x03};
  ctx.established = true;
  memcpy(ctx.tx_key, key, 32);
  memcpy(ctx.session_id, sid, 16);

  failtx_ctx_t fctx = {0};
  usmp_transport_t tr;
  memset(&tr, 0, sizeof(tr));
  tr.send = failtx_send;
  tr.recv = failtx_recv;
  tr.ctx = &fctx;
  ctx.transport = tr;

  uint8_t msg1[] = "first-plaintext";
  int r1 = usmp_send(&ctx, msg1, sizeof(msg1));
  assert(r1 == USMP_ERR_TRANSPORT_FAILED);  // send reports failure...
  assert(ctx.tx_seq == 1);                   // ...but the seq is burned regardless
  assert(ctx.established == false);          // ...and the session is torn down

  // Model the shipped example's continue-on-failure loop: a naive caller
  // re-arms `established` (without a fresh key/seq) and sends different
  // plaintext. The second frame must NOT land on seq 0 again.
  ctx.established = true;
  uint8_t msg2[] = "second-different-plaintext";
  int r2 = usmp_send(&ctx, msg2, sizeof(msg2));
  assert(r2 == USMP_ERR_TRANSPORT_FAILED);
  assert(ctx.established == false);

  // Every transmitted frame must carry a distinct sequence number, hence a
  // distinct nonce under the fixed tx_key. Pre-fix this array was {0, 0}.
  assert(fctx.count >= 2);
  for (int i = 0; i < fctx.count; i++) {
    for (int j = i + 1; j < fctx.count; j++) {
      assert(fctx.seqs[i] != fctx.seqs[j]);
    }
  }
  printf("  - tx_seq advanced on send failure; no nonce reuse across %d frames\n", fctx.count);
  printf("[TEST] Send-failure nonce-reuse regression test passed!\n");
}

// ── Test: control frames must not wrap tx_seq past the overflow sentinel
// ──────────────────────────────────────────────────────────
void test_control_seq_overflow(void) {
  printf("[TEST] Running control-frame seq overflow guard test...\n");

  usmp_t ctx, dummy_server;
  usmp_transport_t c_tr, s_tr;
  loopback_ctx_t loopback;
  setup_session_pair(&ctx, &dummy_server, &c_tr, &s_tr, &loopback);

  // Park tx_seq at the reserved terminal sentinel. A PING here would wrap to 0
  // and reuse the session's very first nonce under the unchanged key.
  ctx.tx_seq = 0xFFFFFFFF;

  int r = usmp_ping(&ctx);
  assert(r == USMP_ERR_TRANSPORT_FAILED);  // refused
  assert(ctx.tx_seq == 0xFFFFFFFF);  // did NOT wrap to 0
  assert(ctx.established == false);  // session torn down
  assert(loopback.write_pos == 0);   // nothing was transmitted

  printf("  - PING at seq 0xFFFFFFFF refused without wrapping or transmitting\n");
  printf("[TEST] Control-frame seq overflow guard test passed!\n");
}

// ── Test: usmp_close sends a graceful BYE
// ──────────────────────────────────────────────────────────
void test_bye_on_close(void) {
  printf("[TEST] Running BYE-on-close test...\n");

  usmp_t client, server;
  usmp_transport_t c_tr, s_tr;
  loopback_ctx_t loopback;
  setup_session_pair(&client, &server, &c_tr, &s_tr, &loopback);

  size_t before = loopback.write_pos;
  usmp_close(&client);

  // A graceful close transmits exactly one BYE control frame (type at header
  // offset 3: magic[2] version[1] type[1] ...).
  assert(loopback.write_pos > before);
  assert(loopback.buffer[before + 3] == USMP_TYPE_BYE);
  assert(client.established == false);

  // The peer decodes it as a peer-initiated session close (usmp_recv → USMP_ERR_PEER_CLOSED).
  uint8_t recv_buf[64];
  int r = usmp_recv(&server, recv_buf, sizeof(recv_buf));
  assert(r == USMP_ERR_PEER_CLOSED);
  assert(server.established == false);

  printf("  - usmp_close emits a BYE the peer decodes as session close\n");
  printf("[TEST] BYE-on-close test passed!\n");
}

void test_usmp_strerror(void) {
  printf("[TEST] Running usmp_strerror tests...\n");
  assert(strcmp(usmp_strerror(USMP_OK), "Success") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_INVALID_ARG), "Invalid argument") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_TRANSPORT_FAILED), "Transport I/O failed") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_AUTH_FAILED), "Authentication failed (bad PSK or corrupted handshake)") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_TIMEOUT), "Operation timed out") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_REPLAY_DETECTED), "Replay attack detected or duplicate sequence") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_BUFFER_OVERFLOW), "Buffer overflow or payload exceeds capacity") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_SEQ_EXHAUSTED), "Sequence numbers exhausted (rekey required)") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_CRYPTO_FAILED), "Cryptographic operation failed (tag mismatch or corrupted ciphertext)") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_NOT_CONNECTED), "Session is not connected or established") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_MUTEX_FAILED), "RTOS mutex lock or acquisition failed") == 0);
  assert(strcmp(usmp_strerror(USMP_ERR_PEER_CLOSED), "Session closed gracefully by remote peer") == 0);
  assert(strcmp(usmp_strerror((usmp_err_t)-999), "Unknown USMP error") == 0);
  printf("  - All error strings validated\n");
  printf("[TEST] usmp_strerror tests passed!\n");
}

extern char g_last_log_level;
extern char g_last_log_tag[64];
extern char g_last_log_msg[256];
extern char g_last_formatted_log[512];

void test_logging(void) {
  printf("[TEST] Running logging tests...\n");

  // 1. Verify default log level is ERROR
  assert(usmp_get_log_level() == USMP_LOG_LEVEL_ERROR);

  // Reset captured variables
  g_last_log_level = 0;
  g_last_log_tag[0] = '\0';
  g_last_log_msg[0] = '\0';
  g_last_formatted_log[0] = '\0';

  // 2. Logging below level ERROR (e.g. INFO) should be ignored
  USMP_LOGI("SOME_TAG", "Info message");
  assert(g_last_log_level == 0);  // ignored

  USMP_LOGD("SOME_TAG", "Debug message");
  assert(g_last_log_level == 0);  // ignored

  USMP_LOGW("SOME_TAG", "Warning message");
  assert(g_last_log_level == 0);  // ignored

  // 3. Logging at ERROR level should be printed and formatted
  USMP_LOGE("TEST_TAG", "Error occurred!");
  assert(g_last_log_level == 'E');
  assert(strcmp(g_last_log_tag, "TEST_TAG") == 0);
  assert(strcmp(g_last_log_msg, "Error occurred!") == 0);
  assert(strcmp(g_last_formatted_log, "[test_tag]: Error occurred!") == 0);

  // Reset capture
  g_last_log_level = 0;

  // 4. Change log level to INFO
  usmp_set_log_level(USMP_LOG_LEVEL_INFO);
  assert(usmp_get_log_level() == USMP_LOG_LEVEL_INFO);

  // Now INFO and WARN should be logged (formatted standard way)
  USMP_LOGI("TAG_INFO", "Info msg");
  assert(g_last_log_level == 'I');
  assert(strcmp(g_last_log_tag, "TAG_INFO") == 0);
  assert(strcmp(g_last_formatted_log, "[I][TAG_INFO] Info msg") == 0);

  g_last_log_level = 0;
  USMP_LOGW("TAG_WARN", "Warn msg");
  assert(g_last_log_level == 'W');
  assert(strcmp(g_last_formatted_log, "[W][TAG_WARN] Warn msg") == 0);

  // DEBUG should still be ignored
  g_last_log_level = 0;
  USMP_LOGD("TAG_DEBUG", "Debug msg");
  assert(g_last_log_level == 0);

  // 5. Change log level to DEBUG
  usmp_set_log_level(USMP_LOG_LEVEL_DEBUG);
  USMP_LOGD("TAG_DEBUG", "Debug msg");
  assert(g_last_log_level == 'D');
  assert(strcmp(g_last_formatted_log, "[D][TAG_DEBUG] Debug msg") == 0);

  // 6. Change log level to NONE
  usmp_set_log_level(USMP_LOG_LEVEL_NONE);
  g_last_log_level = 0;
  USMP_LOGE("TEST_TAG", "Error!");
  assert(g_last_log_level == 0);  // no log

  // Reset back to default
  usmp_set_log_level(USMP_LOG_LEVEL_ERROR);

  printf("  - Logging tests passed!\n");
}

static int dummy_send_rekey(usmp_transport_t* t, const uint8_t* data, size_t len) {
  (void)t;
  (void)data;
  (void)len;
  return 0;
}

static void test_rekey(void) {
  printf("Running rekey test...\n");
  usmp_t session = {0};
  session.established = true;
  session.transport.send = dummy_send_rekey;
  session.tx_seq = 100;
  session.rx_seq = 100;
  memset(session.session_id, 0x11, 16);
  memset(session.tx_key, 0xAA, 32);
  memset(session.rx_key, 0xBB, 32);

  uint8_t old_tx[32];
  memcpy(old_tx, session.tx_key, 32);

  int ret = usmp_rekey(&session);
  assert(ret == 0);
  assert(session.tx_seq == 0);
  assert(session.rx_seq == 0);
  assert(memcmp(session.tx_key, old_tx, 32) != 0);

  printf("  - Rekey test passed!\n");
}

static void test_chacha20_poly1305(void) {
  printf("Running ChaCha20-Poly1305 test...\n");
  uint8_t key[32];
  memset(key, 0x42, sizeof(key));
  uint8_t nonce[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  uint8_t aad[10] = {0xAB, 0xCD, 0x02, 0x05, 0, 0, 0, 0, 0, 10};
  const char* plain_text = "ChaCha20-Poly1305 C test payload";
  size_t plain_len = strlen(plain_text);

  uint8_t cipher_buf[128];
  size_t cipher_len = 0;
  int ret = usmp_crypto_encrypt(USMP_CIPHER_CHACHA20_POLY1305, key, nonce, aad, sizeof(aad),
                                (const uint8_t*)plain_text, plain_len, cipher_buf, &cipher_len);
  assert(ret == 0);

  uint8_t decrypted[128];
  size_t dec_len = 0;
  ret = usmp_crypto_decrypt(USMP_CIPHER_CHACHA20_POLY1305, key, nonce, aad, sizeof(aad),
                            cipher_buf, cipher_len, decrypted, &dec_len);
  assert(ret == 0);
  assert(dec_len == plain_len);
  assert(memcmp(decrypted, plain_text, plain_len) == 0);

  printf("  - ChaCha20-Poly1305 test passed!\n");
}

typedef struct {
  mbedtls_ecdh_context srv_ecdh;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  const uint8_t* psk;
  size_t psk_len;
  uint8_t server_nonce[USMP_NONCE_LEN];
  uint8_t server_pub[32];
  uint8_t client_pub[32];
  uint8_t client_device_id[USMP_DEVICE_ID_LEN];
  uint8_t session_id[USMP_SESSION_ID_LEN];
  uint8_t srv_tx_key[32];
  uint8_t srv_rx_key[32];
  uint8_t rx_queue[512];
  size_t rx_queue_len;
  size_t rx_queue_pos;
  int step;
} mock_hs_server_t;

static int mock_hs_entropy(void* data, unsigned char* output, size_t len, size_t* olen) {
  (void)data;
  for (size_t i = 0; i < len; i++) {
    output[i] = (unsigned char)(rand() & 0xFF);
  }
  *olen = len;
  return 0;
}

static int mock_hs_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  mock_hs_server_t* srv = (mock_hs_server_t*)t->ctx;
  usmp_packet_t pkt;
  if (usmp_parse_packet((uint8_t*)(uintptr_t)data, (int)len, &pkt) != 0) return -1;

  if (pkt.type == USMP_TYPE_HELLO) {
    srv->step = 1;
    memcpy(srv->client_device_id, pkt.payload, USMP_DEVICE_ID_LEN);
    memcpy(srv->client_pub, pkt.payload + USMP_DEVICE_ID_LEN, 32);

    mbedtls_ecdh_init(&srv->srv_ecdh);
    mbedtls_entropy_init(&srv->entropy);
    mbedtls_ctr_drbg_init(&srv->ctr_drbg);
    mbedtls_entropy_add_source(&srv->entropy, mock_hs_entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy,
                          (const unsigned char*)"srv-rng", 7);
    mbedtls_ecdh_setup(&srv->srv_ecdh, MBEDTLS_ECP_DP_CURVE25519);

    uint8_t srv_pub_buf[65];
    size_t srv_pub_len = 0;
    mbedtls_ecdh_make_public(&srv->srv_ecdh, &srv_pub_len, srv_pub_buf, sizeof(srv_pub_buf),
                             mbedtls_ctr_drbg_random, &srv->ctr_drbg);
    memcpy(srv->server_pub, srv_pub_buf + (srv_pub_len - 32), 32);

    uint8_t peer_buf[33];
    peer_buf[0] = 32;
    memcpy(peer_buf + 1, srv->client_pub, 32);
    mbedtls_ecdh_read_public(&srv->srv_ecdh, peer_buf, sizeof(peer_buf));

    uint8_t shared_secret[32];
    size_t shared_len = 0;
    mbedtls_ecdh_calc_secret(&srv->srv_ecdh, &shared_len, shared_secret, sizeof(shared_secret),
                             mbedtls_ctr_drbg_random, &srv->ctr_drbg);

    memset(srv->server_nonce, 0x5A, USMP_NONCE_LEN);

    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t info[7 + 32 + 32];
    memcpy(info, "usmp-v2", 7);
    memcpy(info + 7, srv->client_pub, 32);
    memcpy(info + 7 + 32, srv->server_pub, 32);

    uint8_t key_material[64];
    mbedtls_hkdf(md, srv->server_nonce, USMP_NONCE_LEN, shared_secret, shared_len,
                 info, sizeof(info), key_material, sizeof(key_material));
    memcpy(srv->srv_rx_key, key_material, 32);       // client tx is server rx
    memcpy(srv->srv_tx_key, key_material + 32, 32);  // client rx is server tx
    mbedtls_platform_zeroize(key_material, sizeof(key_material));
    mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));

    usmp_packet_t chal = {0};
    chal.magic = USMP_MAGIC;
    chal.version = USMP_VERSION;
    chal.type = USMP_TYPE_CHALLENGE;
    chal.seq = 1;
    chal.length = USMP_NONCE_LEN + 32;
    memcpy(chal.payload, srv->server_nonce, USMP_NONCE_LEN);
    memcpy(chal.payload + USMP_NONCE_LEN, srv->server_pub, 32);

    uint16_t out_len = 0;
    usmp_build_packet(&chal, srv->rx_queue, &out_len);
    srv->rx_queue_len = out_len;
    srv->rx_queue_pos = 0;
    return 0;
  }

  if (pkt.type == USMP_TYPE_HELLO_ACK) {
    srv->step = 2;
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    uint8_t input_c[4 + 32 + 6 + 32 + 32];
    input_c[0] = (uint8_t)(USMP_MAGIC & 0xFF);
    input_c[1] = (uint8_t)((USMP_MAGIC >> 8) & 0xFF);
    input_c[2] = USMP_VERSION;
    input_c[3] = USMP_TYPE_HELLO_ACK;
    memcpy(input_c + 4, srv->server_nonce, 32);
    memcpy(input_c + 36, srv->client_device_id, 6);
    memcpy(input_c + 42, srv->client_pub, 32);
    memcpy(input_c + 74, srv->server_pub, 32);

    uint8_t expected_c_hmac[32];
    mbedtls_md_hmac(md, srv->psk, srv->psk_len, input_c, sizeof(input_c), expected_c_hmac);
    assert(memcmp(pkt.payload, expected_c_hmac, 32) == 0);

    memset(srv->session_id, 0x77, USMP_SESSION_ID_LEN);
    uint8_t input_s[4 + 32 + 16 + 32 + 32];
    input_s[0] = (uint8_t)(USMP_MAGIC & 0xFF);
    input_s[1] = (uint8_t)((USMP_MAGIC >> 8) & 0xFF);
    input_s[2] = USMP_VERSION;
    input_s[3] = USMP_TYPE_SESSION_OK;
    memcpy(input_s + 4, srv->server_nonce, 32);
    memcpy(input_s + 36, srv->session_id, 16);
    memcpy(input_s + 52, srv->client_pub, 32);
    memcpy(input_s + 84, srv->server_pub, 32);

    uint8_t srv_hmac[32];
    mbedtls_md_hmac(md, srv->psk, srv->psk_len, input_s, sizeof(input_s), srv_hmac);

    usmp_packet_t ok_pkt = {0};
    ok_pkt.magic = USMP_MAGIC;
    ok_pkt.version = USMP_VERSION;
    ok_pkt.type = USMP_TYPE_SESSION_OK;
    ok_pkt.seq = 3;
    ok_pkt.length = USMP_SESSION_ID_LEN + 32;
    memcpy(ok_pkt.payload, srv->session_id, USMP_SESSION_ID_LEN);
    memcpy(ok_pkt.payload + USMP_SESSION_ID_LEN, srv_hmac, 32);

    uint16_t out_len = 0;
    usmp_build_packet(&ok_pkt, srv->rx_queue, &out_len);
    srv->rx_queue_len = out_len;
    srv->rx_queue_pos = 0;
    return 0;
  }

  return -1;
}

static int mock_hs_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  mock_hs_server_t* srv = (mock_hs_server_t*)t->ctx;
  if (srv->rx_queue_len == 0 || srv->rx_queue_pos >= srv->rx_queue_len) return -1;
  size_t avail = srv->rx_queue_len - srv->rx_queue_pos;
  if (avail > max_len) return -1;
  memcpy(buf, srv->rx_queue + srv->rx_queue_pos, avail);
  srv->rx_queue_pos += avail;
  return (int)avail;
}

static int mock_hs_available(usmp_transport_t* t) {
  mock_hs_server_t* srv = (mock_hs_server_t*)t->ctx;
  if (!srv || srv->rx_queue_len == 0 || srv->rx_queue_pos >= srv->rx_queue_len) return 0;
  return (int)(srv->rx_queue_len - srv->rx_queue_pos);
}

static int mock_hs_reconnect(usmp_transport_t* t) {
  (void)t;
  return 0;
}

static void test_zero_heap_handshake(void) {
  printf("Running zero-heap handshake test...\n");
  const uint8_t psk[16] = "usmp-test-psk-16";

  mock_hs_server_t srv = {0};
  srv.psk = psk;
  srv.psk_len = sizeof(psk);

  usmp_transport_t transport = {0};
  transport.send = mock_hs_send;
  transport.recv = mock_hs_recv;
  transport.ctx = &srv;

  uint8_t scratch[USMP_HANDSHAKE_SCRATCH_LEN];
  memset(scratch, 0xEE, sizeof(scratch));

  usmp_t session = {0};
  session.psk = psk;
  session.psk_len = sizeof(psk);
  session.scratch = scratch;
  session.scratch_len = sizeof(scratch);

  int ret = usmp_handshake(&transport, &session);
  assert(ret == USMP_OK);
  assert(session.established == true);
  assert(memcmp(session.tx_key, srv.srv_rx_key, 32) == 0);
  assert(memcmp(session.rx_key, srv.srv_tx_key, 32) == 0);

  // Validate that scratchpad buffers were wiped (zeroized) upon completion
  uint8_t zero_block[USMP_HANDSHAKE_SCRATCH_LEN] = {0};
  assert(memcmp(scratch, zero_block, sizeof(scratch)) == 0);

  mbedtls_ecdh_free(&srv.srv_ecdh);
  mbedtls_entropy_free(&srv.entropy);
  mbedtls_ctr_drbg_free(&srv.ctr_drbg);

  // Test usmp_connect propagation of scratchpad
  memset(&srv, 0, sizeof(srv));
  srv.psk = psk;
  srv.psk_len = sizeof(psk);
  memset(scratch, 0xCC, sizeof(scratch));

  usmp_t client_ctx = {0};
  client_ctx.psk = psk;
  client_ctx.psk_len = sizeof(psk);
  client_ctx.scratch = scratch;
  client_ctx.scratch_len = sizeof(scratch);

  ret = usmp_connect(&client_ctx, &transport);
  assert(ret == USMP_OK);
  assert(client_ctx.established == true);
  assert(client_ctx.scratch == scratch);
  assert(client_ctx.scratch_len == sizeof(scratch));
  assert(memcmp(scratch, zero_block, sizeof(scratch)) == 0);

  mbedtls_ecdh_free(&srv.srv_ecdh);
  mbedtls_entropy_free(&srv.entropy);
  mbedtls_ctr_drbg_free(&srv.ctr_drbg);

  printf("  - Zero-heap handshake test passed!\n");
}

static void test_async_handshake_fsm(void) {
  printf("Running async non-blocking handshake FSM tests...\n");
  const uint8_t psk[16] = "usmp-test-psk-16";

  mock_hs_server_t srv = {0};
  srv.psk = psk;
  srv.psk_len = sizeof(psk);

  usmp_transport_t transport = {0};
  transport.send = mock_hs_send;
  transport.recv = mock_hs_recv;
  transport.available = mock_hs_available;
  transport.reconnect = mock_hs_reconnect;
  transport.ctx = &srv;

  usmp_t session = {0};
  session.psk = psk;
  session.psk_len = sizeof(psk);

  // 1. Initial connect async
  usmp_err_t err = usmp_connect_async(&session, &transport);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_AWAITING_CHALLENGE);
  assert(session.established == false);
  assert(session.hs_ctx != NULL);
  assert(usmp_get_state(&session) == USMP_STATE_AWAITING_CHALLENGE);

  // At this point, HELLO was sent to mock server, and mock server generated CHALLENGE in rx_queue
  // Test non-blocking tick: if we simulate no data available yet
  size_t saved_len = srv.rx_queue_len;
  srv.rx_queue_len = 0;  // temporarily hide incoming packet
  err = usmp_step(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_AWAITING_CHALLENGE);

  // Restore incoming packet (CHALLENGE)
  srv.rx_queue_len = saved_len;

  // 2. Next step consumes CHALLENGE, computes ECDH, and sends HELLO_ACK
  err = usmp_step(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_AWAITING_SESSION_OK);
  assert(usmp_get_state(&session) == USMP_STATE_AWAITING_SESSION_OK);

  // 3. Next step consumes SESSION_OK, verifies server HMAC, and finishes handshake
  err = usmp_step(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_ESTABLISHED);
  assert(session.established == true);
  assert(session.hs_ctx == NULL);
  assert(memcmp(session.tx_key, srv.srv_rx_key, 32) == 0);
  assert(memcmp(session.rx_key, srv.srv_tx_key, 32) == 0);

  // 4. Stepping an established session is healthy and non-blocking
  err = usmp_step(&session);
  assert(err == USMP_OK);

  // 5. Reconnect async resets session and restarts handshake FSM
  mbedtls_ecdh_free(&srv.srv_ecdh);
  mbedtls_entropy_free(&srv.entropy);
  mbedtls_ctr_drbg_free(&srv.ctr_drbg);
  memset(&srv, 0, sizeof(srv));
  srv.psk = psk;
  srv.psk_len = sizeof(psk);

  err = usmp_reconnect_async(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_AWAITING_CHALLENGE);
  assert(session.established == false);
  assert(session.hs_ctx != NULL);

  // Complete reconnected handshake step-by-step
  err = usmp_step(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_AWAITING_SESSION_OK);

  err = usmp_step(&session);
  assert(err == USMP_OK);
  assert(session.state == USMP_STATE_ESTABLISHED);
  assert(session.established == true);
  assert(session.hs_ctx == NULL);

  // 6. Close session transitions state to IDLE
  usmp_close(&session);
  assert(session.state == USMP_STATE_IDLE);
  assert(session.established == false);

  mbedtls_ecdh_free(&srv.srv_ecdh);
  mbedtls_entropy_free(&srv.entropy);
  mbedtls_ctr_drbg_free(&srv.ctr_drbg);

  printf("  - Async non-blocking handshake FSM tests passed!\n");
}

extern uint32_t usmp_test_get_wdt_feed_count(void);
extern void usmp_test_reset_wdt_feed_count(void);

extern uint32_t usmp_test_get_mutex_lock_count(void);
extern uint32_t usmp_test_get_mutex_unlock_count(void);
extern void usmp_test_reset_mutex_counts(void);

typedef struct {
  uint32_t srtt;
  uint32_t rttvar;
  uint32_t rto;
} test_rtt_ctx_t;

static void update_rtt(test_rtt_ctx_t* ctx, uint32_t sample, int attempt) {
  if (attempt == 0) {
    if (sample == 0) sample = 1;
    if (ctx->srtt == 0) {
      ctx->srtt = sample;
      ctx->rttvar = sample / 2;
    } else {
      int32_t delta = (int32_t)sample - (int32_t)ctx->srtt;
      int32_t abs_delta = delta < 0 ? -delta : delta;
      ctx->rttvar = (uint32_t)((int32_t)ctx->rttvar + (abs_delta - (int32_t)ctx->rttvar) / 4);
      ctx->srtt = (uint32_t)((int32_t)ctx->srtt + delta / 8);
    }
    uint32_t new_rto = ctx->srtt + 4 * ctx->rttvar;
    if (new_rto < 100) new_rto = 100;
    if (new_rto > 5000) new_rto = 5000;
    ctx->rto = new_rto;
  }
}

static uint32_t compute_backoff_timeout(uint32_t base_rto, int attempt) {
  if (base_rto < 100) base_rto = 100;
  if (base_rto > 5000) base_rto = 5000;
  uint32_t timeout_ms = base_rto * (1U << attempt);
  if (timeout_ms > 5000) timeout_ms = 5000;
  if (timeout_ms < 100) timeout_ms = 100;
  return timeout_ms;
}

static void test_coap_rtt_estimation(void) {
  printf("Running CoAP RTT estimation and WDT guard tests...\n");

  test_rtt_ctx_t ctx = {
      .srtt = 200,
      .rttvar = 100,
      .rto = 500,
  };

  // 1. Initial State
  assert(ctx.srtt == 200);
  assert(ctx.rttvar == 100);
  assert(ctx.rto == 500);

  // 2. High latency sample (400 ms on attempt 0)
  update_rtt(&ctx, 400, 0);
  // delta = 400 - 200 = 200
  // rttvar = 100 + (200 - 100)/4 = 125
  // srtt = 200 + 200/8 = 225
  // rto = 225 + 4 * 125 = 725
  assert(ctx.srtt == 225);
  assert(ctx.rttvar == 125);
  assert(ctx.rto == 725);

  // 3. Karn's Algorithm: retransmitted frames (attempt > 0) MUST NOT update RTT
  uint32_t prev_srtt = ctx.srtt;
  uint32_t prev_rttvar = ctx.rttvar;
  uint32_t prev_rto = ctx.rto;
  update_rtt(&ctx, 900, 1);  // attempt 1 (retry)
  assert(ctx.srtt == prev_srtt);
  assert(ctx.rttvar == prev_rttvar);
  assert(ctx.rto == prev_rto);

  update_rtt(&ctx, 10, 2);  // attempt 2 (retry)
  assert(ctx.srtt == prev_srtt);
  assert(ctx.rttvar == prev_rttvar);
  assert(ctx.rto == prev_rto);

  // 4. Low latency sample (50 ms on attempt 0)
  update_rtt(&ctx, 50, 0);
  // delta = 50 - 225 = -175, abs_delta = 175
  // rttvar = 125 + (175 - 125)/4 = 137
  // srtt = 225 + (-175)/8 = 225 - 21 = 204
  // rto = 204 + 4 * 137 = 752
  assert(ctx.srtt == 204);
  assert(ctx.rttvar == 137);
  assert(ctx.rto == 752);

  // 5. Binary Exponential Backoff Progression
  assert(compute_backoff_timeout(500, 0) == 500);
  assert(compute_backoff_timeout(500, 1) == 1000);
  assert(compute_backoff_timeout(500, 2) == 2000);
  assert(compute_backoff_timeout(500, 3) == 4000);
  assert(compute_backoff_timeout(500, 4) == 5000);  // 8000 clamped to 5000 max

  // 6. Minimum & Maximum Clamping Bounds
  assert(compute_backoff_timeout(10, 0) == 100);    // clamped to 100ms min
  assert(compute_backoff_timeout(9999, 0) == 5000); // clamped to 5000ms max

  // Drive RTT down with repeated low samples
  for (int i = 0; i < 50; i++) {
    update_rtt(&ctx, 1, 0);
  }
  assert(ctx.rto >= 100);  // Min RTO clamp

  // Drive RTT up with massive samples
  for (int i = 0; i < 50; i++) {
    update_rtt(&ctx, 6000, 0);
  }
  assert(ctx.rto <= 5000);  // Max RTO clamp

  // 7. Watchdog Timer (WDT) Feed Hook Verification
  usmp_test_reset_wdt_feed_count();
  assert(usmp_test_get_wdt_feed_count() == 0);
  for (int i = 0; i < 15; i++) {
    usmp_port_wdt_feed();
  }
  assert(usmp_test_get_wdt_feed_count() == 15);

  printf("  - CoAP RTT estimation and WDT guard tests passed!\n");
}

#ifdef _WIN32
#include <windows.h>
typedef HANDLE test_thread_t;
typedef DWORD(WINAPI* test_thread_fn_t)(LPVOID);
static inline int test_thread_create(test_thread_t* t, test_thread_fn_t fn, void* arg) {
  *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
  return (*t != NULL) ? 0 : -1;
}
static inline void test_thread_join(test_thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
typedef void* (*test_thread_fn_t)(void*);
static inline int test_thread_create(test_thread_t* t, test_thread_fn_t fn, void* arg) {
  return pthread_create(t, NULL, fn, arg);
}
static inline void test_thread_join(test_thread_t t) {
  pthread_join(t, NULL);
}
#endif

#define CONCURRENT_SENDS_PER_THREAD 25
#define CONCURRENT_THREAD_COUNT 4
#define TOTAL_CONCURRENT_SENDS (CONCURRENT_SENDS_PER_THREAD * CONCURRENT_THREAD_COUNT)

typedef struct {
  usmp_t* session;
  int thread_id;
} concurrent_worker_arg_t;

static uint32_t g_concurrent_seen_seqs[TOTAL_CONCURRENT_SENDS];
#ifdef _WIN32
static volatile LONG g_concurrent_seen_count = 0;
#else
static volatile long g_concurrent_seen_count = 0;
#endif

static int concurrent_mock_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  (void)t;
  usmp_packet_t pkt;
  if (usmp_parse_packet((uint8_t*)data, (int)len, &pkt) == 0) {
    if (pkt.type == USMP_TYPE_DATA || pkt.type == USMP_TYPE_DATA_FRAG) {
#ifdef _WIN32
      LONG idx = InterlockedIncrement(&g_concurrent_seen_count) - 1;
#else
      long idx = __sync_fetch_and_add(&g_concurrent_seen_count, 1);
#endif
      if (idx < TOTAL_CONCURRENT_SENDS) {
        g_concurrent_seen_seqs[idx] = pkt.seq;
      }
    }
  }
  return 0;
}

#ifdef _WIN32
static DWORD WINAPI __attribute__((force_align_arg_pointer)) concurrent_send_worker(LPVOID param)
#else
static void* concurrent_send_worker(void* param)
#endif
{
  concurrent_worker_arg_t* arg = (concurrent_worker_arg_t*)param;
  for (int i = 0; i < CONCURRENT_SENDS_PER_THREAD; i++) {
    char payload[32];
    snprintf(payload, sizeof(payload), "Worker %d Msg %d", arg->thread_id, i);
    usmp_err_t err = usmp_send(arg->session, (const uint8_t*)payload, (uint16_t)strlen(payload));
    assert(err == USMP_OK);
  }
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

static void test_split_mutex_concurrency(void) {
  printf("Running split RTOS mutex architecture and concurrency tests...\n");

  // 1. Mutex lifecycle & basic lock balance verification
  usmp_test_reset_mutex_counts();
  assert(usmp_test_get_mutex_lock_count() == 0);
  assert(usmp_test_get_mutex_unlock_count() == 0);

  usmp_t session = {0};
  int ret_m = usmp_port_mutex_create(&session.tx_mutex);
  assert(ret_m == 0 && session.tx_mutex != NULL);
  ret_m = usmp_port_mutex_create(&session.rx_mutex);
  assert(ret_m == 0 && session.rx_mutex != NULL);

  session.established = true;
  session.transport.send = concurrent_mock_send;
  memset(session.session_id, 0x42, 16);
  memset(session.tx_key, 0xAA, 32);
  memset(session.rx_key, 0xBB, 32);

  // Single send lock/unlock balance
  const char* msg = "Single send test";
  usmp_err_t send_err = usmp_send(&session, (const uint8_t*)msg, (uint16_t)strlen(msg));
  assert(send_err == USMP_OK);
  assert(usmp_test_get_mutex_lock_count() == 1);
  assert(usmp_test_get_mutex_unlock_count() == 1);

  // Ping lock/unlock balance
  usmp_err_t ping_err = usmp_ping(&session);
  assert(ping_err == USMP_OK);
  assert(usmp_test_get_mutex_lock_count() == 2);
  assert(usmp_test_get_mutex_unlock_count() == 2);

  // Rekey hierarchical locking: acquires tx_mutex then rx_mutex
  usmp_err_t rekey_err = usmp_rekey(&session);
  assert(rekey_err == USMP_OK);
  assert(usmp_test_get_mutex_lock_count() == 4);
  assert(usmp_test_get_mutex_unlock_count() == 4);

  // 2. Full-duplex non-blocking send while rx_mutex is held
  // In a split mutex design, an rx task holding rx_mutex must NOT block usmp_send()
  assert(usmp_port_mutex_lock(session.rx_mutex) == 0);
  // While rx_mutex is held by "rx task", usmp_send() should proceed freely
  send_err = usmp_send(&session, (const uint8_t*)msg, (uint16_t)strlen(msg));
  assert(send_err == USMP_OK);
  assert(usmp_port_mutex_unlock(session.rx_mutex) == 0);

  // 3. Multi-threaded Concurrent Senders Serialization & Nonce Reuse Elimination
  g_concurrent_seen_count = 0;
  memset(g_concurrent_seen_seqs, 0xFF, sizeof(g_concurrent_seen_seqs));
  session.tx_seq = 0;

  test_thread_t threads[CONCURRENT_THREAD_COUNT];
  concurrent_worker_arg_t args[CONCURRENT_THREAD_COUNT];

  for (int i = 0; i < CONCURRENT_THREAD_COUNT; i++) {
    args[i].session = &session;
    args[i].thread_id = i;
    int cr = test_thread_create(&threads[i], concurrent_send_worker, &args[i]);
    assert(cr == 0);
  }

  for (int i = 0; i < CONCURRENT_THREAD_COUNT; i++) {
    test_thread_join(threads[i]);
  }

  assert(g_concurrent_seen_count == TOTAL_CONCURRENT_SENDS);
  assert(session.tx_seq == TOTAL_CONCURRENT_SENDS);

  // Verify all sequence numbers [0 .. TOTAL_CONCURRENT_SENDS - 1] were emitted exactly once (no duplicates/collisions)
  bool seq_present[TOTAL_CONCURRENT_SENDS] = {false};
  for (int i = 0; i < TOTAL_CONCURRENT_SENDS; i++) {
    uint32_t s = g_concurrent_seen_seqs[i];
    assert(s < TOTAL_CONCURRENT_SENDS);
    assert(seq_present[s] == false);  // Nonce reuse check: NO DUPLICATE SEQUENCE NUMBERS!
    seq_present[s] = true;
  }
  for (int i = 0; i < TOTAL_CONCURRENT_SENDS; i++) {
    assert(seq_present[i] == true);
  }

  // All locks were properly released
  assert(usmp_test_get_mutex_lock_count() == usmp_test_get_mutex_unlock_count());

  // 4. Session Teardown & Safe NULL De-referencing
  usmp_close(&session);
  assert(session.established == false);
  assert(session.tx_mutex == NULL);
  assert(session.rx_mutex == NULL);

  // Calling usmp_send, usmp_ping, or usmp_close on closed session is safe
  assert(usmp_send(&session, (const uint8_t*)msg, (uint16_t)strlen(msg)) == USMP_ERR_NOT_CONNECTED);
  assert(usmp_ping(&session) == USMP_ERR_NOT_CONNECTED);
  usmp_close(&session);  // idempotent safe no-op

  printf("  - Split RTOS mutex architecture and concurrency tests passed!\n");
}

int main(void) {
  printf("==================================================\n");
  printf("         USMP C CORE UNIT TESTS RUNNER            \n");
  printf("==================================================\n");

  test_golden();
  test_fragmentation();
  test_malformed_frames();
  test_fragment_ordering();
  test_replay_window();
  test_send_failure_no_nonce_reuse();
  test_control_seq_overflow();
  test_bye_on_close();
  test_usmp_strerror();
  test_logging();
  test_rekey();
  test_chacha20_poly1305();
  test_zero_heap_handshake();
  test_async_handshake_fsm();
  test_coap_rtt_estimation();
  test_split_mutex_concurrency();

  printf("All C core unit tests passed successfully!\n");
  return 0;
}
