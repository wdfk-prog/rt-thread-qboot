#!/usr/bin/env python3
"""CI checks for the static GitHub Pages package tool."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
CLI_TOOL = REPO_ROOT / "tools" / "package_tool.py"
WEB_DIR = REPO_ROOT / "docs" / "package-tool"
WEB_CORE = WEB_DIR / "package_tool_web.py"
OUT_DIR = REPO_ROOT / "_ci" / "package-tool-web-test"

CRYPT_ALGOS = ("none", "xor", "aes")
CMPRS_ALGOS = ("none", "gzip", "quicklz", "fastlz", "hpatchlite")
ALGO2_ALGOS = ("none", "crc")


def load_module(name: str, path: Path):
    """Load a Python source file as a module."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"failed to load module spec: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_static_files() -> None:
    """Verify the static Pages tool files are present and wired together."""
    required = [
        WEB_DIR / "index.html",
        WEB_DIR / "app.js",
        WEB_DIR / "style.css",
        WEB_CORE,
    ]
    for path in required:
        if not path.is_file():
            raise AssertionError(f"missing web file: {path}")

    index_text = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    app_text = (WEB_DIR / "app.js").read_text(encoding="utf-8")
    style_text = (WEB_DIR / "style.css").read_text(encoding="utf-8")

    assert "https://cdn.jsdelivr.net/pyodide/v0.29.3/full/pyodide.js" in index_text
    assert "package_tool_web.py" in app_text
    assert "package_firmware_bytes" in app_text
    assert "unpack_rbl_bytes" in app_text
    assert "operationPackAuto" in app_text
    assert "operationUnpack" in app_text
    assert "operationPatchPack" in app_text
    assert "operationPatchUnpack" in app_text
    assert "data-mode-section=\"unpack patch-unpack\"" in index_text
    assert "data-file-trigger=\"raw-file\"" in index_text
    assert "data-file-trigger=\"pkg-file\"" in index_text
    assert "data-file-trigger=\"rbl-file\"" in index_text
    assert "data-file-trigger=\"old-file\"" in index_text
    assert "data-aes-only" in index_text
    assert "hpatch-compress" in index_text
    assert "data-compress-field" in index_text
    assert '<option value="none">none</option>' in index_text
    assert '<option value="tuz">_CompressPlugin_tuz</option>' in index_text
    assert "patchCompress" in app_text
    assert 'isPatchPack ? "hpatchlite"' in app_text
    assert 'setSectionVisible(section, operation !== "patch-pack")' in app_text
    assert "patch_compress" in app_text
    assert "_CompressPlugin_tuz" in app_text
    assert 'crypt === "aes" || operation === "unpack" || operation === "patch-unpack"' in app_text
    assert "AES key" in app_text
    assert "gzip" in app_text
    assert "[hidden]" in style_text

    keys = set()
    for part in index_text.split('data-i18n="')[1:]:
        keys.add(part.split('"', 1)[0])
    for key in keys:
        assert f"{key}:" in app_text, f"missing i18n key in app.js: {key}"


def check_javascript_entrypoint() -> None:
    """Verify the browser JavaScript entrypoint contains required hooks."""
    app_text = (WEB_DIR / "app.js").read_text(encoding="utf-8")
    required_tokens = (
        "async function loadPackager",
        "async function buildPackage",
        "async function processPackage",
        "async function processUnpack",
        "updateFormState",
        "package_firmware_bytes",
        "unpack_rbl_bytes",
        "applyLanguage",
        "translations",
    )
    for token in required_tokens:
        assert token in app_text, f"missing JavaScript token: {token}"
    subprocess.run(["node", "--check"], input=app_text, text=True, check=True)


def check_web_matches_cli_manual_core() -> None:
    """Compare manual browser core output against the CLI core for every option row."""
    cli = load_module("qboot_package_tool", CLI_TOOL)
    web = load_module("qboot_package_tool_web", WEB_CORE)
    raw = bytes((idx * 5 + 9) & 0xFF for idx in range(211))
    pkg = bytes((idx * 13 + 1) & 0xFF for idx in range(137))
    timestamp = 1714473600
    rows = []

    for crypt in CRYPT_ALGOS:
        for cmprs in CMPRS_ALGOS:
            for algo2 in ALGO2_ALGOS:
                cli_bytes, cli_algo, cli_algo2 = cli.build_rbl_package(
                    raw_fw=raw,
                    pkg_obj=pkg,
                    crypt=crypt,
                    cmprs=cmprs,
                    algo2=algo2,
                    timestamp=timestamp,
                    part_name="webapp",
                    fw_ver="v2.03",
                    prod_code="web-product",
                )
                web_bytes = web.package_rbl_bytes(
                    raw_fw=raw,
                    pkg_obj=pkg,
                    crypt=crypt,
                    cmprs=cmprs,
                    algo2=algo2,
                    part="webapp",
                    version="v2.03",
                    product="web-product",
                    timestamp=timestamp,
                )
                assert web_bytes == cli_bytes
                rows.append((crypt, cmprs, algo2, cli_algo, cli_algo2))

    assert len(rows) == len(CRYPT_ALGOS) * len(CMPRS_ALGOS) * len(ALGO2_ALGOS)


