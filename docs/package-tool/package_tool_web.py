"""Browser-compatible QBoot RBL packager core."""

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


def package_rbl_bytes(raw_fw: bytes, pkg_obj: bytes, crypt: str = "none",
                      cmprs: str = "none", algo2: str = "crc",
                      part: str = "app", version: str = "v1.00",
                      product: str = "00010203040506070809",
                      timestamp: Optional[int] = None) -> bytes:
    """Return RBL bytes for browser-side upload and download flow."""
    if not isinstance(raw_fw, (bytes, bytearray)):
        raise PackageToolError("raw firmware must be bytes")
    if not isinstance(pkg_obj, (bytes, bytearray)):
        raise PackageToolError("package body must be bytes")

    crypt_algo = parse_algo_strict("--crypt", crypt, QBOOT_CRYPT_ALGOS)
    cmprs_algo = parse_algo_strict("--cmprs", cmprs, QBOOT_CMPRS_ALGOS)
    effective_algo2 = parse_algo_strict("--algo2", algo2, QBOOT_ALGO2_ALGOS)
    if cmprs_algo == QBOOT_ALGO_CMPRS_HPATCHLITE:
        effective_algo2 = QBOOT_ALGO2_VERIFY_NONE
    if timestamp is None:
        timestamp = int(time.time())

    raw_bytes = bytes(raw_fw)
    pkg_bytes = bytes(pkg_obj)
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
