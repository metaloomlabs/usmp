# USMP — Unified Secure Multi-transport Protocol

> Secure, lightweight, transport-agnostic communication for embedded devices.

USMP is a binary application-layer protocol that gives your ESP32 (or any embedded device) a **secure, authenticated, encrypted session** with a gateway — with three function calls.

```c
usmp_transport_tcp_init(&transport, "192.168.137.1", 9000);
usmp_connect(&ctx, &transport);
usmp_send(&ctx, data, len);
```

---

## Why USMP?

Most IoT protocols make you choose between **simple** and **secure**:

| Protocol | Simple | Secure | Embedded-friendly |
|---|---|---|---|
| Raw TCP | Yes | No | Yes |
| MQTT | Yes | Needs TLS | Partial |
| TLS | No | Yes | Heavy |
| CoAP | Yes | Needs DTLS | Yes |
| **USMP** | **Yes** | **Yes** | **Yes** |

USMP is **secure by default**. There is no insecure mode. Every session is:

- **Mutually authenticated** — both device and gateway verify each other
- **Encrypted** — AES-256-GCM, mandatory
- **Forward secret** — X25519 ephemeral keys, new per session
- **Replay protected** — nonces + monotonic sequence numbers

---

## How it works

```txt
ESP32                          Gateway
  │                                │
  │──── HELLO ────────────────────▶│  "I'm device AA:BB:CC:DD:EE:FF"
  │◀─── CHALLENGE ─────────────────│  "Prove it. Here's a nonce."
  │──── HELLO_ACK ────────────────▶│  HMAC proof (PSK)
  │◀─── SESSION_OK ────────────────│  HMAC proof (PSK) + session ID
  │                                │
  │════ AES-256-GCM frames ════════│  encrypted, sequenced, authenticated
```

The handshake takes **~200ms** on ESP32. After that, sending a frame takes **<5ms**.

---

## Features

- **Mutual authentication** — PSK-based HMAC, both sides verified
- **Forward secrecy** — X25519 ephemeral key exchange per session
- **AES-256-GCM encryption** — mandatory, authenticated
- **Replay protection** — per-session nonces + sequence numbers
- **Transport agnostic** — TCP now, UART and UDP coming
- **Simple API** — connect, send, recv, close
- **Cross-platform** — ESP32, Arduino (ESP32 cores), STM32 coming
- **Python SDK** — asyncio server and client

---

## Status

**Active Development — v0.2.0**

| Component | Status |
|---|---|
| Protocol spec | Complete |
| Frame layer | Working |
| Handshake | Working |
| AES-256-GCM encryption | Working |
| Mutual authentication | Working |
| ESP32 port | Working |
| Arduino port | Working |
| Python SDK | Working |
| Transport abstraction | Working |
| UART transport | In Progress |
| CLI tool | In Progress |
| mDNS discovery | In Progress |
| Cloud bridge | Planned |

---

## Quick links

- [Quick Start (ESP32)](getting-started/quickstart-esp32.md)
- [Quick Start (Arduino)](getting-started/quickstart-arduino.md)
- [Quick Start (Python)](getting-started/quickstart-python.md)
- [Protocol Specification](spec.md)
- [Security Model](security/model.md)
- [GitHub](https://github.com/winterx64/usmp)
