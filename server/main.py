import socket
import struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from dxp_parser import parse_frame, DXPParseError, DXP_TYPE_DATA
from handshake import run_handshake

HOTSPOT_IP = "192.168.137.1"
PORT = 9000
GCM_TAG_LEN = 16


def build_nonce(seq: int, session_id: bytes) -> bytes:
    return seq.to_bytes(4, "little") + session_id + b"\x00" * 4


def decrypt_frame(frame, session_key: bytes, session_id: bytes) -> bytes | None:
    nonce = build_nonce(frame.seq, session_id)

    # AAD = magic(2) + ver(1) + type(1) + seq(4) + zeros(2) = 10 bytes
    # matches ESP32 dxp_session.c AAD construction exactly
    aad = struct.pack("<H", frame.magic)
    aad += struct.pack("<B", frame.version)
    aad += struct.pack("<B", frame.type)
    aad += struct.pack("<I", frame.seq)
    aad += b"\x00\x00"  # length placeholder

    aesgcm = AESGCM(session_key)
    try:
        plaintext = aesgcm.decrypt(nonce, frame.payload, aad)
        return plaintext
    except Exception as e:
        print(f"[CRYPTO] Decryption failed: {e}")
        return None


def get_local_ip():
    import socket as s

    try:
        with s.socket(s.AF_INET, s.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            return sock.getsockname()[0]
    except Exception:
        return "127.0.0.1"


server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

try:
    server.bind((HOTSPOT_IP, PORT))
    print(f"Bound to {HOTSPOT_IP}:{PORT}")
except OSError as e:
    print(f"Bind failed ({e}), falling back to 0.0.0.0")
    server.bind(("0.0.0.0", PORT))

server.listen(1)
print(f"Listening on {server.getsockname()[0]}:{PORT}\n")

while True:
    conn, addr = server.accept()
    print(f"TCP connected: {addr}")
    try:
        session = run_handshake(conn)
        if session is None:
            print("[HS] Handshake failed, dropping connection")
            continue

        print(f"[HS] Session established: {session}\n")

        session_key = bytes.fromhex(session["session_key"])
        session_id = bytes.fromhex(session["session_id"])
        rx_seq = 0

        while True:
            data = conn.recv(1024)
            if not data:
                break
            try:
                frame = parse_frame(data)
                if frame.type == DXP_TYPE_DATA:
                    if frame.seq != rx_seq:
                        print(
                            f"[CRYPTO] Seq mismatch: expected {rx_seq} got {frame.seq}"
                        )
                        break
                    plaintext = decrypt_frame(frame, session_key, session_id)
                    if plaintext is None:
                        print("[CRYPTO] Auth failed — dropping connection")
                        break
                    print(f"[DATA] seq={frame.seq} → {plaintext!r}")
                    rx_seq += 1
                else:
                    print(f"RX: {frame}")
            except DXPParseError as e:
                print(f"Parse error: {e}")
    finally:
        conn.close()
        print("Connection closed, waiting for next client...\n")
