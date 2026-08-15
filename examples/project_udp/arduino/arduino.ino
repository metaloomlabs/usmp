#include <USMP.h>

// ── Config
// ────────────────────────────────────────────────────────────────────
// WARNING: Do NOT use hardcoded PSK constants in production environments.
// In production, provision and load the PSK from a secure storage mechanism.
#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "192.168.1.100"
#define WIFI_SSID "connecting....."
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

USMPClient usmp(PSK);

void setup() {
  Serial.begin(115200);

  // By default, only errors are logged. Uncomment below to see details.
  // usmp.setLogLevel(USMP_LOG_LEVEL_INFO);

  // Connect WiFi + UDP + handshake in one call
  if (!usmp.begin(USMP::UDP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
    Serial.println("USMP UDP connect failed — check server and PSK");
    return;
  }

  Serial.println("Connected over UDP!");
  Serial.println("Device:  " + usmp.deviceId());
  Serial.println("Session: " + usmp.sessionId());

  usmp.send("hello from arduino over UDP");
}

void loop() {
  usmp.maintain();  // keepalive + reconnect — must call every loop

  if (usmp.available()) {
    String msg = usmp.read();
    Serial.println("RX-UDP: " + msg);
  }
}
