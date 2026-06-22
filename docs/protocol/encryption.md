# USMP Encryption & Frame Protection

All post-handshake frames in USMP (`DATA`, `PING`, `PONG`, `BYE`) are protected using **AES-256-GCM** (Galois/Counter Mode). This provides confidentiality, data integrity, and origin authenticity. If a frame has been altered or tampered with in transit, decryption will fail immediately.

---

## 1. AES-256-GCM Parameters

### Explanation
AES-GCM requires three inputs in addition to the plaintext and key: a secret key, a unique nonce, and Additional Authenticated Data (AAD).

**Key Parameters:**
* **Session Key**: 256 bits (32 bytes), derived via HKDF-SHA256 during the handshake.
* **GCM Nonce**: 96 bits (12 bytes), generated fresh using a cryptographically secure random number generator (CSRPNG) for each frame.
* **GCM Tag**: 128 bits (16 bytes), generated at the end of the ciphertext to authenticate the payload and AAD.

---

## 2. Nonce Generation

### Explanation
To prevent nonce-reuse attacks (which completely compromise AES-GCM security), USMP generates a **fresh, random 12-byte nonce** for each outgoing encrypted packet. The nonce is prepended directly to the ciphertext in the frame's payload field.

**On-the-wire Payload Layout:**
```
[ 12-byte random Nonce ] [ Ciphertext (Length - 28 bytes) ] [ 16-byte Auth Tag ]
```

### Implementation

#### Client-Side Nonce Generation (C)
```c
/* Generate a fresh random nonce — never derived from sequence numbers */
uint8_t nonce[12];
if (usmp_port_random(nonce, 12) != 0) {
    return -1;
}
```

#### Server-Side Nonce Generation (Python)
```python
import os
# Generate 12 cryptographically random bytes
nonce = os.urandom(12)
```

---

## 3. Additional Authenticated Data (AAD)

### Explanation
To protect the frame header from spoofing or tampering, USMP feeds the first 10 bytes of the header into the AES-GCM engine as AAD. The AAD is verified but is left unencrypted in the header on the wire.

**AAD Structure (10 bytes):**
* `0..1` (2 bytes): `magic` (Frame marker, always `0xABCD` little-endian)
* `2` (1 byte): `version` (Protocol version, currently `0x01`)
* `3` (1 byte): `type` (Packet type, e.g. `0x05` for `DATA`)
* `4..7` (4 bytes): `seq` (Sequence number, little-endian)
* `8..9` (2 bytes): `length` (Total payload length: `12 + plaintext_len + 16`, little-endian)

*Note: The 2-byte `crc` field at offsets 10-11 is excluded from AAD.*

### Implementation

#### Client-Side AAD Packaging (C)
```c
static void build_aad(uint16_t magic, uint8_t version, uint8_t type,
                      uint32_t seq, uint16_t length, uint8_t *aad) {
  aad[0] = magic & 0xFF;
  aad[1] = (magic >> 8) & 0xFF;
  aad[2] = version;
  aad[3] = type;
  aad[4] = seq & 0xFF;
  aad[5] = (seq >> 8) & 0xFF;
  aad[6] = (seq >> 16) & 0xFF;
  aad[7] = (seq >> 24) & 0xFF;
  aad[8] = length & 0xFF;
  aad[9] = (length >> 8) & 0xFF;
}
```

#### Server-Side AAD Packaging (Python)
```python
import struct

def build_aad(magic: int, version: int, type_: int, seq: int, length: int) -> bytes:
    return (
        struct.pack("<H", magic)
        + struct.pack("<B", version)
        + struct.pack("<B", type_)
        + struct.pack("<I", seq)
        + struct.pack("<H", length)
    )
```

---

## 4. Encryption

### Explanation
Plaintext application data is encrypted, producing a ciphertext of identical length and a 16-byte authentication tag.

### Implementation

#### Client-Side Encryption (C using mbedTLS)
```c
mbedtls_gcm_context gcm;
mbedtls_gcm_init(&gcm);

mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);

uint8_t *ct_out  = out + 12;         // Ciphertext placement
uint8_t *tag_out = ct_out + plain_len; // Tag placement

mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                           plain_len,
                           nonce, 12,
                           aad, aad_len,
                           plaintext, ct_out,
                           16, tag_out);

// Prepend the nonce so the receiver can extract it
memcpy(out, nonce, 12);
*out_len = 12 + plain_len + 16;

mbedtls_gcm_free(&gcm);
```

#### Server-Side Encryption (Python)
```python
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# enc_length = 12 (nonce) + plaintext_len + 16 (tag)
aad = build_aad(magic, version, type_, seq, 12 + len(plaintext) + 16)

aesgcm = AESGCM(key)
# cryptography library automatically returns ciphertext + tag
ciphertext_and_tag = aesgcm.encrypt(nonce, plaintext, aad)
payload = nonce + ciphertext_and_tag
```

---

## 5. Decryption & Authentication

### Explanation
The receiver extracts the 12-byte nonce, reconstructs the AAD, and runs the AES-GCM decryption engine to verify the tag and restore the plaintext.

### Implementation

#### Client-Side Decryption (C using mbedTLS)
```c
const uint8_t *nonce      = nonce_ct_tag;
const uint8_t *ciphertext = nonce_ct_tag + 12;
size_t cipher_len         = nct_len - 12 - 16;
const uint8_t *tag        = ciphertext + cipher_len;

mbedtls_gcm_context gcm;
mbedtls_gcm_init(&gcm);

mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);

int ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len,
                                   nonce, 12,
                                   aad, aad_len,
                                   tag, 16,
                                   ciphertext, out);
mbedtls_gcm_free(&gcm);

if (ret != 0) {
    // Decryption or tag verification failed!
    return -1;
}
*out_len = cipher_len;
```

#### Server-Side Decryption (Python)
```python
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

nonce = nonce_ct_tag[:12]
ct_tag = nonce_ct_tag[12:]
aad = build_aad(magic, version, type_, seq, length)

aesgcm = AESGCM(key)
try:
    plaintext = aesgcm.decrypt(nonce, ct_tag, aad)
except InvalidTag:
    # Handle verification failure - teardown connection
    raise CryptoError("Authentication tag verification failed")
```
