#include <USMP.h>

// ── Config
// ────────────────────────────────────────────────────────────────────
// WARNING: Do NOT use hardcoded PSK constants in production environments.
#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "[IP_ADDRESS]"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

// Most ESP32 dev boards use GPIO 2 for the built-in LED.
// We use a literal to avoid lambda capture and board-specific header definition issues.
#define LED_PIN 2

USMPClient usmp(PSK);

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off

  // Setup callbacks
  usmp.onConnect([]() {
    Serial.println("[USMP] Connected to gateway.");
    usmp.send("STATUS: READY");
  });

  usmp.onMessage([](const uint8_t *data, size_t len) {
    // Convert message payload to string
    String msg = "";
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
    msg.trim();
    msg.toUpperCase();

    Serial.println("Received command: " + msg);

    if (msg == "LED_ON") {
      digitalWrite(LED_PIN, HIGH);
      usmp.send("STATUS: LED IS ON");
    } else if (msg == "LED_OFF") {
      digitalWrite(LED_PIN, LOW);
      usmp.send("STATUS: LED IS OFF");
    } else {
      usmp.send("ERROR: UNKNOWN_CMD");
    }
  });

  // Start USMP UDP transport
  // Choose either TCP or UDP (uncomment the one you want to use)
  // auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);
  auto transport = USMP::UDP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);

  if (!usmp.begin(transport)) {
    Serial.println("Initial connect failed. Retrying in loop...");
  }
}

void loop() {
  // Drives the keepalives and incoming packet processing.
  // When a message is received, the onMessage callback will execute.
  usmp.maintain();
}
