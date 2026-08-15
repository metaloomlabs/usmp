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

#define PUBLISH_INTERVAL_MS 10000  // Send data every 10 seconds

USMPClient usmp(PSK);
uint32_t lastPublishTime = 0;

void setup() {
  Serial.begin(115200);

  // Configure callbacks for connection state monitoring
  usmp.onConnect([]() { Serial.println("[USMP] Session established: " + usmp.sessionId()); });

  usmp.onDisconnect([]() { Serial.println("[USMP] Disconnected — will attempt auto-reconnect"); });

  // Start connection. You can use USMP::UDP or USMP::TCP
  Serial.println("[USMP] Connecting to WiFi and gateway...");

  // Choose either TCP or UDP (uncomment the one you want to use)
  // auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);
  auto transport = USMP::UDP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);

  if (!usmp.begin(transport)) {
    Serial.println("[USMP] Initial connection failed. maintain() will retry.");
  }
}

void loop() {
  // Drives handshakes, keepalives, and automatic reconnection backoffs.
  // Must be called as frequently as possible in the loop.
  usmp.maintain();

  // If the session is established, periodically publish telemetry data
  if (usmp.alive()) {
    uint32_t now = millis();
    if (now - lastPublishTime >= PUBLISH_INTERVAL_MS) {
      lastPublishTime = now;

      // Mock sensor reading (e.g. Temperature / Humidity)
      float temp = 20.0 + random(0, 150) / 10.0;
      float hum = 40.0 + random(0, 400) / 10.0;

      // Format payload as JSON
      char payload[64];
      snprintf(payload, sizeof(payload), "{\"temp\":%.1f,\"hum\":%.1f}", temp, hum);

      Serial.print("Publishing telemetry: ");
      Serial.println(payload);

      if (usmp.send(payload)) {
        Serial.println("Publish success");
      } else {
        Serial.println("Publish failed");
      }
    }
  }

  // Handle any incoming command or confirmation messages from the gateway
  if (usmp.available()) {
    String msg = usmp.read();
    if (msg.length() > 0) {
      Serial.println("Received from gateway: " + msg);
    }
  }
}
