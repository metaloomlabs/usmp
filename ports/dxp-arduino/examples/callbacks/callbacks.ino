#include <DXP.h>

#define PSK        "dxp-dev-psk-change-me-before-prod"
#define SERVER_IP  "192.168.137.1"
#define WIFI_SSID  "YourNetwork"
#define WIFI_PASS  "YourPassword"

DXPClient dxp(PSK);

// ── Callbacks ─────────────────────────────────────────────────────────────────

void onConnect() {
    Serial.println("[DXP] Connected — session: " + dxp.sessionId());
    dxp.send("hello from arduino");
}

void onDisconnect() {
    Serial.println("[DXP] Disconnected — maintain() will reconnect");
}

void onReconnect() {
    Serial.println("[DXP] Reconnected — new session: " + dxp.sessionId());
    dxp.send("reconnected");
}

void onMessage(const uint8_t *data, size_t len) {
    Serial.printf("[DXP] RX (%d bytes): %.*s\n", len, len, data);
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    dxp.keepalive(15000);        // PING every 15s
    dxp.onConnect(onConnect);
    dxp.onDisconnect(onDisconnect);
    dxp.onReconnect(onReconnect);
    dxp.onMessage(onMessage);

    dxp.begin(DXP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS));
    // Callbacks fire automatically — no need to check return value here
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    dxp.maintain(); // drives everything
}