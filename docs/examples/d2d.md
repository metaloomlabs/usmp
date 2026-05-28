# Device to Device (D2D)

!!! warning "Coming soon"
    D2D support requires `usmp_listen()` on the ESP32, which is currently
    under development.

D2D allows two ESP32 devices to establish a secure USMP session directly,
without routing through a gateway.

## Planned API

```c
// Device A — acts as server
usmp_t ctx = {0};
usmp_listen(&ctx, USMP_TRANSPORT_TCP, 9000);  // blocks until connected

// Device B — acts as client
usmp_transport_t transport = {0};
usmp_transport_tcp_init(&transport, "192.168.1.x", 9000);
usmp_connect(&ctx, &transport);
```

## Discovery

Devices will use mDNS to discover each other on the LAN:

```bash
# Device A announces itself
_usmp._tcp.local  port 9000

# Device B queries
usmp scan  # finds Device A
```
