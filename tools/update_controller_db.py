#!/usr/bin/env python3
"""Pin a new version of the community SDL_GameControllerDB (stdlib only).

spindle downloads exactly one external file, gamecontrollerdb.txt, and accepts it only if its size
and SHA-256 match the pin compiled into the executable. This tool fetches the file at a given
commit, prints its hash and rewrites src/assets/ControllerDbPin.hpp.

    python tools/update_controller_db.py COMMIT_SHA [--dry-run]

Find the commit you want on https://github.com/mdqinc/SDL_GameControllerDB (use a full 40-character
SHA, never a branch name, so the pin can never change underneath a build).
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import urllib.request
from pathlib import Path

REPO = "mdqinc/SDL_GameControllerDB"
PIN = Path(__file__).resolve().parent.parent / "src" / "assets" / "ControllerDbPin.hpp"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("commit")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        print("commit must be a full 40-character lowercase SHA")
        return 2
    url = f"https://raw.githubusercontent.com/{REPO}/{args.commit}/gamecontrollerdb.txt"
    request = urllib.request.Request(url, headers={"User-Agent": "spindle-dev-tools"})
    with urllib.request.urlopen(request, timeout=60) as resp:
        data = resp.read()
    digest = hashlib.sha256(data).hexdigest()
    lines = data.count(b"\n")
    print(f"{url}\n  {len(data)} bytes, {lines} lines, sha256 {digest}")
    if args.dry_run:
        return 0
    text = PIN.read_text()
    text = re.sub(r'kCommit = "[0-9a-f]{40}"', f'kCommit = "{args.commit}"', text)
    text = re.sub(r'kSha256 = "[0-9a-f]{64}"', f'kSha256 = "{digest}"', text)
    text = re.sub(r"kSizeBytes = \d+", f"kSizeBytes = {len(data)}", text)
    text = re.sub(r'SDL_GameControllerDB/[0-9a-f]{40}/gamecontrollerdb.txt', f"SDL_GameControllerDB/{args.commit}/gamecontrollerdb.txt", text)
    PIN.write_text(text)
    print(f"updated {PIN}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
