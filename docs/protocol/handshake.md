# USMP Handshake: Setting Up a Secure Session

The USMP handshake is where the magic starts. It is a mutually authenticated, forward-secret exchange that establishes a secure tunnel in **4 messages**. On a standard ESP32, the entire process takes about **200ms** to complete.

Here is the high-level choreographic sequence:

```text
Client (ESP32 / Arduino)                        Server (Python Gateway)
      │                                                   │
      │─────────────── 1. PKT_HELLO (0x01) ──────────────▶│
      │           [device_id(6) || pub_C(32)]             │
      │                                                   │
      │◀────────────── 2. PKT_CHALLENGE (0x02) ───────────│
      │             [nonce(32) || pub_S(32)]              │
      │                                                   │
      │         [Both derive session keys locally]        │
      │                                                   │
      │─────────────── 3. PKT_HELLO_ACK (0x03) ──────────▶│
      │                 [hmac_client(32)]                 │
      │                                                   │
      │◀────────────── 4. PKT_SESSION_OK (0x04) ──────────│
      │         [session_id(16) || hmac_server(32)]       │
      │                                                   │
      │═══════════════ SESSION ESTABLISHED ═══════════════│
```

## A Conversational Handshake Dialogue

Let's tell the story of the handshake as a friendly dialogue between the Client and the Server:

1. **Client (`PKT_HELLO`)**: *"Hey there! I want to establish a secure session. Here is my hardware address (`device_id`) and an ephemeral public key (`pub_C`) I just generated."*
2. **Server (`PKT_CHALLENGE`)**: *"Hi! I see you. To start, here is my own ephemeral public key (`pub_S`) along with a random value (`nonce`) we'll use to mix up our keys."*
3. **Client (`PKT_HELLO_ACK`)**: *"Awesome. I've computed our shared secret using Curve25519, run it through HKDF, and generated session keys. To prove I know our Pre-Shared Key (PSK), I've signed the nonce and our public keys with an HMAC. Here it is!"*
4. **Server (`PKT_SESSION_OK`)**: *"Looks perfect! I've checked your HMAC proof, and it matches. To prove that I also know the PSK (so you know you're not talking to an imposter), here is my own HMAC proof along with a unique `session_id` to track our connection."*

Now both sides are fully authenticated, they share identical symmetric session keys, and they're ready to communicate securely!

## Step-by-Step Packet Details

Let's inspect the binary layout and implementation of each handshake step.

### Step 1: `PKT_HELLO` (0x01)

The client initiates the handshake by announcing its physical hardware identity (`device_id`) and sending its ephemeral Curve25519 public key (`pub_C`).

#### Payload Layout (38 bytes)

* **Bytes `0..5` (6 bytes)**: `device_id` (e.g. WiFi MAC address).
* **Bytes `6..37` (32 bytes)**: `pub_C` (ephemeral X25519 public key generated fresh for this session).

#### Implementation

* **Client (C)**: Generates a temporary Curve25519 keypair and writes the packet payload:

    ```c
    uint8_t pub_buf[65];
    size_t pub_buf_len = 0;
    mbedtls_ecdh_make_public(&ecdh, &pub_buf_len, pub_buf, sizeof(pub_buf), 
                             mbedtls_ctr_drbg_random, &ctr_drbg);
    uint8_t *pub_c = pub_buf + (pub_buf_len - 32);

    usmp_packet_t pkt;
    pkt.magic = USMP_MAGIC;
    pkt.version = 1;
    pkt.type = USMP_TYPE_HELLO;
    pkt.seq = 0;
    pkt.length = 38;
    memcpy(pkt.payload, session->device_id, 6);
    memcpy(pkt.payload + 6, pub_c, 32);

    // Build and send the packet...
    ```

* **Server (Python)**: Parses the HELLO frame and matches the client's `device_id` against the registered Pre-Shared Keys:

    ```python
    frame = await read_frame(reader, verify_crc=False)
    if frame.type != PacketType.HELLO or frame.length != 38:
        raise HandshakeError("Invalid HELLO frame received")

    device_id = frame.payload[:6]
    pub_c = frame.payload[6:38]
    ```

### Step 2: `PKT_CHALLENGE` (0x02)

The server replies with a random challenge salt (`nonce`) and its own ephemeral public key (`pub_S`).

#### Payload Layout (64 bytes)

