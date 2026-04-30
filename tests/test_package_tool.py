#!/usr/bin/env python3
"""CI regression tests for tools/package_tool.py."""

import contextlib
import csv
import importlib.util
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from types import SimpleNamespace
import zlib


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL = REPO_ROOT / "tools" / "package_tool.py"
OUT_DIR = Path(
    os.environ.get("QBOOT_PACKAGE_TOOL_TEST_OUT", REPO_ROOT / "_ci" / "package-tool-test")
)
HEADER_SIZE = 96

CRYPT_ALGOS = {
    "none": 0x0000,
    "xor": 0x0001,
    "aes": 0x0002,
}

CMPRS_ALGOS = {
    "none": 0x0000,
    "gzip": 0x0100,
    "quicklz": 0x0200,
    "fastlz": 0x0300,
    "hpatchlite": 0x0400,
}

ALGO2_ALGOS = {
    "none": 0x0000,
    "crc": 0x0001,
}


def crc32(data: bytes) -> int:
    """Return the unsigned CRC32 value used by the RBL header."""
    return zlib.crc32(data) & 0xFFFFFFFF


def load_package_tool():
    """Load package_tool.py as a module without requiring tools/__init__.py."""
    spec = importlib.util.spec_from_file_location("qboot_package_tool", TOOL)
    if spec is None or spec.loader is None:
        raise AssertionError(f"failed to load module spec: {TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_help() -> subprocess.CompletedProcess:
    """Run the package_tool.py help path through the real CLI."""
    return subprocess.run(
        [sys.executable, "-S", str(TOOL), "--help"],
        cwd=str(REPO_ROOT),
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )


def parse_header(data: bytes) -> dict:
    """Parse the fixed-size RBL header from an output package."""
    if len(data) < HEADER_SIZE:
        raise AssertionError(f"output too small: {len(data)} bytes")

    values = struct.unpack("<4sHHI16s24s24sIIIII", data[:HEADER_SIZE])
    return {
        "magic": values[0],
        "algo": values[1],
        "algo2": values[2],
        "timestamp": values[3],
        "part": values[4].rstrip(b"\0"),
        "version": values[5].rstrip(b"\0"),
        "product": values[6].rstrip(b"\0"),
        "pkg_crc": values[7],
        "raw_crc": values[8],
        "raw_size": values[9],
        "pkg_size": values[10],
        "hdr_crc": values[11],
    }


def make_args(pkg_path: Path, raw_path: Path, output: Path, crypt: str = "none",
              cmprs: str = "none", algo2: str = "crc") -> SimpleNamespace:
    """Build a namespace matching the argparse result used by package_rbl()."""
    return SimpleNamespace(
        pkg=str(pkg_path),
        raw=str(raw_path),
        output=str(output),
        crypt=crypt,
        cmprs=cmprs,
        algo2=algo2,
        part="app",
        version="v9.99",
        product="ci-product",
    )


def run_package(module, args: SimpleNamespace) -> str:
    """Run package_rbl() in-process while capturing its status line."""
    stream = io.StringIO()
    with contextlib.redirect_stdout(stream):
        module.package_rbl(args)
    return stream.getvalue()


def assert_valid_package(output: Path, raw: bytes, pkg: bytes, crypt: str,
                         cmprs: str, algo2: str, before: int,
                         after: int) -> None:
    """Verify header fields, CRCs, sizes, and payload bytes."""
    data = output.read_bytes()
    header = parse_header(data)
    expected_algo = CRYPT_ALGOS[crypt] | CMPRS_ALGOS[cmprs]
    expected_algo2 = 0 if cmprs == "hpatchlite" else ALGO2_ALGOS[algo2]

    assert header["magic"] == b"RBL\0"
    assert header["algo"] == expected_algo
    assert header["algo2"] == expected_algo2
    assert before <= header["timestamp"] <= after
    assert header["part"] == b"app"
    assert header["version"] == b"v9.99"
    assert header["product"] == b"ci-product"
    assert header["pkg_crc"] == crc32(pkg)
    assert header["raw_crc"] == crc32(raw)
    assert header["raw_size"] == len(raw)
    assert header["pkg_size"] == len(pkg)
    assert header["hdr_crc"] == crc32(data[:HEADER_SIZE - 4])
    assert data[HEADER_SIZE:] == pkg


def write_inputs() -> tuple[Path, Path, bytes, bytes]:
    """Create deterministic raw and package-body inputs for CI."""
    inputs = OUT_DIR / "inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    raw = bytes((idx * 7 + 3) & 0xFF for idx in range(257))
    pkg = bytes((idx * 11 + 5) & 0xFF for idx in range(149))
    raw_path = inputs / "firmware.raw"
    pkg_path = inputs / "package-body.bin"
    raw_path.write_bytes(raw)
    pkg_path.write_bytes(pkg)
    return raw_path, pkg_path, raw, pkg


def check_help() -> None:
    """Verify the CLI help path and advertised options."""
    result = run_help()
    help_text = result.stdout
    for token in (
        "RBL header packager",
        "--pkg",
        "--raw",
        "--crypt",
        "--cmprs",
        "--algo2",
        "none, gzip, quicklz",
        "none, xor, aes",
    ):
        assert token in help_text, f"missing help token: {token}"


def check_default_package(module, raw_path: Path, pkg_path: Path,
                          raw: bytes, pkg: bytes) -> None:
    """Verify default algorithm selection without explicit algo flags."""
    output = OUT_DIR / "outputs" / "default.rbl"
    args = make_args(pkg_path, raw_path, output)
    before = int(time.time())
    status = run_package(module, args)
    after = int(time.time())
    assert "algo=0x0000" in status
    assert "algo2=0x0001" in status
    assert_valid_package(output, raw, pkg, "none", "none", "crc", before, after)


def check_algorithm_matrix(module, raw_path: Path, pkg_path: Path,
                           raw: bytes, pkg: bytes) -> None:
    """Cover all supported crypt, compression, and algo2 combinations."""
    outputs = OUT_DIR / "outputs"
    outputs.mkdir(parents=True, exist_ok=True)
    csv_path = OUT_DIR / "package_tool_matrix.csv"

    with csv_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["crypt", "cmprs", "algo2", "effective_algo", "effective_algo2", "output"])

        for crypt, crypt_value in CRYPT_ALGOS.items():
            for cmprs, cmprs_value in CMPRS_ALGOS.items():
                for algo2, algo2_value in ALGO2_ALGOS.items():
                    output = outputs / f"crypt-{crypt}_cmprs-{cmprs}_algo2-{algo2}.rbl"
                    args = make_args(pkg_path, raw_path, output, crypt, cmprs, algo2)
                    before = int(time.time())
                    status = run_package(module, args)
                    after = int(time.time())
                    assert "RBL package created" in status
                    assert_valid_package(output, raw, pkg, crypt, cmprs, algo2, before, after)
                    writer.writerow([
                        crypt,
                        cmprs,
                        algo2,
                        f"0x{(crypt_value | cmprs_value):04X}",
                        f"0x{(0 if cmprs == 'hpatchlite' else algo2_value):04X}",
                        output.relative_to(OUT_DIR),
                    ])


