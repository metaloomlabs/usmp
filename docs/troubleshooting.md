# USMP Troubleshooting Guide

This document lists common issues encountered when deploying, testing, or developing with the Unified Secure Multi-transport Protocol (USMP), along with their typical causes and resolutions.

---

## 1. Handshake Failures

### 1.1 `Client HMAC verification failed` (Server Side) or `Server HMAC verification failed` (Client Side)
* **Symptom**: The handshake fails at Step 3 or Step 4 with an authentication or HMAC error.
* **Cause**: 
  - The client and server are not using the exact same Pre-Shared Key (PSK).
  - The PSK loaded at runtime on the device does not match the PSK configured in the server's device registry.
* **Resolution**:
  - Verify that the PSK is correct and identical on both sides.
  - If using dynamic PSK resolution (`Callable` or dictionary registry on the server), check that the device ID parsed from the HELLO packet exactly matches the registration key.

### 1.2 `Device ID not registered` (Server Side)
* **Symptom**: The server rejects the handshake after receiving the client's HELLO frame.
* **Cause**: The server is using a dictionary of PSKs, and the client's device ID is not a key in that dictionary.
* **Resolution**:
  - Register the device's ID in the server configuration.
  - If device IDs are dynamic (e.g., random or MAC-address-based), ensure the server resolves the correct PSK for the incoming device ID.

---

## 2. Encryption and Connection Errors

### 2.1 `Decryption failed` or `CryptoError`
* **Symptom**: Connection is established, but immediately raises a decryption failure error on the first payload frame.
* **Cause**:
  - Mismatched session keys between client and server (usually caused by a handshake sequence mismatch or memory corruption).
  - The frame was modified or corrupted in transit, causing the AES-GCM authentication tag check to fail.
  - Nonce generation issues.
* **Resolution**:
  - Capture network packets to verify frame integrity.
  - Re-establish the session (re-run handshake) to derive fresh session keys.

### 2.2 `Sequence mismatch`
* **Symptom**: The receiver drops data packets with a sequence number error.
* **Cause**:
  - Out of order delivery or packet loss.
  - A potential replay attack.
  - A bug in sequence counter management on one of the endpoints.
* **Resolution**:
  - Ensure the transport channel preserves packet order (e.g., TCP).
  - If a packet was lost, trigger a session reconnect to reset sequence counters.

---

## 3. Embedded & Hardware Limitations

### 3.1 Stack Overflow / Crashes during Handshake (Arduino / ESP32)
* **Symptom**: The microcontroller resets or halts during the `usmp_connect()` call.
* **Cause**:
  - The large cryptographic buffers or mbedtls contexts exceeded the available stack space.
  - Pre-existing heap fragmentation preventing allocation of handshake packets.
* **Resolution**:
  - Ensure the task stack size allocated to the USMP task is at least 4-8 KB on ESP32.
  - Verify that the compiler options do not inline excessively large stack structures.
  - Use our heap-allocated version of handshake buffers to relieve stack pressure.

---

## 4. Keepalive & Inactivity Timeout Issues

### 4.1 Frequent Disconnects after PING/PONG (Arduino / Polling Wrapper)
* **Symptom**: The client successfully connects, sends/receives data, but immediately disconnects and reconnects 5 seconds after sending a keepalive `PING`. 
* **Cause**: 
  - The core C function `usmp_recv` loops internally after reading control frames (like `PONG` or `PING`) to look for a `DATA` frame.
  - In polling or non-blocking environments (like the Arduino port), if the transport buffer becomes empty, the transport's read operation blocks waiting for the next packet.
  - If a timeout is enforced (e.g. the default 5-second timeout in `arduino_tcp_recv`), the read times out, which `usmp_recv` treats as a fatal connection drop.
* **Resolution**:
  - Update the library to the latest version where `usmp_recv` checks the transport's `available` hook and returns `0` early if the buffer is empty.
  - Ensure that polling wrappers (like `USMPClient::read()`) are updated to handle `0` return values as normal non-data events instead of connection failures.

### 4.2 Client Keepalive Overridden or Wiped Out
* **Symptom**: You set `usmp.keepalive(15000)` on the client, but it still waits for 30 seconds before sending a PING.
* **Cause**: The `usmp.keepalive(ms)` function was called **before** `usmp.begin()`. Inside `begin()`, the client context structure `_ctx` is fully zeroed out via `memset`, resetting the keepalive timeout to the default 30 seconds.
* **Resolution**:
  - Always call `usmp.keepalive(ms)` **after** `usmp.begin()`.

### 4.3 Optimizing Keepalive Traffic (Reducing Redundant PINGs)
* **Symptom**: The client and server are constantly streaming data, but the client still sends PING packets every 15/30 seconds, causing redundant traffic.
* **Cause**: The client's keepalive timer (`usmp_keepalive_tick`) tracks **transmit inactivity** (time since the last outgoing packet was sent by the client). Even if the client is receiving data constantly, if it doesn't send anything, its transmit timer will expire.
* **Resolution**:
  - Implement **Application-Level Control (ACK)**. Modify the client's message received callback (`onMessage`) to send a small response (e.g. `usmp.send("ACK")`) back to the server.
  - Since the client is now actively transmitting, this resets the client's transmit inactivity timer (`last_tx_ms`), completely suppressing keepalive PINGs during active data streams.
  - If the server stops sending, the client stops ACKing, and after the keepalive interval (e.g. 15s) of complete silence, the client will automatically resume sending PINGs to verify the link.

