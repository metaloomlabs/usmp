# What is USMP?

USMP (Unified Secure Multi-transport Protocol) is a lightweight binary protocol for **secure, authenticated communication between embedded devices and a gateway**.

It is designed for the gap that exists in the IoT ecosystem today:

!!! quote "The IoT security gap"
    Most embedded devices either use **no security at all** (raw TCP, plain MQTT),
    or bolt on **TLS** — which is complex, heavy, and hard to configure correctly
    on constrained hardware.

USMP sits in the middle: **genuinely secure, genuinely simple**.

## What USMP is

- A **session protocol** — it establishes a secure, authenticated session between two endpoints
- A **transport-agnostic** protocol — it runs on TCP, UART, UDP, or anything that moves bytes
- A **binary protocol** — compact, efficient, no JSON or XML overhead
- A **protocol with mandatory security** — there is no plaintext mode

## What USMP is not

- A replacement for MQTT at cloud scale — USMP handles the **device → gateway** leg
- A data format — USMP doesn't care what's inside your frames (that's your application)
- A radio protocol — USMP runs on top of WiFi, UART, BLE, whatever you have
- A cloud service — USMP is infrastructure-free, runs entirely on your hardware

## Who is USMP for?

### Makers and hobbyists

You want your ESP32 to talk to your laptop securely without a cloud dependency or a complex TLS setup. USMP gives you that in three function calls.

### Embedded engineers

You're building a product with 10–10,000 devices. You need mutual authentication, forward secrecy, and replay protection — but you don't want to manage a PKI or pay for a cloud broker. USMP gives you production-grade security on $5 hardware.

### Researchers and students

You want to understand how a secure IoT protocol works from the inside. USMP is small enough to read in a day, with every security decision documented and explained.

## The three-function API

```c
// 1. Connect
usmp_transport_t transport = {0};
usmp_transport_tcp_init(&transport, "192.168.137.1", 9000);

usmp_t ctx = {0};
usmp_connect(&ctx, &transport);

// 2. Send
usmp_send(&ctx, (uint8_t *)"hello", 5);

// 3. Receive
uint8_t buf[256];
int len = usmp_recv(&ctx, buf, sizeof(buf));
```

That's the entire API surface for 90% of use cases.

## Security at a glance

| Property | How USMP achieves it |
|----------|---------------------|
| Device authentication | HMAC-SHA256 with PSK |
| Gateway authentication | HMAC-SHA256 with PSK (mutual) |
| Confidentiality | AES-256-GCM |
| Integrity | AES-256-GCM authentication tag |
| Forward secrecy | X25519 ephemeral key exchange |
| Replay protection | Per-session nonce + sequence numbers |
| Key derivation | HKDF-SHA256 |

## Next steps

- [Quick Start (ESP32)](quickstart-esp32.md) — get running in 10 minutes
- [Quick Start (Python)](quickstart-python.md) — set up the gateway
- [Protocol Overview](../protocol/overview.md) — understand what's happening under the hood
