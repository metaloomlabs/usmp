# DXP — Device Exchange Protocol

> Secure, lightweight, transport-agnostic communication for embedded devices.

DXP is a binary application-layer protocol that gives your ESP32 (or any embedded device) a **secure, authenticated, encrypted session** with a gateway — with three function calls.

```c
dxp_transport_tcp_init(&transport, "192.168.137.1", 9000);
dxp_connect(&ctx, &transport);
dxp_send(&ctx, data, len);
```

## Why DXP?

Most IoT protocols make you choose between **simple** and **secure**:

| Protocol | Simple | Secure    | Embedded-friendly |
|----------|--------|--------   |-------------------|
| Raw TCP  | ✓      | ✗         | ✓                 |
| MQTT     | ✓      | Needs TLS | Partial           |
| TLS      | ✗      | ✓         | Heavy             |
| CoAP     | ✓      | Needs DTLS| ✓                 |
| **DXP**  | **✓**  | **✓**     | **✓**             |

DXP is **secure by default**. There is no insecure mode. Every session is:

- **Mutually authenticated** — both device and gateway verify each other
- **Encrypted** — AES-256-GCM, mandatory
- **Forward secret** — X25519 ephemeral keys, new per session
- **Replay protected** — nonces + monotonic sequence numbers

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

## Features

- 🔐 **Mutual authentication** — PSK-based HMAC, both sides verified
- 🔑 **Forward secrecy** — X25519 ephemeral key exchange per session
- 🔒 **AES-256-GCM encryption** — mandatory, authenticated
- 🔄 **Replay protection** — per-session nonces + sequence numbers
- 🔌 **Transport agnostic** — TCP now, UART and UDP coming
- 📦 **Simple API** — connect, send, recv, close
- 🌐 **Cross-platform** — ESP32 today, STM32 and Arduino coming
- 🐍 **Python SDK** — asyncio server and client

---

## Status

🚧 **Active Development — v0.1.0**

| Component | Status |
|-----------|--------|
| Protocol spec | ✅ Complete |
| Frame layer | ✅ Working |
| Handshake | ✅ Working |
| AES-256-GCM encryption | ✅ Working |
| Mutual authentication | ✅ Working |
| ESP32 port | ✅ Working |
| Python SDK | ✅ Working |
| Transport abstraction | ✅ Working |
| UART transport | 🚧 Coming |
| CLI tool | 🚧 Coming |
| mDNS discovery | 🚧 Coming |
| Cloud bridge | 📋 Planned |

---

## Quick links

- [Quick Start (ESP32)](getting-started/quickstart-esp32.md)
- [Quick Start (Python)](getting-started/quickstart-python.md)
- [Protocol Specification](spec.md)
- [Security Model](security/model.md)
- [GitHub](https://github.com/winterx64/dxp)
