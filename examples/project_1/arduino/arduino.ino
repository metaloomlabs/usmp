// examples/project_1/arduino/arduino.ino
#include <USMP.h>

// ── Config
// ────────────────────────────────────────────────────────────────────
// WARNING: Do NOT use hardcoded PSK constants in production environments.
// In production, provision and load the PSK from a secure storage mechanism
// (e.g. EEPROM, Flash secure partition, or over a secure provisioning protocol).
#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "10.52.94.175"
#define WIFI_SSID "connecting....."
#define WIFI_PASS "x64.winter"

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
  usmp.maintain();  // keepalive + reconnect — must call every loop

  if (usmp.available()) {
    String msg = usmp.read();
    Serial.println("RX: " + msg);
  }
}
