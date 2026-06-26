# Complete Build Examples & Demos

Welcome to the examples guide! Here you will find complete, copy-paste-ready project templates showing how to integrate USMP into your systems.

These projects are fully compatible with **USMP version 0.4.7** (which includes dynamic payload fragmentation, deterministic nonces, handshake public key binding, rate limiting, and timeout hardening).

## 1. Python Gateway Server

This script implements an asynchronous gateway server. It listens on port `9000`, handles different Pre-Shared Keys per device, logs telemetry, and responds to messages.

Create a file named `server.py`:

```python title="server.py"
import asyncio
import logging
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError, CryptoError, SequenceError

# Set up logging for visibility
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("usmp_gateway")

# Setup a dictionary map for device-specific Pre-Shared Keys
DEVICE_REGISTRY = {
    b"\x00\x11\x22\x33\x44\x55": b"secure-psk-device-1",
    b"\xaa\xbb\xcc\xdd\xee\xff": b"secure-psk-device-2"
}

# Bind to all interfaces on default port 9000
server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=DEVICE_REGISTRY,
    session_timeout=45.0  # Close inactive connection if no data/PING within 45s
)

@server.on_session
async def handle_device_session(session: USMPSession):
    print(f"[JOIN] Connected: device={session.device_id} (Session: {session.session_id})")
    try:
        while True:
            # Blocks until decrypted data is received (handles PING/PONG automatically)
            payload = await session.recv()
            message = payload.decode("utf-8").strip()
            print(f"[RX] From {session.device_id}: {message}")
            
            # Respond to client
            reply = f"Acknowledged: {message}"
            await session.send(reply.encode("utf-8"))
            
    except ConnectionClosedError:
        print(f"[CLOSED] Device {session.device_id} disconnected cleanly.")
    except CryptoError as e:
        print(f"[ERROR] Crypto tag verification failed on {session.device_id}: {e}")
    except SequenceError as e:
        print(f"[ERROR] Packet sequence mismatch on {session.device_id}: {e}")
    except Exception as e:
        print(f"[ERROR] Unexpected session error: {e}")

if __name__ == "__main__":
    print(f"Starting USMP Gateway on port 9000...")
    try:
        asyncio.run(server.serve())
    except KeyboardInterrupt:
        print("Gateway stopped.")
```

## 2. Arduino Client Examples

For Arduino setups, you can choose between two main structures: **Polling** (checking for data in your loop) and **Callbacks** (registering event-driven hooks).

### 2.1 The Callback-Based Client (Highly Recommended)

Callbacks keep your main `loop()` clean and prevent data racing conditions where `available()` and `read()` might execute simultaneously with internal tasks.

Create a sketch named `usmp_callbacks.ino`:

```cpp title="usmp_callbacks.ino"
#include <USMP.h>

// Wi-Fi and Server settings
#define WIFI_SSID   "YourNetworkSSID"
#define WIFI_PASS   "YourNetworkPassword"
#define SERVER_IP   "192.168.1.100" // Gateway Server IP
#define SERVER_PORT 9000

// Pre-Shared Key (This must match your device entry in the server registry)
#define PSK         "secure-psk-device-1"

USMPClient usmp(PSK);
unsigned long last_telemetry_time = 0;

// Triggered when initial connection & handshake complete successfully
void onConnect() {
    Serial.println("Session established! ID: " + usmp.sessionId());
    
    // IMPORTANT: Keepalive configuration must be set AFTER begin()!
    usmp.keepalive(15000); // Send keepalive pings every 15 seconds
    
    usmp.send("Hello Gateway! Client joined using callbacks.");
}

// Triggered if the connection drops
void onDisconnect() {
    Serial.println("Connection lost. Reconnecting in the background...");
}

// Triggered when a reconnection handshake completes successfully
void onReconnect() {
    Serial.println("Session restored! New Session ID: " + usmp.sessionId());
}

// Triggered when a decrypted payload arrives from the gateway
void onMessage(const uint8_t *data, size_t len) {
    Serial.print("Got message: ");
    Serial.write(data, len);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("[APP] Starting event-driven USMP Client...");

    // Register our events before connecting
    usmp.onConnect(onConnect);
    usmp.onDisconnect(onDisconnect);
    usmp.onReconnect(onReconnect);
    usmp.onMessage(onMessage);

    // Connecting handles WiFi join and TCP handshake automatically
    usmp.begin(USMP::TCP(SERVER_IP, SERVER_PORT).wifi(WIFI_SSID, WIFI_PASS));
}

void loop() {
    // Keep driving the network tasks and triggering callbacks
    usmp.maintain();

    // Send a telemetry packet every 10 seconds while the connection is healthy
    if (usmp.alive() && (millis() - last_telemetry_time >= 10000)) {
        last_telemetry_time = millis();
        String telemetry = "Uptime = " + String(millis() / 1000) + "s";
        Serial.println("Sending: " + telemetry);
        usmp.send(telemetry);
    }

    delay(10); // Yield to background core processor
}
```

> [!CAUTION]
> **Avoid Mixed Reading Modes**
> If you register an `onMessage` callback handler, do not call `usmp.available()` or `usmp.read()`. Mixing the two models will consume packets twice or prevent the callback from triggering correctly!

