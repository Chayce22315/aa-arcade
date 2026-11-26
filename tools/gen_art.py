#!/usr/bin/env python3
"""Generate spindle's image assets deterministically (stdlib only).

- data/art/flame.png: a 128 x 256 RGBA flame used for the fire meter, the flames around a circle that is
  on fire, and the little bursts when a needle burns. Compiled into the game.
- The app icon (a core with its needles, white on black) at every size the phone builds need:
  ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png and the Android launcher icons under
  android/app/src/main/res/ (legacy squares plus the adaptive-icon foreground).

Like the sound effects, the PNGs are committed, so building never needs Python.

    python tools/gen_art.py            # writes every image
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


def encode_png(width: int, height: int, rows: list[bytes], color_type: int = 6) -> bytes:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)  # 8-bit RGBA (6) or RGB (2)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def flame() -> bytes:
    rows = []
    for py in range(H):
        row = bytearray()
        for px in range(W):
            row += bytes(flame_pixel(px, py))
        rows.append(bytes(row))
    return encode_png(W, H, rows)


# ------------------------------------------------------------------ app icon

def icon_shapes():
    """The attract-mode decoration in unit coordinates (0..1, y down): a core, nine needles, one in flight."""
    cx, cy = 0.5, 0.46
    core, length, ball, needle = 0.125, 0.30, 0.042, 0.011
    shapes = [("disc", cx, cy, core)]
    for i in range(9):
        a = -math.pi / 2 + 2 * math.pi * i / 9 + (0.25 if i % 3 == 0 else 0.0)
        px, py = cx + length * math.cos(a), cy + length * math.sin(a)
        shapes.append(("capsule", cx, cy, px, py, needle))
        shapes.append(("disc", px, py, ball))
    shapes.append(("disc", cx, cy + 0.43, ball))  # the next needle on its way up
    return shapes


def _segment_distance(x, y, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - (ax + vx * t), y - (ay + vy * t))


def icon(size: int, transparent: bool, scale: float = 1.0) -> bytes:
    """White marks with analytic anti-aliasing; scale shrinks the art toward the centre (adaptive icons)."""
    shapes = icon_shapes()
    reach = 0.5 * scale * 1.02
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            # pixel centre in unit art coordinates
            ux = 0.5 + ((px + 0.5) / size - 0.5) / scale
            uy = 0.5 + ((py + 0.5) / size - 0.5) / scale
            cover = 0.0
            if abs((px + 0.5) / size - 0.5) <= reach and abs((py + 0.5) / size - 0.5) <= reach:
                d = 1e9
                for sh in shapes:
                    if sh[0] == "disc":
                        d = min(d, math.hypot(ux - sh[1], uy - sh[2]) - sh[3])
                    else:
                        d = min(d, _segment_distance(ux, uy, sh[1], sh[2], sh[3], sh[4]) - sh[5])
                d_px = d * size * scale
                cover = max(0.0, min(1.0, 0.5 - d_px))
            v = int(round(255 * cover))
            if transparent:
                row += bytes((255, 255, 255, v))
            else:
                row += bytes((v, v, v))
        rows.append(bytes(row))
    return encode_png(size, size, rows, 6 if transparent else 2)


ANDROID_DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}

ROOT = Path(__file__).resolve().parent.parent


def outputs():
    """(path relative to the repository, recipe) for every generated image."""
    out = [(Path("data/art/flame.png"), flame)]
    # iOS: one 1024 x 1024 opaque icon (the App Store refuses an alpha channel).
    out.append((Path("ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png"), lambda: icon(1024, False)))
    res = Path("android/app/src/main/res")
    for name, k in ANDROID_DENSITIES.items():
        legacy, fg = int(48 * k), int(108 * k)
        out.append((res / f"mipmap-{name}" / "ic_launcher.png", lambda n=legacy: icon(n, False)))
        # Adaptive icon foreground: 108 dp canvas, art inside the 66 dp safe circle; background colour in XML.
        out.append((res / f"mipmap-{name}" / "ic_launcher_foreground.png", lambda n=fg: icon(n, True, 66.0 / 108.0)))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to write into")
    parser.add_argument("--check", action="store_true", help="verify committed files instead of writing")
    args = parser.parse_args()
    mismatches = 0
    for rel, recipe in outputs():
        data = recipe()
        path = args.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
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
