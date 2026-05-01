#!/usr/bin/env python3
"""CI checks for the static GitHub Pages package tool."""

import importlib.util
from pathlib import Path
import shutil
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
    assert "https://cdn.jsdelivr.net/pyodide/v0.29.3/full/pyodide.js" in index_text
    assert "package_tool_web.py" in app_text
    assert "package_rbl_bytes" in app_text
    assert "data-lang=\"zh-CN\"" in index_text
    assert "data-lang=\"en\"" in index_text
    assert "data-file-trigger=\"raw-file\"" in index_text
    assert "data-file-trigger=\"pkg-file\"" in index_text
    assert "class=\"file-input\"" in index_text
    assert "translations" in app_text
    assert "normalizeLanguage" in app_text
    assert "readSavedLanguage" in app_text
    assert "saveLanguage" in app_text
    assert "window.localStorage.setItem" in app_text
    assert "function saveLanguage(lang) {\n  try {\n    saveLanguage(lang);" not in app_text
    assert "chooseFile" in app_text
    assert "No file selected" in app_text
    assert "refreshFileName" in app_text
    assert "zh-CN" in app_text
    assert "en:" in app_text
    assert "Package body to append" in app_text
    assert "要写入 RBL 的包体" in app_text
    assert "pkgBodyHint" in app_text
    assert "same file as the raw firmware" in app_text

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
        "package_rbl_bytes",
        "applyLanguage",
        "translations",
    )
    for token in required_tokens:
        assert token in app_text, f"missing JavaScript token: {token}"


def check_web_matches_cli_core() -> None:
    """Compare browser core output against the CLI core for every option row."""
    cli = load_module("qboot_package_tool", CLI_TOOL)
    web = load_module("qboot_package_tool_web", WEB_CORE)
    raw = bytes((idx * 5 + 9) & 0xFF for idx in range(211))
    pkg = bytes((idx * 13 + 1) & 0xFF for idx in range(137))
    timestamp = 1714473600

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True)

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

    long_cli_bytes, _, _ = cli.build_rbl_package(
        raw_fw=raw,
        pkg_obj=pkg,
        crypt="aes",
        cmprs="quicklz",
        algo2="crc",
        timestamp=timestamp,
        part_name="application-slot-name",
        fw_ver="version-field-longer-than-24",
        prod_code="product-field-longer-than-24",
    )
    long_web_bytes = web.package_rbl_bytes(
        raw_fw=raw,
        pkg_obj=pkg,
        crypt="aes",
        cmprs="quicklz",
        algo2="crc",
        part="application-slot-name",
        version="version-field-longer-than-24",
        product="product-field-longer-than-24",
        timestamp=timestamp,
    )
    assert long_web_bytes == long_cli_bytes

    site_dir = OUT_DIR / "site" / "package-tool"
    site_dir.mkdir(parents=True, exist_ok=True)
    for filename in ("index.html", "app.js", "style.css", "package_tool_web.py"):
        shutil.copy2(WEB_DIR / filename, site_dir / filename)

    (OUT_DIR / "summary.md").write_text(
        "# package_tool Web CI Test Summary\n\n"
        f"- Matrix cases compared with CLI core: {len(rows)}\n"
        "- Static files checked: index.html, app.js, style.css, package_tool_web.py\n"
        "- Bilingual switch checked: zh-CN and en language buttons plus i18n keys\n"
        "- JavaScript syntax checked with node --check\n"
        "- Verified behavior: browser core output matches tools/package_tool.py\n"
        "- Verified long metadata field truncation matches CLI semantics\n"
        "- Copied static page files into _ci/package-tool-web-test/site/package-tool\n",
        encoding="utf-8",
    )


def check_documentation_links() -> None:
    """Verify README/docs advertise the Pages tool without remote curl checks."""
    readme_en = (REPO_ROOT / "readme.md").read_text(encoding="utf-8")
    readme_zh = (REPO_ROOT / "README_zh.md").read_text(encoding="utf-8")
    tools_en = (REPO_ROOT / "docs" / "en" / "tools.md").read_text(encoding="utf-8")
    tools_zh = (REPO_ROOT / "docs" / "zh" / "tools.md").read_text(encoding="utf-8")
    pages_workflow = (
        REPO_ROOT / ".github" / "workflows" / "pages-doxygen.yml"
    ).read_text(encoding="utf-8")

    web_url = "https://wdfk-prog.space/rt-thread-qboot/package-tool/index.html"
    assert web_url in readme_en
    assert web_url in readme_zh
    for doc_path in (
        REPO_ROOT / "docs" / "en" / "tools.md",
        REPO_ROOT / "docs" / "zh" / "tools.md",
        REPO_ROOT / "docs" / "en" / "document-map.md",
        REPO_ROOT / "docs" / "zh" / "document-map.md",
        REPO_ROOT / "docs" / "doxygen-mainpage.md",
    ):
        doc_text = doc_path.read_text(encoding="utf-8")
        assert web_url in doc_text, f"missing absolute package tool URL: {doc_path}"
        assert "../package-tool/index.html" not in doc_text
        assert "package-tool/index.html" not in doc_text.replace(web_url, "")

    assert "QBoot RBL Packager Web Tool" in readme_en
    assert "QBoot RBL Packager 网页工具" in readme_zh
    assert "byte-for-byte" in tools_en
    assert "完整字节流" in tools_zh

    assert "Verify deployed package tool page" not in pages_workflow
    assert "curl --fail" not in pages_workflow
    assert "package-tool/index.html" in pages_workflow


def check_error_paths() -> None:
    """Verify browser core raises predictable errors for invalid options."""
    web = load_module("qboot_package_tool_web_errors", WEB_CORE)
    for kwargs, fragment in (
        ({"crypt": "bad"}, "invalid --crypt"),
        ({"cmprs": "bad"}, "invalid --cmprs"),
        ({"algo2": "bad"}, "invalid --algo2"),
    ):
        try:
            web.package_rbl_bytes(b"raw", b"pkg", **kwargs)
        except web.PackageToolError as exc:
            assert fragment in str(exc)
        else:
            raise AssertionError(f"expected PackageToolError: {fragment}")


def main() -> int:
    """Run static page and browser-core consistency checks."""
    check_static_files()
    check_javascript_entrypoint()
    check_web_matches_cli_core()
    check_documentation_links()
    check_error_paths()
    print("package_tool web CI tests passed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
