# USMP Full-Build Demos & Examples

This document provides a set of complete, copy-paste-ready build examples demonstrating how to integrate USMP on different platforms. It includes:
1. **Python Gateway Server & Test Client**
2. **Arduino ESP32 Client** (using the USMP-Arduino port)
3. **ESP-IDF Client** (native C with secure Static IP and non-blocking RX loops)

---

## 1. Python Gateway Server

### Explanation
An asyncio-based USMP server that listens on port `9000` for incoming device connections, authenticates using the pre-shared key (PSK), and acts as a bidirectional message processor.

### Implementation
Create a file named `server.py`:
```python
import asyncio
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError, CryptoError, SequenceError

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

# Configure the server with a 45-second inactivity timeout
server = USMPServer(host=HOST, port=PORT, psk=PSK, session_timeout=45.0)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"[SESSION] Connected: device={session.device_id} (Session: {session.session_id})")
    try:
        while True:
            # Blocks until decrypted data is received (handles PING/PONG automatically)
            payload = await session.recv()
            message = payload.decode().strip()
            print(f"[RX] From {session.device_id}: {message}")
            
            # Respond to client
            response_msg = f"Acknowledged: {message}"
            await session.send(response_msg.encode())
            
    except ConnectionClosedError:
        print(f"[CLOSED] Device {session.device_id} disconnected cleanly.")
    except (CryptoError, SequenceError) as e:
        print(f"[ERROR] Security violation on {session.device_id}: {e}")
    except Exception as e:
        print(f"[ERROR] Unexpected session exception: {e}")

if __name__ == "__main__":
    print(f"Starting USMP Gateway on {HOST}:{PORT}...")
    try:
        asyncio.run(server.serve())
    except KeyboardInterrupt:
        print("Gateway stopped.")
```

---

## 2. Arduino Client Demo

### Explanation
A complete sketch for ESP32 microcontrollers using the Arduino framework. It manages WiFi connection, initiates the USMP session, and schedules non-blocking checks in the `loop()` task using `usmp.maintain()`.

### Implementation
Create a sketch named `usmp_arduino_demo.ino`:
```cpp
#include <USMP.h>

// ── Configuration Settings
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"
#define SERVER_IP "192.168.137.1"  // Gateway IP address
#define SERVER_PORT 9000

// Pre-Shared Key (Warning: Do not use hardcoded PSK constants in production)
#define USMP_PSK "usmp-dev-psk-change-me-before-prod"

USMPClient usmp(USMP_PSK);
unsigned long last_send_time = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("[APP] Initializing USMP Client...");

  // USMP handles Wi-Fi connections, socket dialing, and the handshake in one call.
  // Note: Custom configurations (like keepalive) must be set AFTER begin().
  if (!usmp.begin(USMP::TCP(SERVER_IP, SERVER_PORT).wifi(WIFI_SSID, WIFI_PASS))) {
    Serial.println("[USMP] Connection/Handshake failed. Check configuration and server state.");
    return;
  }

  // Set keepalive to 15 seconds (must be set after begin due to context initialization)
  usmp.keepalive(15000);

  Serial.println("[USMP] Handshake successful!");
  Serial.printf("[USMP] Device ID: %s\n", usmp.deviceId().c_str());
  Serial.printf("[USMP] Session ID: %s\n", usmp.sessionId().c_str());

  // Transmit initial welcome frame
  usmp.send("Hello from Arduino ESP32");
}

void loop() {
  // 1. Maintain the connection (sends keepalive PINGs, manages reconnects)
  usmp.maintain();

  // 2. Poll for incoming data (non-blocking)
  if (usmp.available()) {
    String message = usmp.read();
    Serial.printf("[USMP] Received message: %s\n", message.c_str());
  }

  // 3. Periodically transmit telemetry every 10 seconds if connected
  if (usmp.alive() && (millis() - last_send_time >= 10000)) {
    last_send_time = millis();
    String telemetry = "Telemetry uptime=" + String(millis() / 1000) + "s";
    Serial.printf("[USMP] Transmitting: %s\n", telemetry.c_str());
    usmp.send(telemetry);
  }

  delay(10); // yields to CPU/Wi-Fi tasks
}
```