def expect_system_exit(code: int, func, *args) -> str:
    """Run a failure path and verify the expected SystemExit code."""
    stream = io.StringIO()
    try:
        with contextlib.redirect_stdout(stream):
            func(*args)
    except SystemExit as exc:
        assert exc.code == code, f"expected exit {code}, got {exc.code}"
        return stream.getvalue()
    raise AssertionError(f"expected SystemExit({code})")


def check_error_paths(module, raw_path: Path, pkg_path: Path) -> None:
    """Verify intentional failure paths return non-zero status."""
    cases = [
        ("invalid-crypt", "bad", "none", "crc", "invalid --crypt", 2),
        ("invalid-cmprs", "none", "bad", "crc", "invalid --cmprs", 2),
        ("invalid-algo2", "none", "none", "bad", "invalid --algo2", 2),
    ]

    for name, crypt, cmprs, algo2, message, code in cases:
        output = OUT_DIR / "outputs" / f"{name}.rbl"
        text = expect_system_exit(
            code,
            module.package_rbl,
            make_args(pkg_path, raw_path, output, crypt, cmprs, algo2),
        )
        assert message in text, f"{name}: missing error text"
        assert not output.exists(), f"{name}: output should not exist"

    output = OUT_DIR / "outputs" / "missing-raw.rbl"
    text = expect_system_exit(
        1,
        module.package_rbl,
        make_args(pkg_path, OUT_DIR / "inputs" / "missing.raw", output),
    )
    assert "--raw is required and must exist" in text
    assert not output.exists(), "missing-raw: output should not exist"


def write_summary() -> None:
    """Write a compact human-readable summary artifact."""
    with (OUT_DIR / "package_tool_matrix.csv").open(encoding="utf-8") as csv_file:
        matrix_rows = list(csv.DictReader(csv_file))
    requested_examples = [
        row for row in matrix_rows
        if (row["crypt"], row["cmprs"], row["algo2"]) in {
            ("none", "none", "crc"),
            ("none", "gzip", "crc"),
            ("none", "quicklz", "crc"),
            ("aes", "none", "crc"),
        }
    ]
    summary = OUT_DIR / "package_tool_test_summary.md"
    summary.write_text(
        "# package_tool CI Test Summary\n\n"
        f"- Matrix cases: {len(matrix_rows)}\n"
        "- Covered crypt values: none, xor, aes\n"
        "- Covered compression values: none, gzip, quicklz, fastlz, hpatchlite\n"
        "- Covered algo2 values: none, crc\n"
        "- Verified fields: magic, algo, algo2, timestamp, part, version, product, "
        "pkg/raw CRC, pkg/raw size, header CRC, and output payload bytes\n"
        "- Verified error paths: invalid --crypt, invalid --cmprs, "
        "invalid --algo2, missing --raw\n\n"
        "## Requested smoke cases\n\n"
        + "\n".join(
            f"- crypt={row['crypt']}, cmprs={row['cmprs']}, algo2={row['algo2']} -> {row['output']}"
            for row in requested_examples
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> int:
    """Run package_tool CLI, matrix, artifact, and error-path checks."""
    if not TOOL.is_file():
        raise SystemExit(f"missing tool: {TOOL}")

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    (OUT_DIR / "outputs").mkdir(parents=True, exist_ok=True)

    module = load_package_tool()
    raw_path, pkg_path, raw, pkg = write_inputs()
    check_help()
    check_default_package(module, raw_path, pkg_path, raw, pkg)
    check_algorithm_matrix(module, raw_path, pkg_path, raw, pkg)
    check_error_paths(module, raw_path, pkg_path)
    write_summary()

    print("package_tool CI tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
