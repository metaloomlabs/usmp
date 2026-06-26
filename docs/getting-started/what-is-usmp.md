# Welcome to USMP: Unified Secure Multi-transport Protocol

Secure communication for IoT devices shouldn't be hard. Yet today, developers are forced to choose between two extremes:
* **Raw TCP (Simple but completely insecure)**: Your packets are sent in plain text, open to eavesdropping and manipulation.
* **Full TLS (Secure but incredibly heavy)**: Demands massive CPU cycles, eats up heap memory, requires managing complex public-key infrastructure (PKI) certificates, and is generally a headache to debug on constrained microcontrollers.

**USMP fills this gap.** It gives your hardware production-grade security (mutual authentication, forward secrecy, and AES-256-GCM encryption) with the ease of three simple function calls.

## What USMP Is (and What It Isn't)

To understand how USMP fits into your project, it's helpful to see what it is built for:

### What USMP Is
* **A Session Protocol**: It creates a secure, authenticated bridge (a session) between two endpoints.
* **Transport Agnostic**: It does not care how bytes move. It runs beautifully over TCP sockets, serial UART wires, UDP, or BLE.
* **Lightweight & Binary**: Designed from the ground up for microcontrollers. There is no JSON parser, XML parsing, or heavy certificate checking—just tight, efficient binary frames.
* **Secure by Default**: There is no "insecure mode." Every single byte sent post-handshake is encrypted and verified.

### What USMP Is Not
* **An Application Protocol**: USMP is not a replacement for MQTT or HTTP. It handles the **device-to-gateway** security layer. Your app payloads (like JSON sensor readings or binary control logs) sit *inside* the secure USMP envelope.
* **A Wireless Protocol**: It runs on top of whatever network layer you already have configured (WiFi, cellular, serial).
* **A Cloud Broker**: It does not require any cloud infrastructure. It runs entirely on your own local gateway and end-device hardware.

## The Three-Function Developer API

Integrating USMP into your C application takes only a few lines of code:

```c
// 1. Set up the transport and connect
usmp_transport_t transport = {0};
usmp_transport_tcp_init(&transport, "192.168.1.100", 9000);

usmp_t ctx = {0};
static const uint8_t psk[] = "your-secret-key";
ctx.psk     = psk;
ctx.psk_len = sizeof(psk);

if (usmp_connect(&ctx, &transport) == 0) {
    // 2. Send secure, encrypted data
    usmp_send(&ctx, (const uint8_t *)"Hello Gateway", 13);
    
    // 3. Receive secure, decrypted data
    uint8_t buffer[256];
    int len = usmp_recv(&ctx, buffer, sizeof(buffer));
}
```

That is the entire API surface you need for 90% of your usage.

## Security Checklist: How USMP Protects You

Here is a quick look at the cryptographic armor USMP wraps around your device's traffic:

| Security Goal | How USMP Achieves It |
|---|---|
| **Identity Verification** | **HMAC-SHA256 with a Pre-Shared Key (PSK)**. Both the device and the gateway prove to each other that they know the secret key without sending the key over the air. |
| **Confidentiality (Secrecy)** | **AES-256-GCM**. All session data is fully encrypted. Anyone sniffing the network sees only random-looking noise. |
| **Data Integrity** | **AES-GCM Authenticated Tag**. If an attacker tampers with even a single bit of a frame in transit, decryption fails instantly and the session is dropped. |
| **Forward Secrecy** | **X25519 Ephemeral Key Exchange**. Ephemeral keys are generated fresh for every session and thrown away. If your PSK leaks in the future, past session traffic remains completely secure. |
| **Replay Protection** | **Deterministic Nonces + Monotonic Sequence Numbers**. Prevents attackers from capturing valid packets and re-sending them later to mimic commands. |
| **Key Derivation** | **HKDF-SHA256**. Generates cryptographic-grade session keys from the key exchange secrets. |

## Where to Go Next?

Ready to build? Dive into the quickstarts:
* [Quick Start (ESP-IDF)](quickstart-esp32.md) — Get running on ESP32 native C.
* [Quick Start (Arduino)](quickstart-arduino.md) — Build using the C++ Arduino client.
* [Quick Start (Python)](quickstart-python.md) — Launch your Python gateway server.
* [Protocol Specifications](../protocol/overview.md) — Peek under the hood.