---

## 3. ESP-IDF Native C Client

### Explanation
A native Espressif IoT Development Framework (ESP-IDF) C project. This demo includes:
1. **`wifi.c`**: Connects to the access point with a **static IP configuration**, safely ignoring standard DHCP state warnings during boot.
2. **`app.c`**: Initializes the TCP transport, executes `usmp_connect()`, and maintains a non-blocking loop that reads incoming data (`usmp_recv`) and ticks the keepalive timer.

### Implementation

#### Component 1: `wifi.c` (Safe Static IP Initialization)
```c
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include <string.h>

#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASS       "your-wifi-password"
#define STATIC_IP       "192.168.137.100"
#define STATIC_GW       "192.168.137.1"
#define STATIC_NETMASK  "255.255.255.0"

static const char *TAG = "WIFI_CONFIG";
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from AP, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Associated with AP. Static IP configuration active: " STATIC_IP);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Stop DHCP and apply Static IP parameters safely without triggering crashes
    esp_err_t dhcp_err = esp_netif_dhcpc_stop(netif);
    if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_INVALID_STATE && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Fatal DHCP client stop error: %s", esp_err_to_name(dhcp_err));
        return false;
    }

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip4addr_aton(STATIC_IP, (ip4_addr_t *)&ip_info.ip);
    ip4addr_aton(STATIC_GW, (ip4_addr_t *)&ip_info.gw);
    ip4addr_aton(STATIC_NETMASK, (ip4_addr_t *)&ip_info.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));

    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
```

#### Component 2: `app.c` (Main Session & Non-blocking RX Loop)
```c
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "APP_CORE";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Station interface...");
    if (!wifi_init()) {
        ESP_LOGE(TAG, "WiFi registration timed out.");
        return;
    }

    const char *server_ip = "192.168.137.1";
    const int port = USMP_DEFAULT_PORT;

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // Pre-shared key definition
    static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1;

    // Dial TCP socket
    if (usmp_transport_tcp_init(&transport, server_ip, port) != 0) {
        ESP_LOGE(TAG, "TCP socket connection failed.");
        return;
    }
    ESP_LOGI(TAG, "TCP Link established. Executing USMP handshake...");

    // Perform cryptographic handshake
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP Handshake failed.");
        return;
    }
    ESP_LOGI(TAG, "Session established successfully.");
    ctx.keepalive_ms = 15000; // Tick keepalive timer every 15s

    // Transmit initial data
    const char *greet = "Hello gateway from ESP-IDF client";
    usmp_send(&ctx, (const uint8_t *)greet, strlen(greet));

    // Active session reader/writer loop
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // Yield to context switches

        // 1. Non-blocking RX Check: poll transport for incoming data
        if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0)
        {
            uint8_t rx_buf[USMP_MAX_DATA_LEN + 1];
            int bytes_read = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN);
            if (bytes_read > 0)
            {
                rx_buf[bytes_read] = '\0';
                ESP_LOGI(TAG, "[RX] Received payload: %s", (char *)rx_buf);
            }
        }

        // 2. Keepalive timer tick (sends PING when transmit inactivity exceeds 15 seconds)
        if (usmp_keepalive_tick(&ctx) == 0) {
            continue; // Normal execution path
        }

        // 3. Fallthrough means connection was lost -> execute reconnect sequence
        ESP_LOGW(TAG, "USMP link dropped. Attempting to reconnect...");
        int backoff_ms = 2000;
        
        while (usmp_reconnect(&ctx) != 0)
        {
            ESP_LOGW(TAG, "Reconnect failed. Retrying in %dms...", backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            if (backoff_ms < 30000) {
                backoff_ms *= 2; // Exponential backoff capped at 30s
            }
        }
        ESP_LOGI(TAG, "Session re-established. Resuming normal operations.");
    }
}
```
