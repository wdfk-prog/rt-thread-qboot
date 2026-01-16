#!/usr/bin/env python3
"""
Pure RBL Packager (raw required)
"""

import os
import struct
import zlib
import sys
import argparse
import time

# ================== ALGO 定义 ==================

QBOOT_ALGO_CRYPT_NONE = 0
QBOOT_ALGO_CRYPT_XOR  = 1
QBOOT_ALGO_CRYPT_AES  = 2

QBOOT_ALGO_CMPRS_NONE       = (0 << 8)
QBOOT_ALGO_CMPRS_GZIP       = (1 << 8)
QBOOT_ALGO_CMPRS_QUICKLZ    = (2 << 8)
QBOOT_ALGO_CMPRS_FASTLZ     = (3 << 8)
QBOOT_ALGO_CMPRS_HPATCHLITE = (4 << 8)

QBOOT_ALGO2_VERIFY_CRC = 1

# ==================================================

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

def create_rbl_header(raw_fw: bytes, pkg_obj: bytes, algo: int, algo2: int,
                      timestamp: int, part_name: str, fw_ver: str, prod_code: str) -> bytes:
    """Create RBL header"""
    header = b""
    header += struct.pack("4s", b"RBL\x00")
    header += struct.pack("<H", algo)
    header += struct.pack("<H", algo2)
    header += struct.pack("<I", timestamp)

    header += struct.pack("16s", part_name.encode("utf-8"))
    header += struct.pack("24s", fw_ver.encode("utf-8"))
    header += struct.pack("24s", prod_code.encode("utf-8"))

    # raw_fw 必须有值
    header += struct.pack("<I", crc32(pkg_obj))        # pkg_crc
    header += struct.pack("<I", crc32(raw_fw))         # raw_crc
    header += struct.pack("<I", len(raw_fw))           # raw_size
    header += struct.pack("<I", len(pkg_obj))          # pkg_size

    hdr_crc = crc32(header)
    header += struct.pack("<I", hdr_crc)
    return header

def parse_algo(name: str, table: dict) -> int:
    if not name:
        return 0
    name = name.lower()
    return table.get(name, 0)

def package_rbl(args):
    # -------- read pkg --------
    with open(args.pkg, "rb") as f:
        pkg_obj = f.read()

    # -------- read raw fw (required) --------
    if not args.raw or not os.path.exists(args.raw):
        print("Error: --raw is required and must exist")
        sys.exit(1)
    with open(args.raw, "rb") as f:
        raw_fw = f.read()

    # -------- parse algo --------
    crypt_algo = parse_algo(args.crypt, {
        "none": QBOOT_ALGO_CRYPT_NONE,
        "xor":  QBOOT_ALGO_CRYPT_XOR,
        "aes":  QBOOT_ALGO_CRYPT_AES,
    })
    cmprs_algo = parse_algo(args.cmprs, {
        "none":       QBOOT_ALGO_CMPRS_NONE,
        "gzip":       QBOOT_ALGO_CMPRS_GZIP,
        "quicklz":    QBOOT_ALGO_CMPRS_QUICKLZ,
        "fastlz":     QBOOT_ALGO_CMPRS_FASTLZ,
        "hpatchlite": QBOOT_ALGO_CMPRS_HPATCHLITE,
    })
    algo = crypt_algo | cmprs_algo

    # -------- create header --------
    header = create_rbl_header(
        raw_fw=raw_fw,
        pkg_obj=pkg_obj,
        algo=algo,
        algo2=QBOOT_ALGO2_VERIFY_CRC,
        timestamp=int(time.time()),
        part_name=args.part,
        fw_ver=args.version,
        prod_code=args.product,
    )

    # -------- write output --------
    with open(args.output, "wb") as f:
        f.write(header)
        f.write(pkg_obj)

    print(f"RBL package created: {args.output} | pkg: {len(pkg_obj)} bytes | raw: {len(raw_fw)} bytes | algo=0x{algo:04X}")

def main():
    parser = argparse.ArgumentParser(description="RBL header packager (raw required)")
    parser.add_argument("--pkg", required=True, help="Prepared package body")
    parser.add_argument("--raw", required=True, help="Raw firmware (required)")
    parser.add_argument("-o", "--output", required=True, help="Output RBL file")
    parser.add_argument("--crypt", default="none", help="Crypt algo: none, xor, aes")
    parser.add_argument("--cmprs", default="none", help="Compress algo: none, gzip, quicklz, fastlz, hpatchlite")
    parser.add_argument("--part", default="app", help="Partition name")
    parser.add_argument("--version", default="v1.00", help="Firmware version")
    parser.add_argument("--product", default="00010203040506070809", help="Product code")
    args = parser.parse_args()
    package_rbl(args)

if __name__ == "__main__":
    main()
