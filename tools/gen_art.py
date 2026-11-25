#!/usr/bin/env python3
"""Generate spindle's image assets deterministically (stdlib only).

Right now that is one file: data/art/flame.png, a 128 x 256 RGBA flame used for the fire meter, the
flames around a circle that is on fire, and the little bursts when a needle burns. Like the sound
effects, the PNG is committed and compiled into spindle.exe, so building never needs Python.

    python tools/gen_art.py            # writes data/art/*.png
    python tools/gen_art.py --check    # verifies the committed files match the recipes
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

W, H = 128, 256


def smoothstep(e0: float, e1: float, x: float) -> float:
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def lerp(a: tuple[float, ...], b: tuple[float, ...], t: float) -> tuple[float, ...]:
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def flame_distance(x: float, y: float, cx: float, bulb_y: float, radius: float, tip_y: float, flick: float) -> float:
    """Pixels inside the flame outline (negative outside): a round bulb rising into a curled tip."""
    if y >= bulb_y:
        dy = y - bulb_y
        if dy >= radius:
            return -(dy - radius) - abs(x - cx)
        half = math.sqrt(radius * radius - dy * dy)
        return half - abs(x - cx)
    if y <= tip_y:
        return -(tip_y - y) - abs(x - cx)
    k = (bulb_y - y) / (bulb_y - tip_y)  # 0 at the bulb, 1 at the tip
    half = radius * (1 - k) ** 1.15
    centre = cx + flick * math.sin(math.pi * k) * k
    return half - abs(x - centre)


EDGE = (232.0, 64.0, 20.0)
MID = (255.0, 138.0, 28.0)
CORE = (255.0, 236.0, 168.0)
HOT = (255.0, 252.0, 228.0)


def flame_pixel(px: int, py: int) -> tuple[int, int, int, int]:
    x, y = px + 0.5, py + 0.5
    outer = flame_distance(x, y, 64.0, 182.0, 56.0, 10.0, 11.0)
    inner = flame_distance(x, y, 64.0, 198.0, 30.0, 92.0, 6.0)

    if outer < 0:
        # soft halo so the flame glows on black
        a = 0.22 * math.exp(outer / 5.0)
        return (int(EDGE[0]), int(EDGE[1]), int(EDGE[2]), int(round(255 * a)))

    color = lerp(EDGE, MID, smoothstep(0.0, 16.0, outer))
    color = lerp(color, CORE, smoothstep(0.0, 5.0, inner))
    color = lerp(color, HOT, smoothstep(6.0, 22.0, inner))
    alpha = smoothstep(0.0, 2.5, outer) * (0.6 + 0.4 * smoothstep(10.0, 96.0, y))
    alpha = max(alpha, 0.22)  # continuous with the halo at the edge
    alpha = min(1.0, alpha)
    return tuple(int(round(c)) for c in color) + (int(round(255 * alpha)),)  # type: ignore[return-value]


def encode_png(width: int, height: int, rows: list[bytes]) -> bytes:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def flame() -> bytes:
    rows = []
    for py in range(H):
        row = bytearray()
        for px in range(W):
            row += bytes(flame_pixel(px, py))
        rows.append(bytes(row))
    return encode_png(W, H, rows)


RECIPES = {"flame": flame}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "art")
    parser.add_argument("--check", action="store_true", help="verify committed files instead of writing")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    mismatches = 0
    for name, recipe in RECIPES.items():
        data = recipe()
        path = args.out / f"{name}.png"
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                print(f"MISMATCH {path}")
                mismatches += 1
            else:
                print(f"ok       {path} ({len(data)} bytes)")
        else:
            path.write_bytes(data)
            print(f"wrote    {path} ({len(data)} bytes)")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
