#include <USMP.h>

#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "[IP_ADDRESS]"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

// Callbacks ─────────────────────────────────────────────────────────────────

void onConnect() {
  Serial.println("[USMP] Connected — session: " + usmp.sessionId());
  usmp.send("hello from arduino");
}

void onDisconnect() {
  Serial.println("[USMP] Disconnected — maintain() will reconnect");
}

void onReconnect() {
  Serial.println("[USMP] Reconnected — new session: " + usmp.sessionId());
  usmp.send("reconnected");
}

void onMessage(const uint8_t *data, size_t len) {
  Serial.printf("[USMP] RX (%d bytes): %.*s\n", len, len, data);
}

// Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  usmp.keepalive(15000); // PING every 15s
  usmp.onConnect(onConnect);
  usmp.onDisconnect(onDisconnect);
  usmp.onReconnect(onReconnect);
  usmp.onMessage(onMessage);

  usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS));
  // Callbacks fire automatically — no need to check return value here
}

// Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  usmp.maintain(); // drives everything
}