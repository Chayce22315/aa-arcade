#!/usr/bin/env python3
"""Check a spindle settings.ini against the game's own schema (stdlib only).

The schema is not duplicated here: the reference comes from the executable itself
(`spindle --write-default-settings FILE`), whose comments carry every key's valid range.

    python tools/validate_settings.py --exe path/to/spindle[.exe] [settings.ini]

Without a settings file it validates the generated defaults (a self-consistency check used by CI).
Exit code 0 = no problems, 1 = problems found, 2 = could not run.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

VALID_RE = re.compile(r"^;\s*(?P<key>[A-Za-z0-9_]+):.*\(valid: (?P<valid>[^)]*)\)\s*$|^;\s*(?P<key2>[A-Za-z0-9_]+): valid: (?P<valid2>.*)$")


def parse_ini(text: str) -> tuple[dict[tuple[str, str], str], list[str]]:
    values: dict[tuple[str, str], str] = {}
    problems: list[str] = []
    section = ""
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip().lstrip("﻿")
        if not line or line[0] in ";#":
            continue
        if line.startswith("["):
            if not line.endswith("]"):
                problems.append(f"line {n}: malformed section header: {line}")
                continue
            section = line[1:-1].strip()
            continue
        if "=" not in line:
            problems.append(f"line {n}: expected key = value: {line}")
            continue
        key, value = (p.strip() for p in line.split("=", 1))
        if (section.lower(), key.lower()) in values:
            problems.append(f"line {n}: duplicate key {key} in [{section}] (the last one wins)")
        values[(section.lower(), key.lower())] = value
    return values, problems


def parse_schema(text: str) -> dict[tuple[str, str], str]:
    schema: dict[tuple[str, str], str] = {}
    section = ""
    pending: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line[1:-1]
            continue
        m = VALID_RE.match(line)
        if m:
            key = m.group("key") or m.group("key2")
            pending[key] = (m.group("valid") or m.group("valid2")).strip()
            continue
        if "=" in line and not line.startswith(";"):
            key = line.split("=", 1)[0].strip()
            schema[(section.lower(), key.lower())] = pending.pop(key, "")
    return schema


def check_value(valid: str, value: str) -> str | None:
    if not valid:
        return None if value or True else "empty"
    if valid == "0 | 1":
        return None if value.lower() in {"0", "1", "true", "false", "yes", "no", "on", "off"} else "expected 0 or 1"
    if ".." in valid and "|" not in valid:
        lo, hi = (float(x) for x in valid.split(".."))
        try:
            v = float(value)
        except ValueError:
            return f"expected a number {valid}"
        if not (lo <= v <= hi):
            return f"out of range {valid}"
        if "." not in valid and "." in value:
            return f"expected a whole number {valid}"
        return None
    options = [o.strip() for o in valid.split("|")]
    return None if value.lower() in options else f"expected one of {valid}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("settings", nargs="?", type=Path)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        ref = Path(tmp) / "defaults.ini"
        try:
            subprocess.run([str(args.exe), "--write-default-settings", str(ref)], check=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as exc:
            print(f"cannot run {args.exe}: {exc}")
            return 2
        reference = ref.read_text()

    schema = parse_schema(reference)
    target_text = args.settings.read_text() if args.settings else reference
    values, problems = parse_ini(target_text)
    for (section, key), value in sorted(values.items()):
        if (section, key) not in schema:
            problems.append(f"[{section}] {key}: unknown key (ignored by the game)")
            continue
        err = check_value(schema[(section, key)], value)
        if err:
            problems.append(f"[{section}] {key} = {value!r}: {err}; the game will use the default")
    missing = [k for k in schema if k not in values]
    for section, key in missing:
        print(f"note: [{section}] {key} missing (default used)")
    for p in problems:
        print(f"problem: {p}")
    print(f"{len(schema)} known keys, {len(values)} set, {len(missing)} missing, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