* **Bytes `0..31` (32 bytes)**: `nonce` (cryptographically secure random salt).
* **Bytes `32..63` (32 bytes)**: `pub_S` (server's ephemeral X25519 public key).

#### Implementation

* **Server (Python)**: Generates a server X25519 keypair and random nonce:

    ```python
    priv_s, pub_s = generate_keypair()
    nonce = os.urandom(32)
    await write_frame(writer, PacketType.CHALLENGE, nonce + pub_s)
    ```

* **Client (C)**: Extracts the nonce and server public key from the received packet:

    ```c
    uint8_t nonce[32];
    uint8_t pub_s[32];
    memcpy(nonce, pkt.payload, 32);
    memcpy(pub_s, pkt.payload + 32, 32);
    ```

### Key Derivation (ECDH & HKDF)

Before proceeding, both sides combine their private keys with the other side's public key to compute a Curve25519 shared secret. They run this secret through HKDF-SHA256:

$$\text{shared\_secret} = \text{X25519}(\text{priv\_local}, \text{pub\_peer})$$

$$\text{session\_key} = \text{HKDF-SHA256}(\text{ikm}=\text{shared\_secret}, \text{salt}=\text{nonce}, \text{info}=\text{"usmp-v1"} \parallel \text{pub\_C} \parallel \text{pub\_S}, \text{len}=32)$$

> [!TIP]
> **Why bind the public keys to the HKDF `info` block?**
> By including `pub_C` and `pub_S` in the HKDF info block, the derived session key is cryptographically tied to this specific key exchange. This protects the protocol from unknown key-share exploits.

### Step 3: `PKT_HELLO_ACK` (0x03)

The client authenticates itself by sending an HMAC proof. To prevent Man-in-the-Middle (MITM) key-swapping, the HMAC is computed over the challenge `nonce`, `device_id`, and **both public keys** (`pub_C` and `pub_S`).

#### Payload Layout (32 bytes)

* **Bytes `0..31` (32 bytes)**: `hmac_client` = $\text{HMAC-SHA256}(\text{PSK}, \text{nonce} \parallel \text{device\_id} \parallel \text{pub\_C} \parallel \text{pub\_S})$

#### Implementation

* **Client (C)**: Concatenates parameters and computes the client HMAC:

    ```c
    uint8_t hmac_client[32];
    uint8_t input[32 + 6 + 32 + 32];
    memcpy(input, nonce, 32);
    memcpy(input + 32, session->device_id, 6);
    memcpy(input + 32 + 6, pub_c, 32);
    memcpy(input + 32 + 6 + 32, pub_s, 32);

    compute_hmac(psk, psk_len, input, sizeof(input), hmac_client);
    ```

* **Server (Python)**: Computes the expected HMAC locally and performs a constant-time comparison to verify:

    ```python
    expected_client = _compute_hmac(resolved_psk, nonce, device_id, pub_c, pub_s)
    if not hmac.compare_digest(expected_client, frame.payload):
        raise AuthError("Client HMAC verification failed")
    ```

### Step 4: `PKT_SESSION_OK` (0x04)

The server authenticates itself by sending back a unique `session_id` and a server HMAC proof computed over the `nonce`, `session_id`, and both public keys.

#### Payload Layout (48 bytes)

* **Bytes `0..15` (16 bytes)**: `session_id` (cryptographically random session identifier).
* **Bytes `16..47` (32 bytes)**: `hmac_server` = $\text{HMAC-SHA256}(\text{PSK}, \text{nonce} \parallel \text{session\_id} \parallel \text{pub\_C} \parallel \text{pub\_S})$

#### Implementation

* **Server (Python)**: Generates a session ID, computes the HMAC proof, and transmits:

    ```python
    session_id = os.urandom(16)
    hmac_server = _compute_hmac(resolved_psk, nonce, session_id, pub_c, pub_s)
    await write_frame(writer, PacketType.SESSION_OK, session_id + hmac_server)
    ```

* **Client (C)**: Computes the expected server HMAC and performs a constant-time memory comparison (`mbedtls_ct_memcmp`) to verify:

    ```c
    uint8_t session_id[16];
    uint8_t hmac_server_received[32];
    // ... unpack payload

    uint8_t hmac_server_expected[32];
    uint8_t input[32 + 16 + 32 + 32];
    // input = nonce(32) || session_id(16) || pub_c(32) || pub_s(32)
    memcpy(input, nonce, 32);
    memcpy(input + 32, session_id, 16);
    memcpy(input + 32 + 16, pub_c, 32);
    memcpy(input + 32 + 16 + 32, pub_s, 32);
    compute_hmac(psk, psk_len, input, sizeof(input), hmac_server_expected);

    if (mbedtls_ct_memcmp(hmac_server_received, hmac_server_expected, 32) != 0) {
        USMP_LOGE(TAG, "Server HMAC verification FAILED — possible rogue server");
        // Close transport and exit...
    }
    ```

> [!IMPORTANT]
> **Constant-Time Verification**
> Both sides MUST perform constant-time comparisons (`hmac.compare_digest` in Python, `mbedtls_ct_memcmp` in C) when validating handshakes. Traditional string or buffer comparisons exit early upon finding the first mismatching byte, exposing the handshake to timing attacks.
