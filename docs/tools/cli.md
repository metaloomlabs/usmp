# CLI Tool

!!! warning "Coming soon"
    The DXP CLI tool is under active development.

The `dxp` CLI tool lets you interact with DXP devices on your LAN
directly from the terminal — like SSH for IoT devices.

## Planned commands

```bash
# Scan LAN for DXP devices
dxp scan

# Connect to a device (interactive shell)
dxp connect aa:bb:cc:dd:ee:ff

# Stream logs from a device
dxp logs aa:bb:cc:dd:ee:ff

# Send a one-shot command
dxp send aa:bb:cc:dd:ee:ff "reboot"

# Monitor all devices
dxp monitor
```

## Planned output

```
$ dxp scan
Scanning LAN for DXP devices...
  aa:bb:cc:dd:ee:ff  192.168.1.60  ESP32  DXP v0.1  online
  aa:bb:cc:dd:ee:00  192.168.1.61  ESP32  DXP v0.1  online

$ dxp connect aa:bb:cc:dd:ee:ff
Connecting to aa:bb:cc:dd:ee:ff (192.168.1.60)...
[DXP] Handshake complete — session a3f1b2c4
> get_sensor
temperature: 23.4C
> exit
```

## Installation (when available)

```bash
pip install dxp-cli
```

```

---

**`docs/security/psk.md`:**
```markdown
# PSK Management

The Pre-Shared Key (PSK) is the root secret in DXP.
Both the device and gateway must have the same PSK.

## Generating a PSK

```bash
python3 -c "import secrets; print(secrets.token_hex(32))"
# e.g: 7f3a1b9e2c4d5f6a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a
```

Use at least 32 bytes (64 hex chars) of random data.

## Storing the PSK

=== "ESP32 (development)"
    ```c
    // For development only — never commit to source control
    #define DXP_PSK "your-generated-psk-here"
    #include "dxp.h"
    ```

=== "ESP32 (production)"
    Store in NVS (Non-Volatile Storage) with flash encryption enabled:
    ```c
    // Read PSK from NVS at runtime
    nvs_handle_t handle;
    nvs_open("dxp", NVS_READONLY, &handle);
    size_t len = 64;
    char psk[64];
    nvs_get_str(handle, "psk", psk, &len);
    nvs_close(handle);
    ```

=== "Python"
    ```python
    import os

    # From environment variable
    PSK = os.environ["DXP_PSK"].encode()

    # From file
    PSK = open("/etc/dxp/psk", "rb").read().strip()
    ```

## Multi-device PSK

The current version uses a single PSK for all devices.
For larger deployments, derive per-device PSKs from a master secret:

```python
import hmac, hashlib

MASTER_SECRET = os.environ["DXP_MASTER_SECRET"].encode()

def get_psk(device_id: bytes) -> bytes:
    return hmac.new(MASTER_SECRET, device_id, hashlib.sha256).digest()
```

!!! note
    Native per-device PSK support is planned for a future release.

## Security rules

!!! danger
    - Never use the default PSK in production
    - Never commit PSKs to source control
    - Never log or print PSKs
    - Rotate PSKs if a device is compromised
    - Use flash encryption on ESP32 to protect stored PSKs
