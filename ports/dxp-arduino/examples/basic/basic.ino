#include <DXP.h>

// ── Config ────────────────────────────────────────────────────────────────────
#define PSK        "dxp-dev-psk-change-me-before-prod"
#define SERVER_IP  "192.168.137.1"
#define WIFI_SSID  "YourNetwork"
#define WIFI_PASS  "YourPassword"

DXPClient dxp(PSK);

void setup() {
    Serial.begin(115200);

    // Connect WiFi + TCP + handshake in one call
    if (!dxp.begin(DXP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("DXP connect failed — check server and PSK");
        return;
    }

    Serial.println("Connected!");
    Serial.println("Device:  " + dxp.deviceId());
    Serial.println("Session: " + dxp.sessionId());

    dxp.send("hello from arduino");
}

void loop() {
    dxp.maintain(); // keepalive + reconnect — must call every loop

    if (dxp.available()) {
        String msg = dxp.read();
        Serial.println("RX: " + msg);
    }
}