def check_header_parser_and_validation() -> None:
    """Verify RBL parse and CRC checks detect valid and corrupted packages."""
    web = load_module("qboot_package_tool_web", WEB_CORE)
    raw = b"firmware" * 16
    rbl = web.package_rbl_bytes(raw, raw, timestamp=1714473600)
    header = web.parse_rbl_header(rbl)

    assert header["magic"] == "RBL"
    assert header["crypt"] == "none"
    assert header["cmprs"] == "none"
    assert header["algo2_name"] == "crc"
    assert header["raw_size"] == len(raw)
    assert header["pkg_size"] == len(raw)

    corrupted = bytearray(rbl)
    corrupted[-1] ^= 0x55
    try:
        web.parse_rbl_header(bytes(corrupted))
    except web.PackageToolError as exc:
        assert "body CRC" in str(exc)
    else:
        raise AssertionError("expected corrupted body CRC to fail")


def check_gzip_pack_unpack_roundtrip() -> None:
    """Verify browser-side gzip packaging and firmware restore."""
    web = load_module("qboot_package_tool_web", WEB_CORE)
    raw = bytes((idx * 17 + 3) & 0xFF for idx in range(1024))
    rbl = web.package_firmware_bytes(
        raw,
        crypt="none",
        cmprs="gzip",
        algo2="crc",
        part="app",
        version="v3.01",
        product="web-gzip",
        timestamp=1714473600,
    )
    header = web.parse_rbl_header(rbl)
    restored, unpacked_header = web.unpack_rbl_bytes(rbl)

    assert header["cmprs"] == "gzip"
    assert header["pkg_size"] < len(raw)
    assert restored == raw
    assert unpacked_header["raw_crc"] == web.crc32(raw)


def check_aes_vectors_and_roundtrip() -> None:
    """Verify pure-Python AES implementation and AES RBL roundtrip."""
    web = load_module("qboot_package_tool_web", WEB_CORE)
    key128 = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
    key256 = bytes.fromhex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
    )
    pt = bytes.fromhex("00112233445566778899aabbccddeeff")

    assert web.aes_ecb_encrypt_block(pt, key128).hex() == "69c4e0d86a7b0430d8cdb78070b4c55a"
    assert web.aes_ecb_encrypt_block(pt, key256).hex() == "8ea2b7ca516745bfeafc49904b496089"
    assert web.aes_ecb_decrypt_block(bytes.fromhex("8ea2b7ca516745bfeafc49904b496089"), key256) == pt

    raw = bytes((idx * 23 + 7) & 0xFF for idx in range(64))
    rbl = web.package_firmware_bytes(
        raw,
        crypt="aes",
        cmprs="none",
        algo2="crc",
        timestamp=1714473600,
    )
    header = web.parse_rbl_header(rbl)
    restored, _ = web.unpack_rbl_bytes(rbl)

    assert header["crypt"] == "aes"
    assert header["cmprs"] == "none"
    assert header["pkg_size"] == len(raw)
    assert restored == raw


