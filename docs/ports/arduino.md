# Arduino Port

The Arduino port provides an ergonomic C++ wrapper `USMPClient` around the USMP Core API, tailored for ESP32 boards running the Arduino framework.

---

## Library Structure

```
ports/usmp-arduino/
  src/
    USMP.h                ← USMPClient class declaration
    USMP.cpp              ← USMPClient implementation
    USMPTransport.h       ← USMPTCPTransport helper class
    USMPTransport.cpp     ← TCP transport bridge
    usmp_api.h            ← C API bridge header
    usmp_port_arduino.cpp ← Arduino platform hooks (RNG, uptime)
  library.properties      ← Arduino library metadata
  library.json            ← PlatformIO library metadata
  examples/
    basic/                ← Blocking/polling read example
    callbacks/            ← Asynchronous callback example
```

---

## Client API Reference

### Constructor

```cpp
USMPClient(const char *psk);
```
Creates a new USMP client instance.
- **`psk`**: The pre-shared key (must match the Python gateway's PSK).

### Connection Management

#### `begin`
```cpp
bool begin(USMPTCPTransport transport);
```
Establishes the connection. If WiFi is configured on the transport, it will block until WiFi is connected before performing the USMP handshake.
- Returns `true` if the handshake is successful, `false` otherwise.

#### `maintain`
```cpp
void maintain();
```
Performs background maintenance. **Must be called in the main `loop()`**.
- Drives non-blocking packet reception and triggers registered callbacks.
- Automatically handles keepalive `PING` and `PONG` frames.
- Re-establishes the connection if dropped, using an exponential backoff (starting at 2s, capping at 30s).

#### `keepalive`
```cpp
void keepalive(uint32_t ms);
```
Sets the keepalive ping interval (default is `30000` ms / 30 seconds).

#### `alive`
```cpp
bool alive();
```
Returns `true` if the session is currently established and active.

#### `reconnect`
```cpp
bool reconnect();
```
Manually triggers an immediate reconnection attempt.

#### `close`
```cpp
void close();
```
Gracefully sends a `BYE` frame to the gateway and closes the connection.

### Send and Receive

#### `send`
```cpp
bool send(const char *str);
bool send(const String &str);
bool send(const uint8_t *data, size_t len);
```
Encrypts and transmits a payload to the gateway.
- Returns `true` on success.
- Returns `false` on failure (marks the connection as disconnected and schedules a reconnect).

#### `available`
```cpp
bool available();
```
Checks if any decrypted payload bytes are waiting to be read.

#### `read`
```cpp
String read();
int read(uint8_t *buf, size_t max_len);
```
Reads the incoming payload.
- `read()` returns the data as an Arduino `String`.
- `read(buf, max_len)` copies raw bytes to the buffer and returns the actual length read (or `-1` on error).

### Callbacks

Register callbacks to handle events asynchronously. These are fired inside `maintain()`.

```cpp
void onConnect(void (*cb)());
void onDisconnect(void (*cb)());
void onReconnect(void (*cb)());
void onMessage(void (*cb)(const uint8_t *data, size_t len));
```

- **`onConnect`**: Fired when a new handshake completes successfully.
- **`onDisconnect`**: Fired when the connection is lost.
- **`onReconnect`**: Fired when a reconnection handshake completes successfully.
- **`onMessage`**: Fired when a data payload is received. Passes raw bytes and length.

---

## Transport Helper API

The `USMP::TCP` namespace provides a fluent helper API for configuring the network connection.

### Constructor
```cpp
USMP::TCP(const char *host, uint16_t port = 9000);
```
Configures the TCP server address.
- **`host`**: The target IP address or hostname of the Python gateway.
- **`port`**: The port number (defaults to `9000`).

### WiFi Configuration
```cpp
USMPTCPTransport &wifi(const char *ssid, const char *password);
```
*(Optional)* Configures USMP to automatically manage the WiFi connection before connecting to TCP. If omitted, you must connect to WiFi manually in your sketch (e.g. `WiFi.begin(...)`) before calling `usmp.begin()`.

```cpp
// Example: Self-managed WiFi
usmp.begin(USMP::TCP("192.168.1.100"));

// Example: USMP-managed WiFi
usmp.begin(USMP::TCP("192.168.1.100", 9000).wifi("MySSID", "MyPass"));
```

---

## Properties

#### `deviceId`
```cpp
String deviceId();
```
Returns the device MAC address as a formatted string (e.g. `ab:cd:ef:01:02:03`).

#### `sessionId`
```cpp
String sessionId();
```
Returns the active session ID as an 8-character hex string (e.g. `a3f1b2c4`).
