# USMP | Unified Secure Multi-transport Protocol

> **Say goodbye to the "IoT Security Gap"!** USMP is a lightweight, secure, and developer-friendly session protocol designed to bring end-to-end encrypted, mutually authenticated tunnels to ESP32, Arduino, and Python.

[![Python SDK](https://img.shields.io/pypi/v/usmp?label=usmp&color=blue)](https://pypi.org/project/usmp)
[![ESP Component Registry](https://components.espressif.com/components/metaloomlabs/usmp/badge.svg)](https://components.espressif.com/components/metaloomlabs/usmp)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.0%2B-blue)](#esp32-esp-idf)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32-teal)](#arduino)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](#license)

---

## Why USMP?

In the world of IoT, developers are often forced to make a frustrating choice when connecting devices:

1. **Raw TCP / UDP**: Fast and lightweight, but completely open to eavesdropping, spoofing, and tampering.
2. **Full TLS / DTLS**: Rock-solid, but extremely heavy, slow to handshake, and resource-prohibitive for smaller microcontrollers.

**USMP fills this gap.** It gives you a lightweight, transport-agnostic, and secure tunnel that runs anywhere. Establishing a secure connection on your device is as simple as:

```c
// 1. Initialize your choice of transport (TCP or UDP, supports IPs & DNS hostnames)
usmp_transport_udp_init(&transport, "usmp.mycompany.com", 9000);

// 2. Perform the handshake and establish a secure session
usmp_connect(&ctx, &transport);

// 3. Send encrypted data safely!
usmp_send(&ctx, data, len);
```

### Cryptographic Guarantees (No "Insecure Mode")

Every single session is hardened:

* **Mutual Authentication**: Both sides verify identity using HMAC-SHA256 and a Pre-Shared Key (PSK) before exchanging payloads.
* **Perfect Forward Secrecy**: An ephemeral X25519 key exchange occurs with every session, protecting past traffic even if keys are compromised later.
* **Mandatory Encryption**: All payload data is encrypted using AES-256-GCM or ChaCha20-Poly1305.
* **In-Band Session Rekeying**: Transparent key rotation during active sessions via `PKT_REKEY`.
* **Replay Protection**: Strict, monotonic 32-bit sequence numbers are verified for every frame.
* **Deterministic Nonces**: Under the hood, AEAD nonces are constructed as `seq (4 bytes, Little-Endian) || session_id[0..7]` to eliminate nonce collision risks.

---

## Key Features

* **Multi-Transport Support**: Production-ready support for **TCP** and **UDP** (with transport-level reliability overlays), with UART and BLE coming soon!
* **Platform Agnostic Core**: A pure C core with only 5 platform hooks to implement for any new platform.
* **Dynamic Payload Fragmentation**: Automatically fragments payloads up to ~1.8 KB into multiple frames (plaintext capacity of 452 bytes per frame) and transparently reassembles them at the receiver.
* **Built-in Rate Limiting**: The Python server locks out offending IPs for 60 seconds after consecutive handshake failures to prevent brute-force attacks.
* **Registry-Based Distribution**:
  * **Python**: Fully async SDK available on **PyPI** (`pip install usmp`).
  * **ESP-IDF**: Native component on the **ESP Component Registry** (`metaloomlabs/usmp`).
  * **Arduino**: Standard packaged offline ZIP library (`usmp-1.2.0-arduino.zip`).

---

## Supported Platforms

| Platform | Status | Getting Started |
| :--- | :--- | :--- |
| **ESP32 (ESP-IDF v5+)** | Production ready | [ESP-IDF Port Guide](ports/usmp-esp32/README.md) |
| **ESP32 (Arduino)** | Production ready | [Arduino Reference](ports/usmp-arduino/) |
| **Python 3.11+** | Production ready | [Python SDK Reference](sdk/python/README.md) |
| **STM32** | Planned | - |
| **Linux** | Planned | - |

---

## Quick Start (TCP or UDP)

### 1. Python Gateway Server

Run `pip install usmp` and launch this async gateway server:

```python
import asyncio
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"

# Initialize TCP or UDP server
server = USMPServer(host="0.0.0.0", port=9000, psk=PSK, protocol=USMPProtocol.TCP)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"Device connected: {session.device_id}")
    try:
        while True:
            # Receive decrypted data
            data = await session.recv()
            print(f"Received: {data.decode('utf-8')}")
            
            # Send an encrypted reply
            await session.send(b"Got your message loud and clear!")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

async def main():
    print("Starting USMP Server on port 9000...")
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

### 2. ESP32 - ESP-IDF Client (C)

Import `metaloomlabs/usmp` in your project component dependencies and write:

```c
#include "usmp.h"
#include "usmp_transport.h"

void app_main(void) {
    wifi_init(); // Configure your standard Wi-Fi

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};
    
    static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
    ctx.psk = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1;

    // Use usmp_transport_tcp_init or usmp_transport_udp_init
    if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) == 0) {
        if (usmp_connect(&ctx, &transport) == 0) {
            printf("Secure session established!\n");
        }
    }

    ctx.keepalive_ms = 15000;
    usmp_send(&ctx, (const uint8_t *)"Hello, USMP!", 12);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Maintain the watchdog & auto-reconnect on drops
        if (usmp_keepalive_tick(&ctx) < 0) {
            printf("Connection lost, reconnecting...\n");
            usmp_reconnect(&ctx);
        }
    }
}
```

### 3. ESP32 - Arduino Client (C++)

Add the offline ZIP library and upload:

```cpp
#include <USMP.h>

