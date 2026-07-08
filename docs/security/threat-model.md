# Security Threat Model & Assumptions

To understand how USMP protects your IoT devices, it is helpful to see what threats the protocol is built to counter, what assumptions we make, and what issues lie outside our scope.

## Basic Assumptions

USMP operates under three core security assumptions:

1. **The Network is Hostile**: We assume an attacker can intercept, view, modify, inject, or replay any packet sent over your communication transport (Wi-Fi, Serial wires, UDP, BLE).
2. **The PSK is Secret**: We assume that your Pre-Shared Key (PSK) was securely provisioned onto both the device and the gateway and has not been leaked to outside parties.
3. **Physical Security is Out-of-Scope**: We assume that if an attacker has physical possession of a device, they can extract keys using flash dumps or debugging pins, unless you have configured advanced hardware-level protections (like ESP32 Flash Encryption and Secure Boot).

## Threats and Responses

Here is how USMP defends your system against active and passive network attacks:

| Attack Vector | Attacker Goal | USMP Defense Strategy |
|:---|:---|:---|
| **Eavesdropping** | Read device data. | **AES-256-GCM Encryption**: All post-handshake payloads are fully encrypted, appearing as random noise to sniffers. |
| **Tampering** | Modify commands or payloads. | **GCM Authentication Tag**: If an attacker alters a single bit in a packet, the cryptographic tag check fails, and the session drops. |
| **Session Replay** | Replay a recorded handshake. | **Fresh Random Nonce**: The server sends a new random challenge nonce for every handshake. Old handshake responses won't match. |
| **Packet Replay** | Replay a valid command. | **Monotonic Sequence Numbers**: The receiver expects sequence numbers to increment strictly by 1. Replayed packets are ignored. |
| **Impersonation** | Mimic a valid device or gateway. | **Mutual HMAC Authentication**: Both sides must prove knowledge of the PSK using HMAC signatures. |
| **Man-in-the-Middle** | Intercept and relay data. | **Cryptographic Key Binding**: Ephermeral keys are hashed into the HMAC proofs, preventing attackers from swapping keys. |

## Detailed Attack Scenarios

### 1. The Sniffer (Passive Eavesdropper)

An attacker sits on the local Wi-Fi router and records every packet sent between your ESP32 and your gateway.

* **Result**: The attacker sees the handshake public keys, but cannot derive the session keys without knowing the private keys or the PSK.
* **Forward Secrecy**: Even if the attacker manages to steal your PSK in the future, they still cannot decrypt the traffic they recorded today, because the ephemeral keys were erased from the device's RAM as soon as the session ended.

### 2. The Impostor (Rogue Gateway)

An attacker sets up a fake server to mimic your gateway and tries to trick your device into sending telemetry or accepting control commands.

* **Result**: The rogue gateway cannot produce a valid `SESSION_OK` HMAC signature because it does not possess the secret PSK. The client detects the bad signature and closes the TCP socket immediately.

### 3. The Replayer (Sequence & Nonce Attacks)

An attacker captures a valid `OPEN_DOOR` data command and replays it later.

* **Result**: The receiver tracks the next expected sequence number. Since the replayed packet carries an old sequence number, the receiver rejects the packet and closes the connection.
* **GCM Nonce Safety**: Nonces are constructed as `seq || session_id[0..7]`. Monotonic sequence counters prevent nonce reuse within a session, and random session IDs ensure unique nonces across different sessions.

## Out of Scope (What USMP Does Not Do)

Some security protections must be managed at the system or hardware level:

* **Key Exfiltration**: If your microcontroller does not use Flash Encryption, an attacker with physical access can read the flash memory to extract the PSK. You must enable ESP32 Flash Encryption to secure your key storage.
* **Offline PSK Cracking**: USMP validates client and server authenticity using HMAC-SHA256 signatures over public handshakes. Because USMP does not use a Password-Authenticated Key Exchange (PAKE) protocol, a passive eavesdropper capturing the handshake can perform offline dictionary/brute-force attacks against weak or low-entropy PSKs. Users must ensure PSKs are cryptographically random and at least 16 bytes in size.
* **Python Memory Zeroization Limits**: While the C core zeroizes all derived keying materials, Python's runtime handles memory dynamically. Calling `del` on key variables in the Python SDK only drops references; the immutable `bytes` values remain in the heap memory until the garbage collector reclaims and overwrites them. Wiping keys from memory cannot be strictly guaranteed in Python.
* **Redundant On-Wire Nonces**: To simplify parsing, each frame explicitly carries the 12-byte GCM nonce on the wire. This introduces a ~2.5% overhead per frame, which is noted for future wire-format optimization.
* **Volumetric DDoS**: While the USMP Python server rate-limits handshake floods to protect its CPU, it cannot block network-level packet flooding. You must configure standard network firewalls (like `iptables` or cloud firewalls) to block flood traffic.
* **Quantum Cryptography**: Our X25519 key exchange is not quantum-resistant. Post-quantum upgrades are planned for future versions of the protocol.
