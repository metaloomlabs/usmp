// examples/aws_ec2_test/arduino/arduino.ino
#include <USMP.h>

// ── USMP EC2 Configuration
// ────────────────────────────────────────────────────────────────────
// WARNING: Do NOT use hardcoded PSK constants in production environments.
// For initial testing, you can match this with the server's PSK.
#define PSK "usmp-dev-psk-change-me-before-prod"

// Replace this with the Public IP (IPv4) of your AWS EC2 Instance.
// Make sure your EC2 Security Group allows inbound TCP traffic on port 9000.
#define EC2_PUBLIC_IP "YOUR_EC2_PUBLIC_IP"
#define SERVER_PORT   9000

// Enter your local Wi-Fi credentials so the ESP32 can connect to the internet.
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"

// Create the USMP client instance with the Pre-Shared Key
USMPClient usmp(PSK);

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000; // Send a message every 5 seconds
int messageCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n--- USMP AWS EC2 Client Test ---");
  
  // Set log level to see verbose handshake detail (highly recommended for troubleshooting)
  usmp.setLogLevel(USMP_LOG_LEVEL_INFO);

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  Serial.print("Connecting to USMP EC2 Server: ");
  Serial.print(EC2_PUBLIC_IP);
  Serial.print(":");
  Serial.println(SERVER_PORT);

  // Connect to Wi-Fi, connect to the EC2 TCP server, and perform the secure handshake.
  // Note: TCP(ip, port) specifies the target address and port.
  if (!usmp.begin(USMP::TCP(EC2_PUBLIC_IP, SERVER_PORT).wifi(WIFI_SSID, WIFI_PASS))) {
    Serial.println("\n[ERROR] USMP connection failed!");
    Serial.println("Verify:");
    Serial.println("  1. The server_ec2.py script is running on the EC2 instance.");
    Serial.println("  2. The EC2 Security Group allows TCP traffic on port 9000.");
    Serial.println("  3. Your Wi-Fi credentials are correct and the ESP32 has internet access.");
    Serial.println("  4. The PSK matches on both the server and client.");
    return;
  }

  Serial.println("\n[SUCCESS] Secure USMP session established!");
  Serial.print("  Device ID:  ");
  Serial.println(usmp.deviceId());
  Serial.print("  Session ID: ");
  Serial.println(usmp.sessionId());
  
  // Send the first secure message
  usmp.send("Hello EC2, this is ESP32 via USMP!");
}

void loop() {
  // Important: Maintain keepalives and automatic reconnects in the background.
  // This must be called frequently in the loop.
  usmp.maintain();

  // Handle incoming decrypted messages from the EC2 server
  if (usmp.available()) {
    String msg = usmp.read();
    Serial.print("Received from EC2: ");
    Serial.println(msg);
  }

  // Periodically send data to the server
  unsigned long currentMillis = millis();
  if (currentMillis - lastSendTime >= sendInterval) {
    lastSendTime = currentMillis;
    messageCount++;
    
    // Construct and send a message securely
    String payload = "ESP32 Ping #" + String(messageCount) + " (Uptime: " + String(currentMillis / 1000) + "s)";
    Serial.print("Sending: ");
    Serial.println(payload);
    
    if (usmp.send(payload)) {
      Serial.println("  Sent successfully (encrypted).");
    } else {
      Serial.println("  Send failed. Session might be reconnecting...");
    }
  }
}
