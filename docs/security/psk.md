# PSK Management

The Pre-Shared Key (PSK) is the root secret in USMP. Both the device and gateway must have the same PSK.

## Generating a PSK

Use at least 32 bytes (64 hex chars) of random data. You can generate one via Python:

```bash
python3 -c "import secrets; print(secrets.token_hex(32))"
# Example output: 7f3a1b9e2c4d5f6a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a
```

## Storing the PSK

=== "ESP32 (ESP-IDF development)"
    ```c
    // For development only — never commit to source control.
    // Configure dynamically at runtime in the usmp_t context.
    usmp_t ctx = {0};
    static const uint8_t s_psk[] = "your-generated-psk-here";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1; // exclude null terminator
    ```

=== "ESP32 (ESP-IDF production)"
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

=== "Arduino (development)"
    ```cpp
    // For development only — never commit to source control.
    // Pass the PSK directly into the client constructor at runtime.
    USMPClient usmp("your-generated-psk-here");
    ```

=== "Arduino (production)"
    Store in ESP32 Preferences (NVS wrapper) with flash encryption enabled:
    ```cpp
    #include <Preferences.h>

    Preferences prefs;
    prefs.begin("usmp", true); // Open in read-only mode
    String psk = prefs.getString("psk");
    prefs.end();

    USMPClient usmp(psk.c_str());
    ```

=== "Python"
    ```python
    import os

    # From environment variable
    PSK = os.environ["USMP_PSK"].encode()

    # Or from a secure file
    PSK = open("/etc/usmp/psk", "rb").read().strip()
    ```

## Multi-device PSK

For larger deployments, you can configure unique PSKs per device. The Python SDK gateway (`USMPServer`) natively supports this in two ways:

1. **Registry Map (`dict`)**: Pass a dictionary mapping device IDs (`bytes`) to unique PSKs:

    ```python
    psk_registry = {
        b"device-id-1": b"unique-psk-for-device-1",
        b"device-id-2": b"unique-psk-for-device-2",
    }
    server = USMPServer(host="0.0.0.0", port=9000, psk=psk_registry)
    ```

2. **Dynamic Lookup (`Callable`)**: Pass a dynamic function matching the signature `def get_psk(device_id: bytes) -> bytes`. For example, deriving keys dynamically from a master secret:

    ```python
    import hmac, hashlib, os

    MASTER_SECRET = os.environ["USMP_MASTER_SECRET"].encode()

    def get_psk(device_id: bytes) -> bytes:
        # derive a device-unique PSK dynamically using HMAC-SHA256
        return hmac.new(MASTER_SECRET, device_id, hashlib.sha256).digest()

    server = USMPServer(host="0.0.0.0", port=9000, psk=get_psk)
    ```

## Security Rules

!!! danger
    - **Never** use the default development PSK in production.
    - **Never** commit PSKs to source control repositories.
    - **Never** log or print PSKs to serial consoles, files, or stdout.
    - Rotate PSKs immediately if a device is physically compromised.
    - Enable **Flash Encryption** and **Secure Boot** on the ESP32 to prevent extraction of the PSK from NVS flash storage.
