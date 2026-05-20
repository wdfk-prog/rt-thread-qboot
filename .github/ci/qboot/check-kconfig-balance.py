#!/usr/bin/env python3
"""Check Kconfig block balance for package CI."""

from pathlib import Path

stack = []
pairs = {
    "if": "endif",
    "menu": "endmenu",
    "choice": "endchoice",
}
closers = {value: key for key, value in pairs.items()}

for lineno, raw in enumerate(Path("Kconfig").read_text(encoding="utf-8").splitlines(), 1):
    line = raw.strip()
    if not line or line.startswith("#"):
        continue
    keyword = line.split()[0]
    if keyword in pairs:
        stack.append((keyword, lineno))
    elif keyword in closers:
        if not stack:
            raise SystemExit(f"Kconfig:{lineno}: unmatched {keyword}")
        opener, opener_lineno = stack.pop()
        if opener != closers[keyword]:
            raise SystemExit(
                f"Kconfig:{lineno}: {keyword} closes {opener} from line {opener_lineno}"
            )

if stack:
    opener, opener_lineno = stack[-1]
    raise SystemExit(f"Kconfig:{opener_lineno}: unclosed {opener}")
