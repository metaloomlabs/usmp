#include <USMP.h>

// ── Config
// ────────────────────────────────────────────────────────────────────
// WARNING: Do NOT use hardcoded PSK constants in production environments.
// In production, provision and load the PSK from a secure storage mechanism
// (e.g. EEPROM, Flash secure partition, or over a secure provisioning protocol).
#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "[IP_ADDRESS]"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

void setup() {
  Serial.begin(115200);

  // Connect WiFi + TCP + handshake in one call
  if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
    Serial.println("USMP connect failed — check server and PSK");
    return;
  }

  Serial.println("Connected!");
  Serial.println("Device:  " + usmp.deviceId());
  Serial.println("Session: " + usmp.sessionId());

  usmp.send("hello from arduino");
}

void loop() {
  usmp.maintain(); // keepalive + reconnect — must call every loop

  if (usmp.available()) {
    String msg = usmp.read();
    Serial.println("RX: " + msg);
  }
}