#!/usr/bin/env python3
"""Shared helpers for package_tool web CI tests."""

import ast
import importlib.util
import inspect
from pathlib import Path
import struct


def raw_metadata_fields(data: bytes) -> dict:
    """Return raw fixed-size metadata fields without stripping NUL bytes."""
    values = struct.unpack("<4sHHI16s24s24sIIIII", data[:96])
    return {
        "part": values[4],
        "version": values[5],
        "product": values[6],
    }


def is_unsupported_hpatch_crypto(crypt: str, cmprs: str) -> bool:
    """Return whether the firmware rejects this HPatchLite crypt pairing."""
    return cmprs == "hpatchlite" and crypt != "none"


def load_module(name: str, path: Path):
    """Load a Python source file as a module."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"failed to load module spec: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_python_call_block(text: str, call_name: str) -> str:
    """Return the first embedded Python call expression from app.js."""
    marker = f"{call_name}("
    start = text.find(marker)
    if start < 0:
        raise AssertionError(f"missing embedded Python call: {call_name}")

    depth = 0
    for offset, char in enumerate(text[start:], start=start):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[start:offset + 1]

    raise AssertionError(f"unterminated embedded Python call: {call_name}")


def extract_python_call_expr(text: str, call_name: str) -> tuple[str, ast.Call]:
    """Return an embedded Python call block and its parsed AST call node."""
    call_block = extract_python_call_block(text, call_name)
    parsed = ast.parse(call_block, mode="eval")
    if not isinstance(parsed.body, ast.Call):
        raise AssertionError(f"embedded Python block is not a call: {call_name}")
    if not isinstance(parsed.body.func, ast.Name) or parsed.body.func.id != call_name:
        raise AssertionError(f"embedded Python call target mismatch: {call_name}")
    return call_block, parsed.body


def assert_python_call_matches_signature(module, text: str, function_name: str,
                                         required_keywords: tuple[str, ...] = (),
                                         forbidden_keywords: tuple[str, ...] = ()) -> str:
    """Verify an embedded Pyodide call remains compatible with its Python API."""
    call_block, call_expr = extract_python_call_expr(text, function_name)
    signature = inspect.signature(getattr(module, function_name))
    args = [object() for _arg in call_expr.args]
    kwargs = {}

    for keyword in call_expr.keywords:
        if keyword.arg is None:
            raise AssertionError(f"{function_name}: **kwargs is not allowed here")
        if keyword.arg in kwargs:
            raise AssertionError(f"{function_name}: duplicate keyword argument {keyword.arg}")
        kwargs[keyword.arg] = object()

    try:
        signature.bind(*args, **kwargs)
    except TypeError as exc:
        raise AssertionError(f"{function_name}: signature mismatch: {exc}") from exc

    keyword_names = set(kwargs)
    for name in required_keywords:
        assert name in keyword_names, f"{function_name}: missing keyword {name}"
    for name in forbidden_keywords:
        assert name not in keyword_names, f"{function_name}: unexpected keyword {name}"

    return call_block
