#!/usr/bin/env python3
"""Run spindle's scripted four-station self-test and collect the results (stdlib only).

    python tools/run_selftest.py path/to/spindle[.exe] [--out DIR] [--wine]

It runs `spindle --root DIR --selftest DIR/out` with SDL's offscreen video driver and dummy audio
driver (so it works headless and in CI), prints the report, converts the BMP screenshots to PNG and
exits with the executable's exit code (0 = every step passed).
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path


def bmp_to_png(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{src}: not a BMP file")
    offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32) or header_size < 40:
        raise ValueError(f"{src}: unsupported BMP ({bpp} bpp)")
    top_down = height < 0
    height = abs(height)
    stride = ((width * bpp + 31) // 32) * 4
    step = bpp // 8
    rows = []
    for y in range(height):
        row_index = y if top_down else height - 1 - y
        row = data[offset + row_index * stride : offset + row_index * stride + width * step]
        out = bytearray(b"\x00")
        for x in range(width):
            b, g, r = row[x * step], row[x * step + 1], row[x * step + 2]
            out += bytes((r, g, b))
        rows.append(bytes(out))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
    dst.write_bytes(png)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--out", type=Path, default=Path("selftest-run"))
    parser.add_argument("--wine", action="store_true", help="run a Windows build through wine")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()

    root = args.out.resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    out = root / "out"
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
    cmd = [str(args.exe.resolve()), "--root", str(root), "--selftest", str(out)]
    if args.wine:
        cmd.insert(0, "wine")
    print("running:", " ".join(cmd), flush=True)
    try:
        proc = subprocess.run(cmd, env=env, timeout=args.timeout, capture_output=True, text=True, errors="replace")
        code = proc.returncode
        (root / "console.log").write_text(proc.stdout + proc.stderr)
    except subprocess.TimeoutExpired:
        print(f"self-test did not finish within {args.timeout} s")
        return 3

    report = out / "selftest_report.txt"
    if report.exists():
        print(report.read_text())
    else:
        print("no report written; console output:")
        print((root / "console.log").read_text()[-4000:])
    for bmp in sorted(out.glob("*.bmp")):
        try:
            bmp_to_png(bmp, bmp.with_suffix(".png"))
        except ValueError as exc:
            print(f"warning: {exc}")
    print(f"exit code {code}; artefacts in {out}")
    return code


if __name__ == "__main__":
    sys.exit(main())
