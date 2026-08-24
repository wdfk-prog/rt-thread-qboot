#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/../../.." && pwd)
tool_path=${repo_root}/tools/package_tool.py
work_dir=$(mktemp -d)

cleanup()
{
    rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$tool_path" ]; then
    printf 'Package tool not found: %s\n' "$tool_path" >&2
    exit 1
fi

raw_file=${work_dir}/raw.bin
pkg_file=${work_dir}/package.bin
output_file=${work_dir}/output.rbl
log_file=${work_dir}/package-tool.log

printf 'qboot raw firmware smoke input\n' > "$raw_file"
printf 'qboot prepared package smoke input\n' > "$pkg_file"

PYTHONDONTWRITEBYTECODE=1 python3 "$tool_path" \
    --pkg "$pkg_file" \
    --raw "$raw_file" \
    --output "$output_file" \
    --crypt xor \
    --cmprs gzip \
    --algo2 crc \
    --part app \
    --version v1.00-ci \
    --product QBOOT-CI \
    > "$log_file"

python3 - "$raw_file" "$pkg_file" "$output_file" <<'PY'
import struct
import sys
import zlib

raw_path, pkg_path, output_path = sys.argv[1:]
header_format = "<4sHHI16s24s24sIIIII"
header_size = struct.calcsize(header_format)

with open(raw_path, "rb") as raw_stream:
    raw_data = raw_stream.read()
with open(pkg_path, "rb") as pkg_stream:
    pkg_data = pkg_stream.read()
with open(output_path, "rb") as output_stream:
    output_data = output_stream.read()

if len(output_data) != header_size + len(pkg_data):
    raise SystemExit("Unexpected RBL output size")

fields = struct.unpack(header_format, output_data[:header_size])
(
    magic,
    algo,
    algo2,
    timestamp,
    part_name,
    fw_version,
    product_code,
    pkg_crc,
    raw_crc,
    raw_size,
    pkg_size,
    header_crc,
) = fields

if magic != b"RBL\x00":
    raise SystemExit("Invalid RBL magic")
if algo != (1 | (1 << 8)):
    raise SystemExit("Unexpected algorithm flags")
if algo2 != 1:
    raise SystemExit("Unexpected verification algorithm")
if timestamp <= 0:
    raise SystemExit("Invalid package timestamp")
if part_name.split(b"\x00", 1)[0] != b"app":
    raise SystemExit("Unexpected partition name")
if fw_version.split(b"\x00", 1)[0] != b"v1.00-ci":
    raise SystemExit("Unexpected firmware version")
if product_code.split(b"\x00", 1)[0] != b"QBOOT-CI":
    raise SystemExit("Unexpected product code")
if pkg_crc != (zlib.crc32(pkg_data) & 0xFFFFFFFF):
    raise SystemExit("Package CRC mismatch")
if raw_crc != (zlib.crc32(raw_data) & 0xFFFFFFFF):
    raise SystemExit("Raw firmware CRC mismatch")
if raw_size != len(raw_data):
    raise SystemExit("Raw firmware size mismatch")
if pkg_size != len(pkg_data):
    raise SystemExit("Package size mismatch")
if header_crc != (zlib.crc32(output_data[:header_size - 4]) & 0xFFFFFFFF):
    raise SystemExit("Header CRC mismatch")
if output_data[header_size:] != pkg_data:
    raise SystemExit("Package payload mismatch")
PY

cat "$log_file"
printf 'PACKAGE_TOOL_SMOKE_PASS\n'
