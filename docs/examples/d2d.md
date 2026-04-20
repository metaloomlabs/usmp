# Device to Device (D2D)

!!! warning "Coming soon"
    D2D support requires `dxp_listen()` on the ESP32, which is currently
    under development.

D2D allows two ESP32 devices to establish a secure DXP session directly,
without routing through a gateway.

## Planned API

```c
// Device A — acts as server
dxp_t ctx = {0};
dxp_listen(&ctx, DXP_TRANSPORT_TCP, 9000);  // blocks until connected

// Device B — acts as client
dxp_transport_t transport = {0};
dxp_transport_tcp_init(&transport, "192.168.1.x", 9000);
dxp_connect(&ctx, &transport);
```

## Discovery

Devices will use mDNS to discover each other on the LAN:

```bash
# Device A announces itself
_dxp._tcp.local  port 9000

# Device B queries
dxp scan  # finds Device A
```
