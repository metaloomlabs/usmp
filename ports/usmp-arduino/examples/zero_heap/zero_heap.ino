#include <USMP.h>

// WARNING: Do NOT use hardcoded PSK constants in production environments.
// In production, provision and load the PSK from a secure storage mechanism
// (e.g. EEPROM, Flash secure partition, or over a secure provisioning protocol).
#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "[IP_ADDRESS]"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

/*
 * Zero-Heap / Static Memory Architecture:
 *
 * 1. USMPClientStatic<RxBufferSize, ScratchBufferSize>:
 *    Embeds the RX buffer (default: 1808 bytes) and the cryptographic handshake
 *    scratchpad buffer (default: 1024 bytes) directly inside object memory.
 *    When allocated in global BSS storage, 0 bytes are allocated on heap.
 *
 * 2. Static Transport Context (USMPArduinoTcpCtx / USMPArduinoUdpCtx):
 *    Allocated statically in global storage, eliminating dynamic 'new' and 'delete'
 *    in transport initialization and teardown.
 */
static USMPClientStatic<> usmp(PSK);
static USMPArduinoTcpCtx tcp_ctx;
// Or for UDP: static USMPArduinoUdpCtx udp_ctx;

void onConnect() {
  Serial.println("[USMP] Connected (Zero-Heap Mode) — session: " + usmp.sessionId());
  usmp.send("hello from zero-heap arduino client");
}

void onDisconnect() {
  Serial.println("[USMP] Disconnected — maintain() will reconnect with static memory");
}

void onMessage(const uint8_t* data, size_t len) {
  Serial.printf("[USMP] RX (%u bytes): %.*s\n", (unsigned int)len, (int)len, data);
}

void setup() {
  Serial.begin(115200);

  usmp.onConnect(onConnect);
  usmp.onDisconnect(onDisconnect);
  usmp.onMessage(onMessage);

  // Configure WiFi and TCP transport
  auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);
  // For UDP: auto transport = USMP::UDP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);

  // Pass static transport context pointer to begin(): 0 heap allocations
  if (!usmp.begin(transport, &tcp_ctx)) {
    // For UDP: if (!usmp.begin(transport, &udp_ctx)) {
    Serial.println("[USMP] Connection failed");
    return;
  }

  Serial.println("[USMP] Session established successfully without heap allocations!");
}

void loop() {
  usmp.maintain(); // Keepalive, incoming frames, and non-blocking reconnect
}
