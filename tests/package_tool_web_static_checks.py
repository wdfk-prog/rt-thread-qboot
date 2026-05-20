#!/usr/bin/env python3
"""Static file and browser wiring checks for package_tool web CI."""

from pathlib import Path
import shutil
import subprocess

from package_tool_web_test_lib import assert_python_call_matches_signature, load_module


def check_static_files(repo_root: Path, web_dir: Path, web_core: Path) -> None:
    """Verify the static Pages tool files are present and wired together."""
    required = [
        web_dir / "index.html",
        web_dir / "app.js",
        web_dir / "style.css",
        web_core,
    ]
    for path in required:
        if not path.is_file():
            raise AssertionError(f"missing web file: {path}")

    index_text = (web_dir / "index.html").read_text(encoding="utf-8")
    app_text = (web_dir / "app.js").read_text(encoding="utf-8")
    style_text = (web_dir / "style.css").read_text(encoding="utf-8")

    assert "https://cdn.jsdelivr.net/pyodide/v0.29.3/full/pyodide.js" in index_text
    assert "package_tool_web.py" in app_text
    assert "package_firmware_bytes" in app_text
    assert "unpack_rbl_bytes" in app_text
    assert "operationPackAuto" in app_text
    assert "operationUnpack" in app_text
    assert "operationPatchPack" in app_text
    assert "operationPatchUnpack" in app_text
    assert 'data-mode-section="unpack patch-unpack"' in index_text
    assert 'data-file-trigger="raw-file"' in index_text
    assert 'data-file-trigger="pkg-file"' in index_text
    assert 'data-file-trigger="rbl-file"' in index_text
    assert 'data-file-trigger="old-file"' in index_text
    assert "data-aes-only" in index_text
    assert "hpatch-compress" in index_text
    assert "data-compress-field" in index_text
    assert '<option value="none">none</option>' in index_text
    assert '<option value="tuz">_CompressPlugin_tuz</option>' in index_text
    assert "patchCompress" in app_text
    assert 'isPatchPack ? "hpatchlite"' in app_text
    assert 'getInput("crypt").value = "none"' in app_text
    assert 'algo2: getInput("algo2").value' in app_text
    assert 'getInput("algo2").value = "none"' not in app_text
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


def check_javascript_entrypoint(web_dir: Path) -> None:
    """Verify the browser JavaScript entrypoint contains required hooks."""
    app_text = (web_dir / "app.js").read_text(encoding="utf-8")
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
    node_bin = shutil.which("node")
    if node_bin is None:
        raise AssertionError("node executable not found in PATH")
    try:
        subprocess.run(
            [node_bin, "--check"],
            input=app_text,
            text=True,
            check=True,
            timeout=30,
        )
    except subprocess.TimeoutExpired as exc:
        raise AssertionError("node --check timed out for package-tool app.js") from exc


def check_pyodide_calls_match_python_api(web_core: Path, web_dir: Path) -> None:
    """Verify app.js Pyodide calls match browser API signatures."""
    web = load_module("qboot_package_tool_web", web_core)
    app_text = (web_dir / "app.js").read_text(encoding="utf-8")

    call_specs = {
        "package_firmware_bytes": {"forbidden_keywords": ("patch_compress",)},
        "package_hpatchlite_rbl_bytes": {"required_keywords": ("patch_compress",)},
        "unpack_rbl_bytes": {"forbidden_keywords": ("patch_compress",)},
        "unpack_hpatchlite_rbl_bytes": {"forbidden_keywords": ("patch_compress",)},
    }
    for function_name, kwargs in call_specs.items():
        call_block = assert_python_call_matches_signature(web, app_text, function_name, **kwargs)
        assert "await pyodide.runPythonAsync" not in call_block

    hpatchlite_block = assert_python_call_matches_signature(
        web,
        app_text,
        "package_hpatchlite_rbl_bytes",
        required_keywords=("patch_compress",),
    )
    assert "patchCompress" in hpatchlite_block


def check_documentation_links(repo_root: Path) -> None:
    """Verify docs advertise automatic browser processing and limitations."""
    tools_en = (repo_root / "docs" / "en" / "tools.md").read_text(encoding="utf-8")
    tools_zh = (repo_root / "docs" / "zh" / "tools.md").read_text(encoding="utf-8")
    pages_workflow = (
        repo_root / ".github" / "workflows" / "pages-doxygen.yml"
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
