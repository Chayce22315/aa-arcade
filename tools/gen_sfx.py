#!/usr/bin/env python3
"""Generate spindle's sound effects deterministically (stdlib only).

The resulting 44.1 kHz mono 16-bit WAV files are committed in data/audio/ and compiled into
spindle.exe, so the game build never needs Python. Re-run after changing a recipe:

    python tools/gen_sfx.py            # writes data/audio/*.wav
    python tools/gen_sfx.py --check    # verifies the committed files match the recipes
"""

from __future__ import annotations

import argparse
import io
import math
import sys
import wave
from pathlib import Path

RATE = 44100
PEAK = 0.70


def env(t: float, dur: float, attack: float = 0.002, release: float = 0.01, decay: float | None = None) -> float:
    """Attack / (optional exponential decay) / release envelope, 0..1."""
    if t < 0 or t >= dur:
        return 0.0
    a = min(1.0, t / attack) if attack > 0 else 1.0
    r = min(1.0, (dur - t) / release) if release > 0 else 1.0
    d = math.exp(-t / decay) if decay else 1.0
    return a * r * d


class Noise:
    """Deterministic LCG noise in -1..1."""

    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFF

    def next(self) -> float:
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return (self.state / 0xFFFFFFFF) * 2.0 - 1.0


def tone(freq_at, dur: float, harmonics=((1, 1.0),), **env_args) -> list[float]:
    out = []
    phase = 0.0
    n = int(dur * RATE)
    for i in range(n):
        t = i / RATE
        f = freq_at(t)
        phase += 2 * math.pi * f / RATE
        s = sum(amp * math.sin(phase * h) for h, amp in harmonics)
        out.append(s * env(t, dur, **env_args))
    return out


def concat(*parts: list[float], gap: float = 0.0) -> list[float]:
    out: list[float] = []
    silence = [0.0] * int(gap * RATE)
    for i, p in enumerate(parts):
        if i:
            out += silence
        out += p
    return out


def mix(*parts: list[float]) -> list[float]:
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def needle_hit() -> list[float]:
    body = tone(lambda t: 1250 - 2500 * t, 0.09, ((1, 1.0), (2, 0.25)), attack=0.001, release=0.01, decay=0.022)
    rnd = Noise(7)
    click = [rnd.next() * env(i / RATE, 0.012, attack=0.0005, release=0.004, decay=0.003) * 0.6 for i in range(int(0.012 * RATE))]
    return mix(body, click)


def fail() -> list[float]:
    return tone(lambda t: 220 - 200 * t, 0.45, ((1, 1.0), (3, 0.33), (5, 0.18), (7, 0.1)), attack=0.004, release=0.06, decay=0.25)


def clear() -> list[float]:
    a = tone(lambda t: 660, 0.09, ((1, 1.0), (2, 0.2)), attack=0.003, release=0.02)
    b = tone(lambda t: 990, 0.16, ((1, 1.0), (2, 0.2)), attack=0.003, release=0.05, decay=0.12)
    return concat(a, b, gap=0.02)


def coin() -> list[float]:
    hi = tone(lambda t: 1568, 0.35, ((1, 1.0), (2, 0.15)), attack=0.001, release=0.04, decay=0.11)
    top = tone(lambda t: 2093, 0.30, ((1, 0.6),), attack=0.001, release=0.04, decay=0.08)
    return mix(hi, [0.0] * int(0.045 * RATE) + top)


def join() -> list[float]:
    return tone(lambda t: 500 + 3300 * t, 0.12, ((1, 1.0), (2, 0.2)), attack=0.002, release=0.03)


def menu_move() -> list[float]:
    return tone(lambda t: 1000, 0.03, ((1, 1.0),), attack=0.001, release=0.012)


def menu_select() -> list[float]:
    a = tone(lambda t: 700, 0.04, ((1, 1.0),), attack=0.001, release=0.01)
    b = tone(lambda t: 1050, 0.06, ((1, 1.0),), attack=0.001, release=0.025)
    return concat(a, b, gap=0.01)


def winner() -> list[float]:
    notes = [523.25, 659.25, 783.99]
    parts = [tone(lambda t, f=f: f, 0.11, ((1, 1.0), (2, 0.25), (3, 0.1)), attack=0.003, release=0.02) for f in notes]
    last = tone(lambda t: 1046.5, 0.42, ((1, 1.0), (2, 0.25), (3, 0.1)), attack=0.003, release=0.12, decay=0.3)
    return concat(*parts, last, gap=0.015)


def round_start() -> list[float]:
    return tone(lambda t: 880, 0.12, ((1, 1.0), (2, 0.12)), attack=0.004, release=0.05, decay=0.09)


RECIPES = {
    "needle_hit": needle_hit,
    "fail": fail,
    "clear": clear,
    "coin": coin,
    "join": join,
    "menu_move": menu_move,
    "menu_select": menu_select,
    "winner": winner,
    "round_start": round_start,
}


def render(samples: list[float]) -> bytes:
    peak = max((abs(s) for s in samples), default=0.0) or 1.0
    gain = PEAK / peak
    frames = bytearray()
    for s in samples:
        v = int(round(max(-1.0, min(1.0, s * gain)) * 32767))
        frames += v.to_bytes(2, "little", signed=True)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    return buf.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "audio")
    parser.add_argument("--check", action="store_true", help="verify committed files instead of writing")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    mismatches = 0
    for name, recipe in RECIPES.items():
        data = render(recipe())
        path = args.out / f"{name}.wav"
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
