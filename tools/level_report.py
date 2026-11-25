#!/usr/bin/env python3
"""Summarise and sanity-check the round curve produced by the level script (stdlib only).

The game generates the rounds (`spindle --level-report ROUNDS SEED`, CSV on stdout, the same Lua
script and validation the cabinet uses); this tool checks the curve over several seeds:
every round valid, needle counts never shrinking between tiers, and enough packing margin.

    python tools/level_report.py --exe path/to/spindle[.exe] [--rounds 200] [--seeds 5]
"""

from __future__ import annotations

import argparse
import csv
import io
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--seeds", type=int, default=3)
    args = parser.parse_args()

    problems = 0
    for seed in range(1, args.seeds + 1):
        out = subprocess.run([str(args.exe), "--level-report", str(args.rounds), str(seed)], check=True, capture_output=True,
                             text=True, timeout=120).stdout
        rows = list(csv.DictReader(io.StringIO(out)))
        if len(rows) != args.rounds:
            print(f"seed {seed}: expected {args.rounds} rows, got {len(rows)}")
            problems += 1
            continue
        for r in rows:
            if r["valid"] != "1":
                print(f"seed {seed} round {r['round']}: invalid ({r['problem']})")
                problems += 1
            if float(r["packing"]) > 0.87:
                print(f"seed {seed} round {r['round']}: circle {float(r['packing']):.0%} full")
                problems += 1
        if seed == 1:
            print("round  launch  prepins  segments  max deg/s  reversals  packing  rings")
            for r in rows:
                n = int(r["round"])
                if n <= 5 or n % 10 == 0:
                    print(f"{n:5}  {r['launch']:>6}  {r['prepins']:>7}  {r['segments']:>8}  {float(r['max_speed_deg']):9.0f}  "
                          f"{r['reversals']:>9}  {float(r['packing']):7.0%}  {r['pickups']:>5}")
        launches = [int(r["launch"]) for r in rows]
        if any(b < a for a, b in zip(launches, launches[1:])):
            print(f"seed {seed}: launch count decreases somewhere")
            problems += 1
    print(f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