def check_fastlz_quicklz_and_diff_roundtrip() -> None:
    """Verify FastLZ, QuickLZ, TinyUZ, and differential browser roundtrips."""
    web = load_module("qboot_package_tool_web", WEB_CORE)
    raw = bytes((idx * 29 + 11) & 0xFF for idx in range(1024))

    for size in (0, 1, 14, 15, 128, 4096):
        payload = bytes((idx * 31 + 19) & 0xFF for idx in range(size))
        assert web.tuz_decompress(web.tuz_compress(payload), size) == payload

    for cmprs in ("fastlz", "quicklz"):
        rbl = web.package_firmware_bytes(raw, crypt="none", cmprs=cmprs,
                                         timestamp=1714473600)
        restored, header = web.unpack_rbl_bytes(rbl)
        assert restored == raw
        assert header["cmprs"] == cmprs
        assert header["pkg_size"] > 4

    old = bytearray(bytes((idx * 17 + 5) & 0xFF for idx in range(768)))
    new_fw = bytearray(old)
    new_fw[32:48] = b"QBOOT-WEB-DIFF!!"
    new_fw[300:306] = b"PATCH!"
    new_fw.extend(b"tail")
    rbl = web.package_hpatchlite_rbl_bytes(bytes(old), bytes(new_fw),
                                           crypt="none",
                                           timestamp=1714473600,
                                           patch_compress="tuz")
    restored, header = web.unpack_hpatchlite_rbl_bytes(bytes(old), rbl)
    assert restored == bytes(new_fw)
    assert header["cmprs"] == "hpatchlite"
    assert header["algo2_name"] == "none"
    assert rbl[web.QBOOT_HEADER_SIZE:web.QBOOT_HEADER_SIZE + 3] == b"hI" + bytes([web.HPATCHLITE_COMPRESS_TUZ])

    rbl_none = web.package_hpatchlite_rbl_bytes(bytes(old), bytes(new_fw),
                                                crypt="none",
                                                timestamp=1714473600,
                                                patch_compress="none")
    restored_none, _ = web.unpack_hpatchlite_rbl_bytes(bytes(old), rbl_none)
    assert restored_none == bytes(new_fw)
    assert rbl_none[web.QBOOT_HEADER_SIZE:web.QBOOT_HEADER_SIZE + 3] == b"hI" + bytes([web.HPATCHLITE_COMPRESS_NONE])

    try:
        web.package_firmware_bytes(raw, crypt="none", cmprs="hpatchlite")
    except web.PackageToolError as exc:
        assert "requires old firmware" in str(exc)
    else:
        raise AssertionError("expected hpatchlite one-input packaging to fail")

    try:
        web.package_firmware_bytes(raw[:-1], crypt="aes", cmprs="none")
    except web.PackageToolError as exc:
        assert "multiple of 16" in str(exc)
    else:
        raise AssertionError("expected AES unaligned input to fail")


def check_documentation_links() -> None:
    """Verify docs advertise automatic browser processing and limitations."""
    tools_en = (REPO_ROOT / "docs" / "en" / "tools.md").read_text(encoding="utf-8")
    tools_zh = (REPO_ROOT / "docs" / "zh" / "tools.md").read_text(encoding="utf-8")
    pages_workflow = (
        REPO_ROOT / ".github" / "workflows" / "pages-doxygen.yml"
    ).read_text(encoding="utf-8")

    for text in (tools_en, tools_zh):
        assert "gzip" in text
        assert "AES" in text or "aes" in text
        assert "quicklz" in text
        assert "fastlz" in text
        assert "hpatchlite" in text
        assert "_CompressPlugin_tuz" in text
    assert "package-tool" in pages_workflow
    assert "package_tool_web.py" in pages_workflow


def write_summary() -> None:
    """Write a compact human-readable summary artifact."""
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    site_dir = OUT_DIR / "site" / "package-tool"
    site_dir.mkdir(parents=True, exist_ok=True)
    for filename in ("index.html", "app.js", "style.css", "package_tool_web.py"):
        shutil.copy2(WEB_DIR / filename, site_dir / filename)

    summary = {
        "manual_matrix_cases": len(CRYPT_ALGOS) * len(CMPRS_ALGOS) * len(ALGO2_ALGOS),
        "real_browser_transforms": ["none", "gzip", "fastlz", "quicklz", "aes", "hpatchlite"],
        "browser_diff_format": "HPatchLite full-diff with _CompressPlugin_tuz",
        "header_size": 96,
        "javascript_checked_with": "node --check",
    }
    (OUT_DIR / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    (OUT_DIR / "summary.md").write_text(
        "# package_tool Web CI Test Summary\n\n"
        f"- Manual wrapper matrix cases compared with CLI core: {summary['manual_matrix_cases']}\n"
        "- Static files checked: index.html, app.js, style.css, package_tool_web.py\n"
        "- Browser-side real transforms checked: none, gzip, fastlz, quicklz, aes, hpatchlite\n"
        "- Firmware restore checked from generated RBL packages\n"
        "- RBL header and body CRC validation checked\n"
        "- JavaScript syntax checked with node --check\n"
        "- Copied static page files into _ci/package-tool-web-test/site/package-tool\n",
        encoding="utf-8",
    )


def main() -> int:
    """Run all web package-tool checks."""
    check_static_files()
    check_javascript_entrypoint()
    check_web_matches_cli_manual_core()
    check_header_parser_and_validation()
    check_gzip_pack_unpack_roundtrip()
    check_aes_vectors_and_roundtrip()
    check_fastlz_quicklz_and_diff_roundtrip()
    check_documentation_links()
    write_summary()
    print("package_tool web CI tests passed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
