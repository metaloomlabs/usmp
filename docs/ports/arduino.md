# Arduino Library Manual

Welcome, Arduino creators! The Arduino port of USMP wraps the pure C protocol core in an ergonomic, developer-friendly C++ class: `USMPClient`. This guide will walk you through the library's structure, API references, and event callbacks.

## Library Layout

When you install USMP inside your Arduino IDE or PlatformIO environment, it unpacks into this structure:

* `src/`
  * `USMP.h` — Declarations for the `USMPClient` and transport classes.
  * `USMP.cpp` — Core client implementation wrapper.
  * `USMPTransport.h` / `USMPTransport.cpp` — TCP socket bindings.
  * `usmp_port_arduino.cpp` — Connects the C core to the Arduino hardware functions (RNG, timers).
* `examples/`
  * `basic/` — Sequential polling read-and-send demo.
  * `callbacks/` — Asynchronous, event-driven callbacks demo.

## Constructor Reference

```cpp
USMPClient(const char *psk);
```

Creates a new USMP client instance.

* **`psk`**: The secret Pre-Shared Key. This must match the key configured on your gateway server!

## Connection & Session Management

### `begin`

```cpp
bool begin(USMPTCPTransport transport);
```

Establishes the TCP connection and completes the cryptographic handshake.

* If you configured Wi-Fi parameters on the transport helper, `begin()` will block until the Wi-Fi connection is fully active before starting the handshake.
* **Returns**: `true` if the session was established successfully, `false` otherwise.

### `maintain`

```cpp
void maintain();
```

Performs background driver maintenance. **You must call this inside your main `loop()` function!**

* Monitors the TCP stream for incoming packets and fires registered callbacks.
* Sends keepalive heartbeats (`PING`) and handles replies (`PONG`).
* Manages automatic reconnection attempts with an exponential backoff (starting at 2 seconds and capping at 30 seconds) if the network drops.

### `keepalive`

```cpp
void keepalive(uint32_t ms);
```

Sets the interval (in milliseconds) at which keepalive pings are sent (default is `30000` ms / 30 seconds).

> [!CAUTION]
> **Keepalive Configuration Order**
> You MUST call `keepalive()` **after** calling `begin()`.
> Under the hood, `begin()` clears the client memory layout using `memset`, which overrides any custom keepalive interval back to the default 30 seconds.

### `alive`

```cpp
bool alive();
```

Checks if your secure session is currently connected and active.

### `reconnect`

```cpp
bool reconnect();
```

Forces the client to disconnect and perform a fresh handshake.

### `close`

```cpp
void close();
```

Gracefully tells the server we are leaving (sends a `BYE` frame) and shuts down the connection.

## Sending and Receiving Data

### `send`

```cpp
bool send(const char *str);
bool send(const String &str);
bool send(const uint8_t *data, size_t len);
```

Encrypts and transmits data to the server.

* Supports automatic fragmentation for payloads up to ~1.8 KB!
* **Returns**: `true` on success, `false` if the transmit failed.

### Polling API (Simple Style)

If you prefer checking for data sequentially inside your `loop()`, you can use the polling API:

#### `available`

```cpp
bool available();
```

Returns `true` if there are decrypted data bytes waiting to be read.

#### `read`

```cpp
String read();
int read(uint8_t *buf, size_t max_len);
```

Reads the next decrypted packet. Calling `read()` returns the message as a `String`. Providing a buffer copies raw bytes and returns the length.

### Callback API (Recommended Style)

Callbacks let you write clean, event-driven code. Register your functions during `setup()` and let `maintain()` do the rest:

```cpp
void onConnect(void (*cb)());
void onDisconnect(void (*cb)());
void onReconnect(void (*cb)());
void onMessage(void (*cb)(const uint8_t *data, size_t len));
```

* **`onConnect`**: Triggered when the initial connection and handshake succeed.
* **`onDisconnect`**: Triggered when the connection drops.
* **`onReconnect`**: Triggered when a reconnection handshake completes.
* **`onMessage`**: Triggered when new decrypted data is received. Provides the raw byte buffer and length.

> [!IMPORTANT]
> **Do Not Mix Callbacks and Polling**
> If you register an `onMessage` callback, **do not** call `usmp.available()` or `usmp.read()`. Doing so will interfere with the callback thread and cause packets to be skipped or lost.

## Recommended Callback Example

Here is the cleanest way to structure your USMP sketch using callbacks:

```cpp title="callbacks_demo.ino"
#include <USMP.h>

#define PSK "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "192.168.1.100"

USMPClient usmp(PSK);

void onConnect() {
    Serial.println("Session Active! ID: " + usmp.sessionId());
    
    // Configure custom keepalive AFTER begin() is complete!
    usmp.keepalive(15000); 
    
    usmp.send("Hello Gateway!");
}

void onDisconnect() {
    Serial.println("Connection lost. Retrying in background...");
}

void onMessage(const uint8_t *data, size_t len) {
    Serial.printf("Received %d bytes: ", len);
    Serial.write(data, len);
    Serial.println();
}

void setup() {
    Serial.begin(115200);

    // Register our events
    usmp.onConnect(onConnect);
    usmp.onDisconnect(onDisconnect);
    usmp.onMessage(onMessage);

    // Connect to the gateway server
    usmp.begin(USMP::TCP(SERVER_IP));
}

void loop() {
    // Keep driving USMP maintenance
    usmp.maintain();
    delay(10);
}
```

## Transport Configurations

The `USMP::TCP` namespace provides a fluent builder to configure your transport layer:

```cpp
USMP::TCP(const char *host, uint16_t port = 9000);
```

* **`host`**: Server IP address or hostname.
* **`port`**: Server TCP port (defaults to `9000`).

### Managing Wi-Fi Connections

If your project is already managing Wi-Fi, pass only the server details:

```cpp
usmp.begin(USMP::TCP("192.168.1.100"));
```

If you want USMP to manage the Wi-Fi connection for you, append `.wifi(ssid, password)`:

```cpp
usmp.begin(USMP::TCP("192.168.1.100").wifi("SSID", "PASSWORD"));
```

## Properties

### `deviceId`

```cpp
String deviceId();
```

Returns the local device MAC address as a formatted string (e.g. `ab:cd:ef:01:02:03`).

### `sessionId`

```cpp
String sessionId();
```

Returns the active session ID as a hex string (e.g. `5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d`).