### 2.2 The Polling-Based Client (Simple Style)

If you prefer checking for data sequentially alongside other sensor loops, you can use the polling API:

Create a sketch named `usmp_polling.ino`:

```cpp title="usmp_polling.ino"
#include <USMP.h>

#define WIFI_SSID   "YourNetworkSSID"
#define WIFI_PASS   "YourNetworkPassword"
#define SERVER_IP   "192.168.1.100"
#define SERVER_PORT 9000
#define PSK         "secure-psk-device-1"

USMPClient usmp(PSK);
unsigned long last_telemetry_time = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("[APP] Starting polling-driven USMP Client...");

    if (!usmp.begin(USMP::TCP(SERVER_IP, SERVER_PORT).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("Connection failed!");
        return;
    }

    // Set keepalive post-connection
    usmp.keepalive(15000);

    Serial.println("Session established! ID: " + usmp.sessionId());
    usmp.send("Hello Gateway! Client joined using polling.");
}

void loop() {
    // Maintain connection heartbeats and background reconnect routines
    usmp.maintain();

    // Poll for new incoming secure messages
    if (usmp.available()) {
        String msg = usmp.read();
        Serial.println("Received: " + msg);
    }

    // Telemetry loop
    if (usmp.alive() && (millis() - last_telemetry_time >= 10000)) {
        last_telemetry_time = millis();
        String telemetry = "Telemetry Uptime = " + String(millis() / 1000) + "s";
        usmp.send(telemetry);
    }

    delay(10);
}
```

## 3. ESP-IDF Native C Client

This native C project compiles under the Espressif IoT Development Framework (v5.0+). It features:

* **Static IP WiFi Setup**: Configures a static IP connection without standard DHCP warnings on boot.
* **Non-Blocking Event Loop**: Periodically checks the transport layer for incoming data, ticks the keepalive state machine, and implements a reconnect loop with an exponential backoff.

#### wifi.c (Static IP Configuration)

```c title="main/wifi.c"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include <string.h>

#define WIFI_SSID       "YourNetworkSSID"
#define WIFI_PASS       "YourNetworkPassword"
#define STATIC_IP       "192.168.1.150"
#define STATIC_GW       "192.168.1.1"
#define STATIC_NETMASK  "255.255.255.0"

static const char *TAG = "WIFI_MANAGER";
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from AP. Retrying connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Successfully joined AP. Static IP configuration active.");
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

    // Turn off DHCP client safely before applying Static IP settings
    esp_err_t dhcp_err = esp_netif_dhcpc_stop(netif);
    if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_INVALID_STATE && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop DHCP: %s", esp_err_to_name(dhcp_err));
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

#### app.c (Main Session Tasks)

```c title="main/app.c"
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
    ESP_LOGI(TAG, "Initializing Wi-Fi Interface...");
    if (!wifi_init()) {
        ESP_LOGE(TAG, "Failed to connect to AP.");
        return;
    }

    const char *server_ip = "192.168.1.100";
    const int port = USMP_DEFAULT_PORT;

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // Configure client context with the Pre-Shared Key (matches server side)
    static const uint8_t s_psk[] = "secure-psk-device-1";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk);

    // Initialize TCP socket connection
    if (usmp_transport_tcp_init(&transport, server_ip, port) != 0) {
        ESP_LOGE(TAG, "Failed to open TCP socket connection.");
        return;
    }
    ESP_LOGI(TAG, "TCP Socket open. Performing USMP Cryptographic Handshake...");

    // Connect & authenticate
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP Handshake failed.");
        return;
    }
    
    ESP_LOGI(TAG, "Secure session established.");
    ctx.keepalive_ms = 15000; // Trigger keepalive PING if inactive for 15 seconds

    // Send a welcome message
    const char *greet = "Hello gateway! This is ESP-IDF native C speaking.";
    usmp_send(&ctx, (const uint8_t *)greet, strlen(greet));

    // Active session receiver loop
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // Yield to context switching

        // 1. Non-blocking Receive Check
        if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0)
        {
            uint8_t rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES + 1];
            int len = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN * USMP_MAX_FRAMES);
            if (len > 0)
            {
                rx_buf[len] = '\0';
                ESP_LOGI(TAG, "Received: %s", (char *)rx_buf);
            }
        }

        // 2. Tick Keepalive (sends a PING frame automatically if no packets were sent)
        if (usmp_keepalive_tick(&ctx) == 0) {
            continue; // Keepalive tick normal, loop continues
        }

        // 3. Keepalive tick returned < 0 -> connection was lost! Run reconnect backoff.
        ESP_LOGW(TAG, "Connection lost! Attempting reconnect...");
        int backoff_ms = 2000;
        
        while (usmp_reconnect(&ctx) != 0)
        {
            ESP_LOGW(TAG, "Reconnect failed. Retrying in %dms...", backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            
            // Exponential backoff capped at 30 seconds
            if (backoff_ms < 30000) {
                backoff_ms *= 2; 
            }
        }
        ESP_LOGI(TAG, "Session restored successfully. Resuming operations.");
    }
}
```
