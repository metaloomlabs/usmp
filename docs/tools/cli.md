# CLI Tool

!!! warning "Coming soon"
    The USMP CLI tool is under active development.

The `usmp` CLI tool lets you interact with USMP devices on your LAN
directly from the terminal — like SSH for IoT devices.

## Planned commands

```bash
# Scan LAN for USMP devices
usmp scan

# Connect to a device (interactive shell)
usmp connect aa:bb:cc:dd:ee:ff

# Stream logs from a device
usmp logs aa:bb:cc:dd:ee:ff

# Send a one-shot command
usmp send aa:bb:cc:dd:ee:ff "reboot"

# Monitor all devices
usmp monitor
```

## Planned output

```
$ usmp scan
Scanning LAN for USMP devices...
  aa:bb:cc:dd:ee:ff  192.168.1.60  ESP32  USMP v0.1  online
  aa:bb:cc:dd:ee:00  192.168.1.61  ESP32  USMP v0.1  online

$ usmp connect aa:bb:cc:dd:ee:ff
Connecting to aa:bb:cc:dd:ee:ff (192.168.1.60)...
[USMP] Handshake complete — session a3f1b2c4
> get_sensor
temperature: 23.4C
> exit
```

## Installation (when available)

```bash
pip install usmp-cli
```

```

---

**`docs/security/psk.md`:**
```markdown
# PSK Management

The Pre-Shared Key (PSK) is the root secret in USMP.
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
    #define USMP_PSK "your-generated-psk-here"
    #include "usmp.h"
    ```

=== "ESP32 (production)"
    Store in NVS (Non-Volatile Storage) with flash encryption enabled:
    ```c
    // Read PSK from NVS at runtime
    nvs_handle_t handle;
    nvs_open("usmp", NVS_READONLY, &handle);
    size_t len = 64;
    char psk[64];
    nvs_get_str(handle, "psk", psk, &len);
    nvs_close(handle);
    ```

=== "Python"
    ```python
    import os

    # From environment variable
    PSK = os.environ["USMP_PSK"].encode()

    # From file
    PSK = open("/etc/usmp/psk", "rb").read().strip()
    ```

## Multi-device PSK

The current version uses a single PSK for all devices.
For larger deployments, derive per-device PSKs from a master secret:

```python
import hmac, hashlib

MASTER_SECRET = os.environ["USMP_MASTER_SECRET"].encode()

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
