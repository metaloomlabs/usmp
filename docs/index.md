# USMP: Unified Secure Multi-transport Protocol

> **Bridging the IoT Security Gap with zero-friction, end-to-end encrypted tunnels.**

USMP is a lightweight, transport-agnostic binary session protocol designed for resource-constrained embedded microcontrollers (ESP32, Arduino) and gateways (Python). It provides iron-clad mutual authentication and AES-256-GCM encryption with just three function calls.

```c
// 1. Initialize your choice of transport (TCP or UDP)
usmp_transport_udp_init(&transport, "192.168.1.100", 9000);

// 2. Perform mutual handshake and establish keys
usmp_connect(&ctx, &transport);

// 3. Send securely encrypted payloads
usmp_send(&ctx, data, len);
```

---

## Why USMP?

Historically, connecting embedded microcontrollers securely meant choosing between insecure raw sockets or heavy, resource-exhausting TLS/DTLS stacks. **USMP fills this gap** by offering a lightweight alternative that implements strict security guarantees without the footprint of full PKI.

| Protocol / Standard | Light on RAM/Flash | Forward Secrecy | Mutual Authentication | Transport Agnostic |
| :--- | :---: | :---: | :---: | :---: |
| **Raw TCP / UDP** | 🟢 Yes | 🔴 No | 🔴 No | 🔴 No |
| **Full TLS / DTLS** | 🔴 No | 🟢 Yes | 🟡 Optional | 🔴 No |
| **USMP** | 🟢 **Yes** | 🟢 **Yes** | 🟢 **Yes** | 🟢 **Yes** |

---

## Core Security Guarantees

USMP does not support an "insecure mode." Every session is strictly hardened out of the box:

* **Mutual Authentication**: Both client (device) and server (gateway) prove their identity using a Pre-Shared Key (PSK) and HMAC-SHA256 proofs before exchanging payload data.
* **Perfect Forward Secrecy**: An ephemeral X25519 Diffie-Hellman key exchange is performed for every session. Even if the Pre-Shared Key is compromised in the future, past captured traffic cannot be decrypted.
* **Mandatory Encryption**: All session data frames are encrypted using AES-256-GCM, ensuring absolute confidentiality and tamper-proof message integrity.
* **Replay Protection**: Strict, monotonic 32-bit sequence numbers and deterministic nonces prevent attackers from capturing and replaying packets.

!!! warning "Known Limitation: PSK Offline Cracking"

    USMP uses Pre-Shared Key (PSK) authentication, which **does not protect against offline
    brute-force attacks** on captured handshake transcripts (unlike PAKE protocols such as
    SPAKE2 or CPace).

    **You must use cryptographically random PSKs** (`os.urandom(32)` in Python, or a hardware
    RNG on embedded devices). Human-readable passphrases — even long ones — are vulnerable.
    Store PSKs in secure storage (encrypted NVS, secure elements, HSMs), never in source code.

    See [SECURITY.md](https://github.com/metaloomlabs/usmp/blob/main/SECURITY.md) for full details.
    A PAKE upgrade is planned for a future release.

---

## Supported Transports & Roadmap

USMP is designed to separate the cryptographic session state machine from the underlying transport medium.

* **TCP**: Production-ready. Best for reliable Wi-Fi or Ethernet streams.
* **UDP**: Production-ready. Optimized for constrained, lossy networks with built-in packet-level acknowledgment and reliability mechanisms.
* **Serial UART (with COBS & Sliding Window)**: 🟡 Coming soon.
* **BLE (Bluetooth Low Energy)**: 🟡 Coming soon.

---

## Direct Distribution Registries

USMP is packaged and published directly to official package managers, keeping your builds clean and independent of private source structures:

=== "Python SDK"
    Available on **PyPI** for gateways, servers, and backends.
    ```bash
    pip install usmp
    ```

=== "ESP32 Component"
    Available on the **ESP Component Registry** for ESP-IDF v5+.
    ```bash
    idf.py add-dependency "metaloomlabs/usmp"
    ```

=== "Arduino Library"
    Available as a packaged offline ZIP archive (`usmp-0.5.1-arduino.zip`) for import into Arduino IDE or PlatformIO.

    1. Go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
    2. Select the packaged ZIP archive.

---

## Navigation & Quick Start

Ready to dive in? Follow our step-by-step tutorials:

1. **[Installation & Setup](getting-started/installation.md)**: Prepare your environment and generate secure Pre-Shared Keys.
2. **[Your First TCP Tunnel](getting-started/tutorial-tcp.md)**: Establish a secure session over TCP.
3. **[Going Connectionless (UDP)](getting-started/tutorial-udp.md)**: Secure your communications over UDP.
4. **[Production Hardening](getting-started/production-hardening.md)**: Learn about credential management, NVS storage, keepalives, and automatic reconnection loops.
