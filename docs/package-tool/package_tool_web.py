"""Browser-compatible QBoot RBL packager, unpacker, and transformer core."""

import binascii
import struct
import time
import zlib
from typing import Optional

QBOOT_ALGO_CRYPT_NONE = 0
QBOOT_ALGO_CRYPT_XOR  = 1
QBOOT_ALGO_CRYPT_AES  = 2

QBOOT_ALGO_CMPRS_NONE       = (0 << 8)
QBOOT_ALGO_CMPRS_GZIP       = (1 << 8)
QBOOT_ALGO_CMPRS_QUICKLZ    = (2 << 8)
QBOOT_ALGO_CMPRS_FASTLZ     = (3 << 8)
QBOOT_ALGO_CMPRS_HPATCHLITE = (4 << 8)

QBOOT_ALGO2_VERIFY_NONE = 0
QBOOT_ALGO2_VERIFY_CRC  = 1

QBOOT_HEADER_SIZE = 96
QBOOT_DEFAULT_AES_IV = "0123456789ABCDEF"
QBOOT_DEFAULT_AES_KEY = "0123456789ABCDEF0123456789ABCDEF"

QBOOT_CRYPT_ALGOS = {
    "none": QBOOT_ALGO_CRYPT_NONE,
    "xor":  QBOOT_ALGO_CRYPT_XOR,
    "aes":  QBOOT_ALGO_CRYPT_AES,
}

QBOOT_CMPRS_ALGOS = {
    "none":       QBOOT_ALGO_CMPRS_NONE,
    "gzip":       QBOOT_ALGO_CMPRS_GZIP,
    "quicklz":    QBOOT_ALGO_CMPRS_QUICKLZ,
    "fastlz":     QBOOT_ALGO_CMPRS_FASTLZ,
    "hpatchlite": QBOOT_ALGO_CMPRS_HPATCHLITE,
}

QBOOT_ALGO2_ALGOS = {
    "none": QBOOT_ALGO2_VERIFY_NONE,
    "crc":  QBOOT_ALGO2_VERIFY_CRC,
}

QBOOT_CRYPT_NAMES = {value: key for key, value in QBOOT_CRYPT_ALGOS.items()}
QBOOT_CMPRS_NAMES = {value: key for key, value in QBOOT_CMPRS_ALGOS.items()}
QBOOT_ALGO2_NAMES = {value: key for key, value in QBOOT_ALGO2_ALGOS.items()}

AES_SBOX = [
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
]

AES_INV_SBOX = [
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d,
]

AES_RCON = [0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36]


class PackageToolError(ValueError):
    """Raised when a browser-side package option is invalid."""


def crc32(data: bytes) -> int:
    """Return the unsigned CRC32 value used by the RBL header."""
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_algo_strict(opt_name: str, value: str, table: dict) -> int:
    """Parse a case-insensitive algorithm option against an exact table."""
    if value is None:
        value = ""
    normalized = str(value).strip().lower()
    if normalized not in table:
        allowed = ", ".join(table.keys())
        raise PackageToolError(f"invalid {opt_name} '{value}'. Allowed: {allowed}")
    return table[normalized]


def _pack_string(value: str, size: int) -> bytes:
    """Pack a UTF-8 string with the same truncation semantics as CLI."""
    return struct.pack(f"{size}s", str(value).encode("utf-8"))


def _unpack_string(value: bytes) -> str:
    """Unpack a NUL-padded UTF-8 metadata field."""
    return value.rstrip(b"\0").decode("utf-8", "replace")


def _normalize_bytes(value: bytes, name: str) -> bytes:
    """Normalize byte-like inputs used by the Pyodide boundary."""
    if not isinstance(value, (bytes, bytearray)):
        raise PackageToolError(f"{name} must be bytes")
    return bytes(value)


def _decode_key_material(value: Optional[str], default: str, expected_len: int,
                         name: str) -> bytes:
    """Decode AES key or IV text as hex when prefixed with hex:, else UTF-8."""
    text = default if value is None or str(value) == "" else str(value)
    if text.startswith("hex:"):
        try:
            data = binascii.unhexlify(text[4:].replace(" ", ""))
        except (binascii.Error, ValueError) as exc:
            raise PackageToolError(f"invalid {name} hex value") from exc
    else:
        data = text.encode("utf-8")
    if len(data) != expected_len:
        raise PackageToolError(f"{name} must be exactly {expected_len} bytes")
    return data


def _xtime(value: int) -> int:
    """Multiply one byte by x in GF(2^8)."""
    value <<= 1
    if value & 0x100:
        value ^= 0x11B
    return value & 0xFF


def _gmul(left: int, right: int) -> int:
    """Multiply two AES field bytes."""
    result = 0
    for _ in range(8):
        if right & 1:
            result ^= left
        left = _xtime(left)
        right >>= 1
    return result