#define PSK       "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP "192.168.1.100"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

void setup() {
    Serial.begin(115200);

    // Swap to USMP::UDP(SERVER_IP) to run over UDP!
    if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("Connection failed!");
        return;
    }

    usmp.keepalive(15000); // Configure keepalive interval
    usmp.send("Hello from Arduino ESP32!");
}

void loop() {
    // Maintains keepalives & reconnects automatically in background
    usmp.maintain();

    if (usmp.available()) {
        String msg = usmp.read();
        Serial.println("Received: " + msg);
    }
}
```

---

## Detailed Documentation

For examples and component guides:

* [Python SDK Guide](sdk/python/README.md)
* [ESP32 Component Guide](ports/usmp-esp32/README.md)
* [Example Projects](examples/README.md)

---

## ⚠️ Security Notice: PSK Limitation

> **USMP uses Pre-Shared Key (PSK) authentication, which is vulnerable to offline brute-force attacks on captured handshake transcripts.**

Unlike PAKE protocols (SPAKE2, CPace), an attacker who records a USMP handshake can attempt to crack the PSK offline. The 16-byte minimum length is enforced, but **length ≠ entropy**.

**You must:**

* Use cryptographically random PSKs (`os.urandom(32)` or hardware RNG) — not human-readable passphrases.
* Store PSKs in secure storage (encrypted NVS, secure elements, HSMs).
* Never hardcode PSKs in source code or firmware.

See [SECURITY.md](SECURITY.md) for the full disclosure and mitigation guidance. A PAKE upgrade is planned for a future release.

---

## Roadmap

* [x] **v0.2.0**: Core protocol, Keepalive mechanism, and Arduino Port.
* [x] **v0.3.0**: TCP transport support and initial Python SDK.
* [x] **v0.4.0**: Published on ESP Component Registry and PyPI, making it stable.
* [x] **v0.4.7**: Security hardening (Deterministic Nonces, Lockout Rate Limiting, Dynamic Fragmentation).
* [x] **v0.5.0**: UDP transport support fully complete and production-ready.
* [x] **v1.1.0**: Hardening & security fixes, CLI tools reference.
* [x] **v1.2.0**: ChaCha20-Poly1305 cipher suite, In-Band Session Rekeying (`PKT_REKEY`), Adaptive UDP RTT estimation, and Arduino control frame decoupling.

---

## Contributing & Governance

We welcome all contributions! Please review our [Contributing Guidelines](CONTRIBUTING.md) and adhere to our [Code of Conduct](CODE_OF_CONDUCT.md) before submitting a Pull Request.

For security concerns, please refer to our [Security Policy](SECURITY.md).

## License

USMP is open-source software licensed under the [Apache 2.0 License](LICENSE).

<p align="center">
  <strong>USMP™</strong> • Developed by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong>
</p>
