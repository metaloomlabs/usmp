# USMP Debugging Guide

This guide provides developers with instructions and tips for debugging USMP integrations, analyzing packet flows, and tuning log levels.

---

## 1. Enabling Debug Logging

### 1.1 Python SDK
The Python SDK uses the standard Python `logging` library. You can configure it to show debug logs:

```python
import logging

# Set the root logger or specific usmp logger to DEBUG
logging.basicConfig(level=logging.DEBUG)
logging.getLogger("usmp").setLevel(logging.DEBUG)
```

This will output details about frame transitions, socket operations, and handshake steps.

### 1.2 C Core (ESP32 / Arduino / C App)
Define the `USMP_DEBUG` preprocessor macro during compilation. This enables the debug log macros like `USMP_LOGD`.

In your `usmp_port.h` implementation, ensure your `usmp_port_log` maps `'D'` (Debug) levels to your output terminal (e.g. `Serial.print` or `ESP_LOGD`).

Example CMake flag:
```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE -DUSMP_DEBUG)
```

---

## 2. Analyzing Frames on the Wire

Since USMP payloads are encrypted with AES-256-GCM, you cannot inspect the plaintext payload directly from Wireshark. However, you can analyze the unencrypted frame headers to verify packet sequencing and packet types:

### 2.1 USMP Frame Header Structure (12 bytes)
* **Magic** (2 bytes): `0xABCD` (Little Endian: `\xCD\xAB`)
* **Version** (1 byte): `0x01`
* **Type** (1 byte): PacketType (e.g. `0x01` = HELLO, `0x05` = DATA)
* **Sequence Number** (4 bytes): Sequence counter (Little Endian)
* **Payload Length** (2 bytes): Size of the encrypted payload (Little Endian)
* **CRC-16** (2 bytes): Header & payload checksum (Little Endian)

To debug framing issues:
1. Check that the first two bytes of every received chunk are `\xCD\xAB`.
2. Check that the sequence number increases by 1 for each successive DATA frame.
3. Validate that the length field matches the actual bytes read after the header.

---

## 3. Simulating Adverse Network Conditions

To test the resilience of USMP keepalive PINGs and connection timeouts:
* **Simulate Latency**: Introduce artificial lag on loopback interfaces using toolkits like `tc` (Linux Traffic Control) or clumsy (Windows).
* **Simulate Disconnections**: Force-close the TCP sockets during active transmission to verify that endpoints successfully clean up their session states and attempt reconnection with correct backoffs.