def _key_expansion(key: bytes) -> tuple[list[list[int]], int]:
    """Expand an AES-128/192/256 key into round keys."""
    if len(key) not in (16, 24, 32):
        raise PackageToolError("AES key must be 16, 24, or 32 bytes")

    nk = len(key) // 4
    nr = nk + 6
    words = [list(key[i:i + 4]) for i in range(0, len(key), 4)]
    for index in range(nk, 4 * (nr + 1)):
        temp = words[index - 1][:]
        if index % nk == 0:
            temp = temp[1:] + temp[:1]
            temp = [AES_SBOX[b] for b in temp]
            temp[0] ^= AES_RCON[index // nk]
        elif nk > 6 and index % nk == 4:
            temp = [AES_SBOX[b] for b in temp]
        words.append([a ^ b for a, b in zip(words[index - nk], temp)])

    round_keys = []
    for round_index in range(nr + 1):
        round_key = []
        for word in words[round_index * 4:(round_index + 1) * 4]:
            round_key.extend(word)
        round_keys.append(round_key)
    return round_keys, nr


def _add_round_key(state: list[int], round_key: list[int]) -> None:
    """Apply one AES round key in-place."""
    for index, key_byte in enumerate(round_key):
        state[index] ^= key_byte


def _sub_bytes(state: list[int]) -> None:
    """Apply AES SubBytes in-place."""
    for index, value in enumerate(state):
        state[index] = AES_SBOX[value]


def _inv_sub_bytes(state: list[int]) -> None:
    """Apply AES inverse SubBytes in-place."""
    for index, value in enumerate(state):
        state[index] = AES_INV_SBOX[value]


def _shift_rows(state: list[int]) -> None:
    """Apply AES ShiftRows in-place."""
    state[1], state[5], state[9], state[13] = state[5], state[9], state[13], state[1]
    state[2], state[6], state[10], state[14] = state[10], state[14], state[2], state[6]
    state[3], state[7], state[11], state[15] = state[15], state[3], state[7], state[11]


def _inv_shift_rows(state: list[int]) -> None:
    """Apply AES inverse ShiftRows in-place."""
    state[1], state[5], state[9], state[13] = state[13], state[1], state[5], state[9]
    state[2], state[6], state[10], state[14] = state[10], state[14], state[2], state[6]
    state[3], state[7], state[11], state[15] = state[7], state[11], state[15], state[3]


def _mix_columns(state: list[int]) -> None:
    """Apply AES MixColumns in-place."""
    for col in range(4):
        offset = col * 4
        a0, a1, a2, a3 = state[offset:offset + 4]
        state[offset + 0] = _gmul(a0, 2) ^ _gmul(a1, 3) ^ a2 ^ a3
        state[offset + 1] = a0 ^ _gmul(a1, 2) ^ _gmul(a2, 3) ^ a3
        state[offset + 2] = a0 ^ a1 ^ _gmul(a2, 2) ^ _gmul(a3, 3)
        state[offset + 3] = _gmul(a0, 3) ^ a1 ^ a2 ^ _gmul(a3, 2)


def _inv_mix_columns(state: list[int]) -> None:
    """Apply AES inverse MixColumns in-place."""
    for col in range(4):
        offset = col * 4
        a0, a1, a2, a3 = state[offset:offset + 4]
        state[offset + 0] = _gmul(a0, 14) ^ _gmul(a1, 11) ^ _gmul(a2, 13) ^ _gmul(a3, 9)
        state[offset + 1] = _gmul(a0, 9) ^ _gmul(a1, 14) ^ _gmul(a2, 11) ^ _gmul(a3, 13)
        state[offset + 2] = _gmul(a0, 13) ^ _gmul(a1, 9) ^ _gmul(a2, 14) ^ _gmul(a3, 11)
        state[offset + 3] = _gmul(a0, 11) ^ _gmul(a1, 13) ^ _gmul(a2, 9) ^ _gmul(a3, 14)


def aes_ecb_encrypt_block(block: bytes, key: bytes) -> bytes:
    """Encrypt one 16-byte AES block."""
    if len(block) != 16:
        raise PackageToolError("AES block must be exactly 16 bytes")
    round_keys, nr = _key_expansion(key)
    state = list(block)
    _add_round_key(state, round_keys[0])
    for round_index in range(1, nr):
        _sub_bytes(state)
        _shift_rows(state)
        _mix_columns(state)
        _add_round_key(state, round_keys[round_index])
    _sub_bytes(state)
    _shift_rows(state)
    _add_round_key(state, round_keys[nr])
    return bytes(state)


def aes_ecb_decrypt_block(block: bytes, key: bytes) -> bytes:
    """Decrypt one 16-byte AES block."""
    if len(block) != 16:
        raise PackageToolError("AES block must be exactly 16 bytes")
    round_keys, nr = _key_expansion(key)
    state = list(block)
    _add_round_key(state, round_keys[nr])
    for round_index in range(nr - 1, 0, -1):
        _inv_shift_rows(state)
        _inv_sub_bytes(state)
        _add_round_key(state, round_keys[round_index])
        _inv_mix_columns(state)
    _inv_shift_rows(state)
    _inv_sub_bytes(state)
    _add_round_key(state, round_keys[0])
    return bytes(state)


def aes_cbc_encrypt(data: bytes, key_text: Optional[str] = None,
                    iv_text: Optional[str] = None) -> bytes:
    """Encrypt data with QBoot-compatible raw AES-CBC and no padding."""
    data = _normalize_bytes(data, "AES plaintext")
    if len(data) % 16 != 0:
        raise PackageToolError("AES input length must be a multiple of 16 bytes")
    key = _decode_key_material(key_text, QBOOT_DEFAULT_AES_KEY, 32, "AES key")
    previous = _decode_key_material(iv_text, QBOOT_DEFAULT_AES_IV, 16, "AES IV")
    output = bytearray()
    for offset in range(0, len(data), 16):
        block = bytes(data[offset + index] ^ previous[index] for index in range(16))
        encrypted = aes_ecb_encrypt_block(block, key)
        output.extend(encrypted)
        previous = encrypted
    return bytes(output)


def aes_cbc_decrypt(data: bytes, key_text: Optional[str] = None,
                    iv_text: Optional[str] = None) -> bytes:
    """Decrypt data with QBoot-compatible raw AES-CBC and no padding."""
    data = _normalize_bytes(data, "AES ciphertext")
    if len(data) % 16 != 0:
        raise PackageToolError("AES input length must be a multiple of 16 bytes")
    key = _decode_key_material(key_text, QBOOT_DEFAULT_AES_KEY, 32, "AES key")
    previous = _decode_key_material(iv_text, QBOOT_DEFAULT_AES_IV, 16, "AES IV")
    output = bytearray()
    for offset in range(0, len(data), 16):
        block = data[offset:offset + 16]
        decrypted = aes_ecb_decrypt_block(block, key)
        output.extend(decrypted[index] ^ previous[index] for index in range(16))
        previous = block
    return bytes(output)


def gzip_compress(data: bytes) -> bytes:
    """Compress raw firmware into a gzip stream accepted by QBoot zlib inflate."""
    compressor = zlib.compressobj(level=9, wbits=31)
    return compressor.compress(_normalize_bytes(data, "gzip input")) + compressor.flush()


def gzip_decompress(data: bytes) -> bytes:
    """Decompress a gzip or zlib stream using QBoot-compatible auto detection."""
    return zlib.decompress(_normalize_bytes(data, "gzip input"), wbits=47)




QBOOT_FASTLZ_BLOCK_HDR_SIZE = 4
QBOOT_QUICKLZ_BLOCK_HDR_SIZE = 4
HPATCHLITE_MAGIC = b"hI"
HPATCHLITE_VERSION_CODE = 1
HPATCHLITE_COMPRESS_NONE = 0
HPATCHLITE_COMPRESS_TUZ = 1
HPATCHLITE_PATCH_COMPRESS_ALGOS = {"none", "tuz"}
TUZ_DICT_SIZE_SAVED_BYTES = 4
TUZ_MIN_LITERAL_LEN = 15
TUZ_MIN_DICT_MATCH_LEN = 2
TUZ_BIG_POS_FOR_LEN = (1 << 11) + (1 << 9) + (1 << 7) - 1



def _pack_block_size(payload: bytes) -> bytes:
    """Pack a QBoot big-endian one-shot compression block size."""
    if not payload:
        raise PackageToolError("compressed block must not be empty")
    if len(payload) > 0xFFFFFFFF:
        raise PackageToolError("compressed block is too large")
    return struct.pack(">I", len(payload)) + payload


def _unpack_block_size(data: bytes, algo_name: str) -> bytes:
    """Return the payload following a QBoot big-endian block-size header."""
    block = _normalize_bytes(data, f"{algo_name} block")
    if len(block) < 4:
        raise PackageToolError(f"{algo_name} block is missing the 4-byte size header")
    block_size = struct.unpack(">I", block[:4])[0]
    if block_size == 0:
        raise PackageToolError(f"{algo_name} block size must not be zero")
    if len(block) != block_size + 4:
        raise PackageToolError(f"{algo_name} block size does not match package body")
    return block[4:]


def fastlz_compress(data: bytes) -> bytes:
    """Encode raw bytes as a QBoot FastLZ block using literal-only level-1 records."""
    raw = _normalize_bytes(data, "FastLZ input")
    if not raw:
        raise PackageToolError("FastLZ input must not be empty")
    payload = bytearray()
    offset = 0
    while offset < len(raw):
        chunk = raw[offset:offset + 32]
        payload.append(len(chunk) - 1)
        payload.extend(chunk)
        offset += len(chunk)
    return _pack_block_size(bytes(payload))


def _fastlz_decompress_payload(payload: bytes, maxout: int) -> bytes:
    """Decode a FastLZ level-1 or level-2 payload."""
    if not payload:
        raise PackageToolError("FastLZ payload is empty")
    level = (payload[0] >> 5) + 1
    if level not in (1, 2):
        raise PackageToolError(f"unsupported FastLZ level {level}")

    data = payload
    ip = 1
    ctrl = data[0] & 31
    out = bytearray()
    loop = True

    while loop:
        length = ctrl >> 5
        ofs = (ctrl & 31) << 8
        if ctrl >= 32:
            length -= 1
            ref = len(out) - ofs
            if level == 1:
                if length == 6:
                    if ip >= len(data):
                        raise PackageToolError("truncated FastLZ length extension")
                    length += data[ip]
                    ip += 1
                if ip >= len(data):
                    raise PackageToolError("truncated FastLZ distance byte")
                ref -= data[ip]
                ip += 1
            else:
                if length == 6:
                    while True:
                        if ip >= len(data):
                            raise PackageToolError("truncated FastLZ length extension")
                        code = data[ip]
                        ip += 1
                        length += code
                        if code != 255:
                            break
                if ip >= len(data):
                    raise PackageToolError("truncated FastLZ distance byte")
                code = data[ip]
                ip += 1
                ref -= code
                if code == 255 and ofs == (31 << 8):
                    if ip + 2 > len(data):
                        raise PackageToolError("truncated FastLZ far distance")
                    ofs = (data[ip] << 8) + data[ip + 1]
                    ip += 2
                    ref = len(out) - ofs - 8191

            if ref - 1 < 0:
                raise PackageToolError("invalid FastLZ reference")
            if len(out) + length + 3 > maxout:
                raise PackageToolError("FastLZ output exceeds raw_size")

            if ip < len(data):
                ctrl = data[ip]
                ip += 1
            else:
                loop = False

            if ref == len(out):
                out.extend([out[ref - 1]] * (length + 3))
            else:
                ref -= 1
                for _ in range(length + 3):
                    out.append(out[ref])
                    ref += 1
        else:
            count = ctrl + 1
            if ip + count > len(data):
                raise PackageToolError("truncated FastLZ literal run")
            if len(out) + count > maxout:
                raise PackageToolError("FastLZ output exceeds raw_size")
            out.extend(data[ip:ip + count])
            ip += count
            loop = ip < len(data)
            if loop:
                ctrl = data[ip]
                ip += 1
    return bytes(out)


def fastlz_decompress(data: bytes, raw_size: Optional[int] = None) -> bytes:
    """Decompress a QBoot FastLZ block."""
    payload = _unpack_block_size(data, "FastLZ")
    maxout = raw_size if raw_size is not None else 0xFFFFFFFF
    return _fastlz_decompress_payload(payload, maxout)


def quicklz_compress(data: bytes) -> bytes:
    """Encode raw bytes as a QBoot QuickLZ block using a stored level-1 packet."""
    raw = _normalize_bytes(data, "QuickLZ input")
    if not raw:
        raise PackageToolError("QuickLZ input must not be empty")
    if len(raw) < 216:
        packet = bytes([0x44, len(raw) + 3, len(raw)]) + raw
    else:
        total = len(raw) + 9
        packet = bytearray([0x46])
        packet.extend(struct.pack("<I", total))
        packet.extend(struct.pack("<I", len(raw)))
        packet.extend(raw)
        packet = bytes(packet)
    return _pack_block_size(packet)


def _quicklz_size_fields(packet: bytes) -> tuple[int, int, int]:
    """Return QuickLZ header size, compressed size, and decompressed size."""
    if not packet:
        raise PackageToolError("QuickLZ packet is empty")
    n = 4 if packet[0] & 0x02 else 1
    header_size = 2 * n + 1
    if len(packet) < header_size:
        raise PackageToolError("truncated QuickLZ packet header")
    comp_size = int.from_bytes(packet[1:1 + n], "little")
    raw_size = int.from_bytes(packet[1 + n:1 + 2 * n], "little")
    return header_size, comp_size, raw_size


def quicklz_decompress(data: bytes, raw_size: Optional[int] = None) -> bytes:
    """Decompress a QBoot QuickLZ stored block."""
    packet = _unpack_block_size(data, "QuickLZ")
    header_size, comp_size, expected_raw_size = _quicklz_size_fields(packet)
    if comp_size != len(packet):
        raise PackageToolError("QuickLZ compressed size does not match block")
    if raw_size is not None and expected_raw_size != raw_size:
        raise PackageToolError("QuickLZ raw size does not match RBL header")
    if packet[0] & 0x01:
        raise PackageToolError(
            "compressed QuickLZ payloads need the native QuickLZ codec; "
            "this browser path supports stored packets"
        )
    restored = packet[header_size:]
    if len(restored) != expected_raw_size:
        raise PackageToolError("QuickLZ stored payload size does not match header")
    return restored


def _hpi_size_bytes(value: int) -> bytes:
    """Encode an HPatchLite size field in little-endian minimal form."""
    if value < 0 or value > 0xFFFFFFFF:
        raise PackageToolError("HPatchLite size is out of range")
    if value == 0:
        return b""
    size = (value.bit_length() + 7) // 8
    return value.to_bytes(size, "little")


def _hpi_pack_uint(value: int) -> bytes:
    """Encode an HPatchLite variable-length unsigned integer."""
    if value < 0 or value > 0xFFFFFFFF:
        raise PackageToolError("HPatchLite integer is out of range")
    groups = [value & 0x7F]
    value >>= 7
    while value:
        groups.append(value & 0x7F)
        value >>= 7
    groups.reverse()
    for index in range(len(groups) - 1):
        groups[index] |= 0x80
    return bytes(groups)


def _hpi_unpack_uint(data: bytes, offset: int, initial: int = 0,
                     has_next: bool = True) -> tuple[int, int]:
    """Decode an HPatchLite variable-length unsigned integer."""
    value = initial
    while has_next:
        if offset >= len(data):
            raise PackageToolError("truncated HPatchLite integer")
        code = data[offset]
        offset += 1
        value = (value << 7) | (code & 0x7F)
        has_next = bool(code >> 7)
    return value, offset


class _TuzWriter:
    """TinyUZ bit writer compatible with HPatchLite _CompressPlugin_tuz."""

    def __init__(self) -> None:
        self.code = bytearray()
        self.type_index = 0
        self.type_count = 0
        self.is_have_data_back = False

    def out_type(self, bit_value: int) -> None:
        """Append one low-to-high TinyUZ type bit."""
        if self.type_count == 0:
            self.type_index = len(self.code)
            self.code.append(0)
        if bit_value:
            self.code[self.type_index] |= 1 << self.type_count
        self.type_count += 1
        if self.type_count == 8:
            self.type_count = 0

    def out_len(self, value: int, pack_bit: int) -> None:
        """Append a TinyUZ variable-length integer to the type-bit stream."""
        if value < 0:
            raise PackageToolError("TinyUZ integer is out of range")
        count = 1
        work_value = value
        while work_value >= (1 << (count * pack_bit)):
            work_value -= 1 << (count * pack_bit)
            count += 1
        while count:
            count -= 1
            for bit_index in range(pack_bit):
                self.out_type((work_value >> (count * pack_bit + bit_index)) & 1)
            self.out_type(1 if count > 0 else 0)

    def out_dict_pos(self, pos: int) -> None:
        """Append a TinyUZ dictionary/control position byte sequence."""
        if pos < 0:
            raise PackageToolError("TinyUZ dictionary position is out of range")
        is_out_len = pos >= (1 << 7)
        if is_out_len:
            pos -= 1 << 7
        self.code.append((pos & 0x7F) | (0x80 if is_out_len else 0))
        if is_out_len:
            self.out_len(pos >> 7, 2)

    def out_ctrl(self, ctrl_type: int) -> None:
        """Append a TinyUZ control record."""
        self.out_type(0)
        self.out_len(ctrl_type, 1)
        if self.is_have_data_back:
            self.out_type(0)
        self.out_dict_pos(0)

    def out_data(self, data: bytes) -> None:
        """Append literal data using the TinyUZ literal-line encoding."""
        if len(data) >= TUZ_MIN_LITERAL_LEN:
            self.out_ctrl(1)
            self.out_len(len(data) - TUZ_MIN_LITERAL_LEN, 2)
            self.code.extend(data)
        else:
            for value in data:
                self.out_type(1)
                self.code.append(value)
        self.is_have_data_back = True

    def out_stream_end(self) -> None:
        """Append the TinyUZ stream-end control record."""
        self.out_ctrl(3)
        self.type_count = 0
        self.is_have_data_back = False


class _TuzReader:
    """TinyUZ low-to-high bit reader for HPatchLite patch streams."""

    def __init__(self, data: bytes) -> None:
        if len(data) < TUZ_DICT_SIZE_SAVED_BYTES:
            raise PackageToolError("truncated TinyUZ stream")
        self.data = data
        self.offset = TUZ_DICT_SIZE_SAVED_BYTES
        self.types = 0
        self.type_count = 0

    def read_byte(self) -> int:
        """Read one byte from the TinyUZ byte stream."""
        if self.offset >= len(self.data):
            raise PackageToolError("truncated TinyUZ byte stream")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_lowbits(self, bit_count: int) -> int:
        """Read low-to-high TinyUZ type bits."""
        count = self.type_count
        result = self.types
        if count >= bit_count:
            self.type_count = count - bit_count
            self.types = result >> bit_count
            return result & ((1 << bit_count) - 1)
        value = self.read_byte()
        bit_count -= count
        self.type_count = 8 - bit_count
        self.types = value >> bit_count
        return (result | (value << count)) & ((1 << (bit_count + count)) - 1)

    def unpack_len(self, read_bit: int) -> int:
        """Decode a TinyUZ variable-length integer from type bits."""
        value = 0
        while True:
            lowbit = self.read_lowbits(read_bit)
            value = (value << (read_bit - 1)) + (lowbit & ((1 << (read_bit - 1)) - 1))
            if not (lowbit & (1 << (read_bit - 1))):
                return value
            value += 1

    def unpack_dict_pos(self) -> int:
        """Decode a TinyUZ dictionary/control position."""
        result = self.read_byte()
        if result < (1 << 7):
            return result
        return ((result & 0x7F) | (self.unpack_len(3) << 7)) + (1 << 7)


def tuz_compress(data: bytes) -> bytes:
    """Compress data with the HPatchLite TinyUZ literal-line stream format.

    The browser uses TinyUZ because QBoot's HPatchLite path expects the
    _CompressPlugin_tuz patch-body compressor. This encoder intentionally
    emits literal-line records instead of match records, so it remains small
    and compatible with the TinyUZ decoder while avoiding a second generic
    zlib/raw-deflate path for differential packages.
    """
    payload = _normalize_bytes(data, "TinyUZ input")
    writer = _TuzWriter()
    writer.code.extend((1).to_bytes(TUZ_DICT_SIZE_SAVED_BYTES, "little"))
    writer.out_data(payload)
    writer.out_stream_end()
    return bytes(writer.code)


def tuz_decompress(data: bytes, expected_size: int) -> bytes:
    """Decompress an HPatchLite TinyUZ stream."""
    code = _normalize_bytes(data, "TinyUZ stream")
    if expected_size < 0:
        raise PackageToolError("TinyUZ expected size is out of range")
    if int.from_bytes(code[:TUZ_DICT_SIZE_SAVED_BYTES], "little") == 0:
        raise PackageToolError("TinyUZ dictionary size is zero")
    reader = _TuzReader(code)
    output = bytearray()
    dict_pos_back = 1
    is_have_data_back = False
    while True:
        if (reader.read_lowbits(1) & 1) == 0:
            saved_len = reader.unpack_len(2)
            if is_have_data_back and (reader.read_lowbits(1) & 1):
                saved_dict_pos = dict_pos_back
            else:
                saved_dict_pos = reader.unpack_dict_pos()
                if saved_dict_pos > TUZ_BIG_POS_FOR_LEN:
                    saved_len += 1
            is_have_data_back = False
            if saved_dict_pos:
                copy_len = saved_len + TUZ_MIN_DICT_MATCH_LEN
                dict_pos_back = saved_dict_pos
                if saved_dict_pos > len(output):
                    raise PackageToolError("TinyUZ dictionary position is out of range")
                for _ in range(copy_len):
                    output.append(output[-saved_dict_pos])
            else:
                if saved_len == 1:
                    literal_len = reader.unpack_len(3) + TUZ_MIN_LITERAL_LEN
                    if reader.offset + literal_len > len(code):
                        raise PackageToolError("truncated TinyUZ literal line")
                    output.extend(code[reader.offset:reader.offset + literal_len])
                    reader.offset += literal_len
                    is_have_data_back = True
                    continue
                dict_pos_back = 1
                reader.type_count = 0
                if saved_len == 2:
                    continue
                if saved_len == 3:
                    break
                raise PackageToolError("unsupported TinyUZ control type")
        else:
            output.append(reader.read_byte())
            is_have_data_back = True
        if len(output) > expected_size:
            raise PackageToolError("TinyUZ output exceeds expected size")
    if len(output) != expected_size:
        raise PackageToolError("TinyUZ output size does not match HPatchLite header")
    return bytes(output)


def hpatchlite_create_body(new_fw: bytes) -> bytes:
    """Create the uncompressed HPatchLite full-diff stream body."""
    new_bytes = _normalize_bytes(new_fw, "new firmware")
    body = bytearray()
    body.extend(_hpi_pack_uint(1))          # cover count
    body.extend(_hpi_pack_uint(0))          # zero-length sentinel cover
    body.append(0)                          # old position tag: absolute zero
    body.extend(_hpi_pack_uint(len(new_bytes)))
    body.extend(new_bytes)                  # diff data copied before the cover
    return bytes(body)


def hpatchlite_create_patch(old_fw: bytes, new_fw: bytes,
                            patch_compress: str = "none") -> bytes:
    """Create a native HPatchLite full-diff patch.

    The patch intentionally stores the complete new firmware as diff data instead
    of searching old-data covers. Optional TinyUZ compression uses HPatchLite's
    own _CompressPlugin_tuz path; generic zlib/gzip compression is deliberately
    not used for differential packages.
    """
    _normalize_bytes(old_fw, "old firmware")
    new_bytes = _normalize_bytes(new_fw, "new firmware")
    normalized_compress = str(patch_compress or "none").strip().lower()
    if normalized_compress not in HPATCHLITE_PATCH_COMPRESS_ALGOS:
        allowed = ", ".join(sorted(HPATCHLITE_PATCH_COMPRESS_ALGOS))
        raise PackageToolError(f"invalid HPatchLite patch compression '{patch_compress}'. Allowed: {allowed}")

    body = hpatchlite_create_body(new_bytes)
    new_size = _hpi_size_bytes(len(new_bytes))
    if normalized_compress == "tuz":
        patch_body = tuz_compress(body)
        uncompress_size = _hpi_size_bytes(len(body))
        code = (HPATCHLITE_VERSION_CODE << 6) | (len(uncompress_size) << 3) | len(new_size)
        return HPATCHLITE_MAGIC + bytes([HPATCHLITE_COMPRESS_TUZ, code]) + new_size + uncompress_size + patch_body

    code = (HPATCHLITE_VERSION_CODE << 6) | len(new_size)
    return HPATCHLITE_MAGIC + bytes([HPATCHLITE_COMPRESS_NONE, code]) + new_size + body


def hpatchlite_apply_patch(old_fw: bytes, patch: bytes) -> bytes:
    """Apply a no-compress or TinyUZ-compressed HPatchLite patch."""
    old_bytes = _normalize_bytes(old_fw, "old firmware")
    patch_bytes = _normalize_bytes(patch, "HPatchLite patch")
    if len(patch_bytes) < 4 or patch_bytes[:2] != HPATCHLITE_MAGIC:
        raise PackageToolError("unsupported hpatchlite body: missing hI magic")
    compress_type = patch_bytes[2]
    code = patch_bytes[3]
    if code >> 6 != HPATCHLITE_VERSION_CODE:
        raise PackageToolError("unsupported HPatchLite version code")
    new_size_len = code & 7
    uncompress_size_len = (code >> 3) & 7
    offset = 4
    if offset + new_size_len + uncompress_size_len > len(patch_bytes):
        raise PackageToolError("truncated HPatchLite header")
    new_size = int.from_bytes(patch_bytes[offset:offset + new_size_len], "little")
    offset += new_size_len
    uncompress_size = int.from_bytes(
        patch_bytes[offset:offset + uncompress_size_len], "little"
    )
    offset += uncompress_size_len

    patch_body = patch_bytes[offset:]
    if compress_type == HPATCHLITE_COMPRESS_NONE:
        if uncompress_size != 0:
            raise PackageToolError("HPatchLite no-compress header has an uncompress size")
    elif compress_type == HPATCHLITE_COMPRESS_TUZ:
        if uncompress_size == 0:
            raise PackageToolError("TinyUZ-compressed HPatchLite patch is missing uncompress size")
        patch_body = tuz_decompress(patch_body, uncompress_size)
    else:
        raise PackageToolError("unsupported HPatchLite patch compression type")

    cover_count, offset = _hpi_unpack_uint(patch_body, 0)
    output = bytearray()
    new_pos_back = 0
    old_pos_back = 0
    for cover_index in range(cover_count):
        cover_length, offset = _hpi_unpack_uint(patch_body, offset)
        if offset >= len(patch_body):
            raise PackageToolError("truncated HPatchLite cover tag")
        tag = patch_body[offset]
        offset += 1
        cover_old_pos, offset = _hpi_unpack_uint(
            patch_body, offset, tag & 31, bool(tag & (1 << 5))
        )
        is_not_need_sub_diff = bool(tag >> 7)
        if tag & (1 << 6):
            cover_old_pos = old_pos_back - cover_old_pos
        else:
            cover_old_pos += old_pos_back
        cover_new_pos, offset = _hpi_unpack_uint(patch_body, offset)
        cover_new_pos += new_pos_back

        if cover_new_pos < new_pos_back:
            raise PackageToolError("HPatchLite cover new position moved backwards")
        diff_size = cover_new_pos - new_pos_back
        if offset + diff_size > len(patch_body):
            raise PackageToolError("truncated HPatchLite diff data")
        output.extend(patch_body[offset:offset + diff_size])
        offset += diff_size

        if cover_length:
            if cover_old_pos < 0 or cover_old_pos + cover_length > len(old_bytes):
                raise PackageToolError("HPatchLite cover old range is out of range")
            old_slice = old_bytes[cover_old_pos:cover_old_pos + cover_length]
            if is_not_need_sub_diff:
                output.extend(old_slice)
            else:
                if offset + cover_length > len(patch_body):
                    raise PackageToolError("truncated HPatchLite sub-diff data")
                sub_diff = patch_body[offset:offset + cover_length]
                offset += cover_length
                output.extend(((a + b) & 0xFF) for a, b in zip(old_slice, sub_diff))
        elif cover_index != cover_count - 1:
            raise PackageToolError("zero-length HPatchLite cover is only valid at the end")

        new_pos_back = cover_new_pos + cover_length
        old_pos_back = cover_old_pos + cover_length

    if offset != len(patch_body):
        raise PackageToolError("trailing data after HPatchLite patch")
    if new_pos_back != new_size or len(output) != new_size:
        raise PackageToolError("HPatchLite restored size does not match header")
    return bytes(output)


def create_rbl_header(raw_fw: bytes, pkg_obj: bytes, algo: int, algo2: int,
                      timestamp: int, part_name: str, fw_ver: str,
                      prod_code: str) -> bytes:
    """Create a fixed-size RBL header compatible with the CLI packager."""
    header = b""
    header += struct.pack("4s", b"RBL\x00")
    header += struct.pack("<H", algo)
    header += struct.pack("<H", algo2)
    header += struct.pack("<I", timestamp)
    header += _pack_string(part_name, 16)
    header += _pack_string(fw_ver, 24)
    header += _pack_string(prod_code, 24)
    header += struct.pack("<I", crc32(pkg_obj))
    header += struct.pack("<I", crc32(raw_fw))
    header += struct.pack("<I", len(raw_fw))
    header += struct.pack("<I", len(pkg_obj))
    header += struct.pack("<I", crc32(header))
    return header


def parse_rbl_header(data: bytes) -> dict:
    """Parse and validate the fixed-size RBL header."""
    package = _normalize_bytes(data, "RBL package")
    if len(package) < QBOOT_HEADER_SIZE:
        raise PackageToolError("RBL package is smaller than the 96-byte header")
    values = struct.unpack("<4sHHI16s24s24sIIIII", package[:QBOOT_HEADER_SIZE])
    if values[0] != b"RBL\0":
        raise PackageToolError("invalid RBL magic")
    hdr_crc = crc32(package[:QBOOT_HEADER_SIZE - 4])
    if hdr_crc != values[11]:
        raise PackageToolError("invalid RBL header CRC")
    pkg_size = values[10]
    if len(package) - QBOOT_HEADER_SIZE != pkg_size:
        raise PackageToolError("RBL package body size does not match header")
    pkg_body = package[QBOOT_HEADER_SIZE:]
    if crc32(pkg_body) != values[7]:
        raise PackageToolError("RBL package body CRC does not match header")

    crypt_id = values[1] & 0x000F
    cmprs_id = values[1] & 0x1F00
    return {
        "magic": "RBL",
        "algo": values[1],
        "algo2": values[2],
        "timestamp": values[3],
        "part": _unpack_string(values[4]),
        "version": _unpack_string(values[5]),
        "product": _unpack_string(values[6]),
        "pkg_crc": values[7],
        "raw_crc": values[8],
        "raw_size": values[9],
        "pkg_size": pkg_size,
        "hdr_crc": values[11],
        "crypt": QBOOT_CRYPT_NAMES.get(crypt_id, f"unknown-{crypt_id}"),
        "cmprs": QBOOT_CMPRS_NAMES.get(cmprs_id, f"unknown-{cmprs_id >> 8}"),
        "algo2_name": QBOOT_ALGO2_NAMES.get(values[2], f"unknown-{values[2]}"),
    }


def _decrypt_package_body(pkg_body: bytes, crypt: str, aes_key: Optional[str],
                          aes_iv: Optional[str]) -> bytes:
    """Apply the inverse QBoot crypt stage for browser-side unpacking."""
    if crypt == "none":
        return pkg_body
    if crypt == "aes":
        return aes_cbc_decrypt(pkg_body, aes_key, aes_iv)
    raise PackageToolError(f"browser-side {crypt} decrypt is not implemented")


def _encrypt_package_body(pkg_body: bytes, crypt: str, aes_key: Optional[str],
                          aes_iv: Optional[str]) -> bytes:
    """Apply the QBoot crypt stage for browser-side packaging."""
    if crypt == "none":
        return pkg_body
    if crypt == "aes":
        return aes_cbc_encrypt(pkg_body, aes_key, aes_iv)
    raise PackageToolError(f"browser-side {crypt} encrypt is not implemented")


def _decompress_package_body(pkg_body: bytes, cmprs: str,
                             raw_size: Optional[int] = None) -> bytes:
    """Apply the inverse QBoot compression stage for browser-side unpacking."""
    if cmprs == "none":
        return pkg_body
    if cmprs == "gzip":
        return gzip_decompress(pkg_body)
    if cmprs == "fastlz":
        return fastlz_decompress(pkg_body, raw_size)
    if cmprs == "quicklz":
        return quicklz_decompress(pkg_body, raw_size)
    if cmprs == "hpatchlite":
        raise PackageToolError("hpatchlite restore requires old firmware")
    raise PackageToolError(f"browser-side {cmprs} decompress is not implemented")


def _compress_package_body(raw_fw: bytes, cmprs: str) -> bytes:
    """Apply the QBoot compression stage for browser-side packaging."""
    if cmprs == "none":
        return raw_fw
    if cmprs == "gzip":
        return gzip_compress(raw_fw)
    if cmprs == "fastlz":
        return fastlz_compress(raw_fw)
    if cmprs == "quicklz":
        return quicklz_compress(raw_fw)
    if cmprs == "hpatchlite":
        raise PackageToolError("hpatchlite packaging requires old firmware")
    raise PackageToolError(f"browser-side {cmprs} compress is not implemented")


def build_processed_body(raw_fw: bytes, crypt: str = "none", cmprs: str = "none",
                         aes_key: Optional[str] = None,
                         aes_iv: Optional[str] = None) -> bytes:
    """Build a package body by applying compression before encryption."""
    raw_bytes = _normalize_bytes(raw_fw, "raw firmware")
    parse_algo_strict("--crypt", crypt, QBOOT_CRYPT_ALGOS)
    parse_algo_strict("--cmprs", cmprs, QBOOT_CMPRS_ALGOS)
    compressed = _compress_package_body(raw_bytes, cmprs)
    return _encrypt_package_body(compressed, crypt, aes_key, aes_iv)


def unpack_processed_body(pkg_body: bytes, crypt: str = "none", cmprs: str = "none",
                          aes_key: Optional[str] = None,
                          aes_iv: Optional[str] = None) -> bytes:
    """Restore raw firmware by applying decryption before decompression."""
    body = _normalize_bytes(pkg_body, "package body")
    parse_algo_strict("--crypt", crypt, QBOOT_CRYPT_ALGOS)
    parse_algo_strict("--cmprs", cmprs, QBOOT_CMPRS_ALGOS)
    decrypted = _decrypt_package_body(body, crypt, aes_key, aes_iv)
    return _decompress_package_body(decrypted, cmprs)


def package_rbl_bytes(raw_fw: bytes, pkg_obj: bytes, crypt: str = "none",
                      cmprs: str = "none", algo2: str = "crc",
                      part: str = "app", version: str = "v1.00",
                      product: str = "00010203040506070809",
                      timestamp: Optional[int] = None) -> bytes:
    """Return RBL bytes with an already-prepared package body."""
    raw_bytes = _normalize_bytes(raw_fw, "raw firmware")
    pkg_bytes = _normalize_bytes(pkg_obj, "package body")

    crypt_algo = parse_algo_strict("--crypt", crypt, QBOOT_CRYPT_ALGOS)
    cmprs_algo = parse_algo_strict("--cmprs", cmprs, QBOOT_CMPRS_ALGOS)
    effective_algo2 = parse_algo_strict("--algo2", algo2, QBOOT_ALGO2_ALGOS)
    if cmprs_algo == QBOOT_ALGO_CMPRS_HPATCHLITE:
        effective_algo2 = QBOOT_ALGO2_VERIFY_NONE
    if timestamp is None:
        timestamp = int(time.time())

    header = create_rbl_header(
        raw_fw=raw_bytes,
        pkg_obj=pkg_bytes,
        algo=crypt_algo | cmprs_algo,
        algo2=effective_algo2,
        timestamp=timestamp,
        part_name=part,
        fw_ver=version,
        prod_code=product,
    )
    return header + pkg_bytes


def package_firmware_bytes(raw_fw: bytes, crypt: str = "none",
                           cmprs: str = "none", algo2: str = "crc",
                           part: str = "app", version: str = "v1.00",
                           product: str = "00010203040506070809",
                           aes_key: Optional[str] = None,
                           aes_iv: Optional[str] = None,
                           timestamp: Optional[int] = None) -> bytes:
    """Transform raw firmware, then wrap the processed body in an RBL package."""
    raw_bytes = _normalize_bytes(raw_fw, "raw firmware")
    pkg_body = build_processed_body(raw_bytes, crypt=crypt, cmprs=cmprs,
                                    aes_key=aes_key, aes_iv=aes_iv)
    return package_rbl_bytes(
        raw_fw=raw_bytes,
        pkg_obj=pkg_body,
        crypt=crypt,
        cmprs=cmprs,
        algo2=algo2,
        part=part,
        version=version,
        product=product,
        timestamp=timestamp,
    )



def package_hpatchlite_rbl_bytes(old_fw: bytes, new_fw: bytes, crypt: str = "none",
                                 algo2: str = "crc", part: str = "app",
                                 version: str = "v1.00",
                                 product: str = "00010203040506070809",
                                 aes_key: Optional[str] = None,
                                 aes_iv: Optional[str] = None,
                                 timestamp: Optional[int] = None,
                                 patch_compress: str = "none") -> bytes:
    """Build an RBL package from a native HPatchLite full-diff patch."""
    old_bytes = _normalize_bytes(old_fw, "old firmware")
    new_bytes = _normalize_bytes(new_fw, "new firmware")
    patch = hpatchlite_create_patch(old_bytes, new_bytes, patch_compress)
    pkg_body = _encrypt_package_body(patch, crypt, aes_key, aes_iv)
    return package_rbl_bytes(
        raw_fw=new_bytes,
        pkg_obj=pkg_body,
        crypt=crypt,
        cmprs="hpatchlite",
        algo2=algo2,
        part=part,
        version=version,
        product=product,
        timestamp=timestamp,
    )


def unpack_hpatchlite_rbl_bytes(old_fw: bytes, rbl_pkg: bytes,
                                aes_key: Optional[str] = None,
                                aes_iv: Optional[str] = None) -> tuple[bytes, dict]:
    """Restore firmware from a native HPatchLite no-compress or TinyUZ RBL package."""
    old_bytes = _normalize_bytes(old_fw, "old firmware")
    package = _normalize_bytes(rbl_pkg, "RBL package")
    header = parse_rbl_header(package)
    if header["cmprs"] != "hpatchlite":
        raise PackageToolError("RBL package is not marked as hpatchlite")
    pkg_body = package[QBOOT_HEADER_SIZE:]
    patch = _decrypt_package_body(pkg_body, header["crypt"], aes_key, aes_iv)
    raw_fw = hpatchlite_apply_patch(old_bytes, patch)
    if len(raw_fw) != header["raw_size"]:
        raise PackageToolError("restored firmware size does not match header")
    if crc32(raw_fw) != header["raw_crc"]:
        raise PackageToolError("restored firmware CRC does not match header")
    return raw_fw, header


def unpack_rbl_bytes(rbl_pkg: bytes, aes_key: Optional[str] = None,
                     aes_iv: Optional[str] = None) -> tuple[bytes, dict]:
    """Validate an RBL package and restore its raw firmware when supported."""
    package = _normalize_bytes(rbl_pkg, "RBL package")
    header = parse_rbl_header(package)
    pkg_body = package[QBOOT_HEADER_SIZE:]
    decrypted = _decrypt_package_body(pkg_body, header["crypt"], aes_key, aes_iv)
    raw_fw = _decompress_package_body(decrypted, header["cmprs"], header["raw_size"])
    if len(raw_fw) < header["raw_size"]:
        raise PackageToolError("restored firmware is smaller than raw_size")
    raw_checked = raw_fw[:header["raw_size"]]
    if crc32(raw_checked) != header["raw_crc"]:
        raise PackageToolError("restored firmware CRC does not match header")
    return raw_checked, header
