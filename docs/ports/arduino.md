# Arduino Library Reference

Welcome, Arduino developers! The Arduino port of USMP wraps the core C protocol in an ergonomic, developer-friendly C++ class: `USMPClient`. This guide walks you through the library structure, API signatures, and transport configurations.

---

## Installation

USMP is packaged as a standard offline ZIP library to keep private repository references out of your builds:

1.  Locate `usmp-X.Y.Z-arduino.zip` in your releases archive.
2.  Open **Arduino IDE**.
3.  Go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
4.  Choose the zip file to complete the import.

To include it in your sketch:
```cpp
#include <USMP.h>
```

---

## API Reference

### Constructor

```cpp
USMPClient(const char *psk);
```
Creates a new USMP client instance.
*   **`psk`**: The Pre-Shared Key. This must match the key configured on your gateway.

---

### Connection Management

#### `begin` (TCP)
```cpp
bool begin(USMPTCPTransport transport);
```
Establishes the connection and performs the handshake over TCP.
*   If Wi-Fi SSID and password are configured on the transport, `begin()` automatically connects to Wi-Fi first.

#### `begin` (UDP)
```cpp
bool begin(USMPUDPTransport transport);
```
Establishes the connection and performs the handshake over UDP.
*   If Wi-Fi SSID and password are configured on the transport, `begin()` automatically connects to Wi-Fi first.

#### `maintain`
```cpp
void maintain();
```
Performs background driver tasks. **You must call this inside your main `loop()` function.**
*   Checks for incoming data, triggers registered callbacks, handles keepalive pings, and automatically reconnects if the socket drops.

#### `keepalive`
```cpp
void keepalive(uint32_t ms);
```
Sets the interval (in milliseconds) for keepalive heartbeat checks (default: `30000` / 30 seconds).

> [!CAUTION]
> **Order of Configuration**
> You must call `keepalive()` **after** calling `begin()`. The `begin()` method executes a `memset` on the internal context, which overrides any custom keepalive interval back to the default 30 seconds.

#### `alive`
```cpp
bool alive();
```
Checks if your secure session is currently connected and active.

#### `close`
```cpp
void close();
```
Gracefully closes the session (sends a `BYE` frame) and shuts down the connection.

---

### Sending and Receiving Data

#### `send`
```cpp
bool send(const char *str);
bool send(const String &str);
bool send(const uint8_t *data, size_t len);
```
Encrypts and transmits a payload to the gateway.
*   Supports automatic fragmentation for payloads up to ~1.8 KB.

---

### Polling API (Simple Style)
If you prefer a simple sequential flow, you can query the client inside your `loop()`:

```cpp
if (usmp.available()) {
    String msg = usmp.read();
    Serial.println(msg);
}
```

*   **`available()`**: Returns `true` if a decrypted packet is ready.
*   **`read()`**: Reads the next decrypted packet as a `String`.
*   **`read(buf, max_len)`**: Copies raw decrypted bytes into a buffer and returns the length.

---

### Callback API (Asynchronous Style)
For event-driven sketches, register callbacks in your `setup()`:

```cpp
void onConnect(void (*cb)());
void onDisconnect(void (*cb)());
void onReconnect(void (*cb)());
void onMessage(void (*cb)(const uint8_t *data, size_t len));
```

*   **`onConnect`**: Triggered when the initial connection and handshake succeed.
*   **`onDisconnect`**: Triggered when the connection drops.
*   **`onReconnect`**: Triggered when a reconnection handshake completes.
*   **`onMessage`**: Triggered when a new decrypted message arrives.

> [!IMPORTANT]
> **Do Not Mix Callbacks and Polling**
> If you register an `onMessage` callback, **do not** call `usmp.available()` or `usmp.read()`. Doing so will interfere with data flow and cause dropped frames.

---

## Transport Configuration Builder

Create your transport adapter configuration using the `USMP::` namespace:

### TCP Transport
```cpp
USMP::TCP(const char *gateway_ip, uint16_t port = 9000);
```
*   Configures a TCP socket connection.
*   To let USMP manage your Wi-Fi, chain `.wifi("SSID", "Password")`.

### UDP Transport
```cpp
USMP::UDP(const char *gateway_ip, uint16_t port = 9000);
```
*   Configures a connectionless UDP socket connection.
*   To let USMP manage your Wi-Fi, chain `.wifi("SSID", "Password")`.

