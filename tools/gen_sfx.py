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


def powerup() -> list[float]:
    notes = [988.0, 1319.0, 1760.0]
    parts = [tone(lambda t, f=f: f * (1 + 0.6 * t), 0.06, ((1, 1.0), (3, 0.12)), attack=0.002, release=0.02) for f in notes]
    shimmer = tone(lambda t: 2637.0, 0.22, ((1, 0.7), (2, 0.1)), attack=0.002, release=0.08, decay=0.07)
    return concat(*parts, shimmer, gap=0.008)


def shield() -> list[float]:
    rnd = Noise(23)
    burst = [rnd.next() * env(i / RATE, 0.05, attack=0.0005, release=0.02, decay=0.012) * 0.8 for i in range(int(0.05 * RATE))]
    ring = tone(lambda t: 2300 - 900 * t, 0.3, ((1, 1.0), (2.76, 0.35), (5.4, 0.15)), attack=0.001, release=0.06, decay=0.09)
    return mix(burst, ring)


def ignite() -> list[float]:
    # whoosh: noise through a lowpass that opens up, over a rising roar
    rnd = Noise(41)
    dur = 0.55
    out = []
    low = 0.0
    for i in range(int(dur * RATE)):
        t = i / RATE
        cutoff = 300 + 5200 * min(1.0, t / 0.22)
        k = 1 - math.exp(-2 * math.pi * cutoff / RATE)
        low += k * (rnd.next() - low)
        out.append(low * env(t, dur, attack=0.06, release=0.25, decay=0.35) * 1.6)
    roar = tone(lambda t: 90 + 260 * min(1.0, t / 0.3), dur, ((1, 0.5), (2, 0.3), (3, 0.15)), attack=0.05, release=0.25, decay=0.3)
    return mix(out, roar)


def burn() -> list[float]:
    # crackle: a few short, bright noise pops
    rnd = Noise(57)
    out: list[float] = []
    for start, amp in ((0.0, 1.0), (0.035, 0.7), (0.07, 0.85), (0.11, 0.5)):
        n0 = int(start * RATE)
        out += [0.0] * max(0, n0 - len(out))
        pop = [rnd.next() * env(i / RATE, 0.03, attack=0.0004, release=0.01, decay=0.006) * amp for i in range(int(0.03 * RATE))]
        for i, v in enumerate(pop):
            if n0 + i < len(out):
                out[n0 + i] += v
            else:
                out.append(v)
    hiss = tone(lambda t: 1800 - 1200 * t, 0.16, ((1, 0.25), (2.3, 0.12)), attack=0.001, release=0.05, decay=0.05)
    return mix(out, hiss)


def round_start() -> list[float]:
    return tone(lambda t: 880, 0.12, ((1, 1.0), (2, 0.12)), attack=0.004, release=0.05, decay=0.09)


def tally_tick(step: int) -> list[float]:
    # one digit landing in the circle: a short blip that climbs a little with every digit
    f = 660.0 * (2 ** (step * 3 / 12))
    return tone(lambda t: f, 0.07, ((1, 1.0), (2, 0.2)), attack=0.002, release=0.03, decay=0.03)


def tally_done() -> list[float]:
    # the number is in: a soft two-note "ding-dong" chord
    a = tone(lambda t: 784.0, 0.32, ((1, 0.6), (2, 0.15)), attack=0.003, release=0.12, decay=0.18)
    b = tone(lambda t: 1046.5, 0.32, ((1, 0.5), (2, 0.1)), attack=0.003, release=0.12, decay=0.18)
    return mix(a, [0.0] * int(0.04 * RATE) + b)


def crown() -> list[float]:
    # the crown lands: a quick rising fanfare with a shimmer on top
    notes = [659.25, 830.61, 987.77]
    parts = [tone(lambda t, f=f: f, 0.08, ((1, 1.0), (2, 0.3), (3, 0.12)), attack=0.003, release=0.02) for f in notes]
    last = tone(lambda t: 1318.5, 0.5, ((1, 1.0), (2, 0.25), (4, 0.08)), attack=0.003, release=0.2, decay=0.35)
    shimmer = tone(lambda t: 2637.0 + 40 * math.sin(t * 60), 0.5, ((1, 0.25),), attack=0.05, release=0.25, decay=0.3)
    head = concat(*parts, gap=0.01)
    return concat(head, mix(last, shimmer))


def letter_shot() -> list[float]:
    # a letter fired into the circle: a falling "pew" then a little thunk
    pew = tone(lambda t: 1400 - 900 * min(1.0, t / 0.06), 0.07, ((1, 0.8), (2, 0.15)), attack=0.001, release=0.02)
    thunk = tone(lambda t: 220, 0.05, ((1, 0.8), (3, 0.2)), attack=0.001, release=0.02, decay=0.02)
    return concat(pew, thunk)


def initials_done() -> list[float]:
    # name saved: a bright little arpeggio
    notes = [523.25, 659.25, 783.99, 1046.5, 1318.5]
    parts = [tone(lambda t, f=f: f, 0.07, ((1, 1.0), (2, 0.2)), attack=0.002, release=0.02) for f in notes[:-1]]
    last = tone(lambda t: notes[-1], 0.3, ((1, 1.0), (2, 0.2)), attack=0.002, release=0.12, decay=0.2)
    return concat(*parts, last, gap=0.008)


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
    "powerup": powerup,
    "shield": shield,
    "ignite": ignite,
    "burn": burn,
    "tally_tick_1": lambda: tally_tick(0),
    "tally_tick_2": lambda: tally_tick(1),
    "tally_tick_3": lambda: tally_tick(2),
    "tally_tick_4": lambda: tally_tick(3),
    "tally_done": tally_done,
    "crown": crown,
    "letter_shot": letter_shot,
    "initials_done": initials_done,
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
