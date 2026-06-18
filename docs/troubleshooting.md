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
