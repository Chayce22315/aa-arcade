#!/usr/bin/env python3
"""Runs spindle's four-station self-test in an iOS simulator and collects the results.

    python3 ci/ios_selftest.py path/to/spindle.app OUTDIR

Boots the newest available iPhone simulator, installs the app, launches it with --selftest, waits for it
to exit, then copies the report, log and screenshots out of the app's data container. Exits non-zero
unless the report says PASS.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

BUNDLE = "io.itch.chayce22315.spindle"


def run(*args: str, check: bool = True, timeout: float | None = None) -> subprocess.CompletedProcess:
    print("+", " ".join(args), flush=True)
    return subprocess.run(args, check=check, text=True, capture_output=True, timeout=timeout)


def runtime_version(runtime: str) -> tuple[int, ...]:
    m = re.search(r"iOS-(\d+)-(\d+)", runtime)
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def main() -> int:
    app, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    devices = json.loads(run("xcrun", "simctl", "list", "devices", "available", "-j").stdout)["devices"]
    phones = [(runtime_version(rt), d) for rt, ds in devices.items() if "iOS" in rt for d in ds if d["name"].startswith("iPhone")]
    if not phones:
        print("no iPhone simulator available")
        return 1
    phones.sort(key=lambda p: p[0])
    device = phones[-1][1]
    udid = device["udid"]
    print(f"simulator: {device['name']} ({udid})", flush=True)
    run("xcrun", "simctl", "boot", udid, check=False)
    run("xcrun", "simctl", "bootstatus", udid, "-b", timeout=600)
    run("xcrun", "simctl", "install", udid, str(app))

    console = ""
    try:
        p = run("xcrun", "simctl", "launch", "--console", "--terminate-running-process", udid, BUNDLE, "--selftest",
                check=False, timeout=1800)
        console = p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        console = f"timed out\n{e.stdout or ''}{e.stderr or ''}"
    (out / "console.txt").write_text(console)

    data = Path(run("xcrun", "simctl", "get_app_container", udid, BUNDLE, "data").stdout.strip())
    for f in data.rglob("*"):
        if f.is_file() and (f.suffix in (".txt", ".log", ".bmp", ".ini")):
            dest = out / f.relative_to(data)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dest)

    reports = list(out.rglob("selftest_report.txt"))
    for log in out.rglob("spindle.log"):
        lines = log.read_text(errors="replace").splitlines()
        print("\n".join(l for l in lines if re.search(r"boot \d+/10|deps:|display:|audio:|selftest: FAIL|error|fatal", l))[:20000])
    if not reports:
        print("no self-test report; console output:\n" + console[-20000:])
        crashes = sorted(Path.home().glob("Library/Logs/DiagnosticReports/spindle*"))
        for c in crashes[-2:]:
            print(f"--- {c.name}\n" + c.read_text(errors="replace")[:20000])
        return 1
    report = reports[0].read_text()
    print(report)
    return 0 if report.startswith("spindle self-test: PASS") else 1


if __name__ == "__main__":
    sys.exit(main())
