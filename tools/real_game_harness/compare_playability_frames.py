#!/usr/bin/env python3
"""compare_playability_frames.py

Track A, A1 (2026-08-27 task) diff step. hobo_playability_probe writes two
PPM sequences -- <outdir>/held/tickNNN.ppm (Left/Up/Right/Down + 'A'/'S'
held every tick) and <outdir>/control/tickNNN.ppm (no input) -- and
deliberately leaves the actual diffing to this separate script (see that
tool's header comment) so the comparison logic can be iterated on without
recompiling C++.

Three comparisons matter here, not just one:

1. held[tick] vs control[tick], per sampled tick -- this is the actual
   playability signal. If these are byte-identical at every tick, holding
   the movement/action keys had literally zero effect on any rendered
   pixel, which would mean either the input isn't reaching the game the
   way hobo-title-progression.md's Key.isDown() finding assumed, or the
   game's *visible* response to those keys doesn't show up in the sampled
   window.

2. control[tick0] vs control[tickN] -- whether ANYTHING animates on its
   own (nested-clip animation, nothing to do with input). If this is also
   identical, the movie is fully static after frame 1 regardless of input,
   which would be a much bigger finding than "input doesn't do anything."

3. held[tick0] vs held[tickN] -- same idea, with input held throughout.

Usage:
    python3 compare_playability_frames.py <outdir> [--png]

<outdir> must be the same directory passed to hobo_playability_probe (i.e.
it contains held/ and control/ subdirectories of tickNNN.ppm files).
--png additionally writes a .png next to every .ppm (via Pillow, if
available) and a side-by-side-diff PNG per tick, purely for human viewing
-- the comparison itself only ever reads the PPMs.
"""

import argparse
import os
import re
import sys


def read_ppm(path):
    """Reads a binary P6 PPM. Returns (width, height, maxval, bytes)."""
    with open(path, "rb") as f:
        data = f.read()

    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: not a binary P6 PPM (header={data[:16]!r})")

    # Tokenize the header: magic, width, height, maxval, then exactly one
    # whitespace byte before the raw pixel data. '#' starts a comment to
    # end-of-line, per the NetPBM spec.
    pos = 2
    tokens = []
    while len(tokens) < 3:
        # skip whitespace
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            nl = data.index(b"\n", pos)
            pos = nl + 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    width, height, maxval = (int(t) for t in tokens)
    pos += 1  # the single mandatory whitespace byte after maxval

    expected = width * height * 3
    pixels = data[pos:pos + expected]
    if len(pixels) != expected:
        raise ValueError(
            f"{path}: expected {expected} pixel bytes, got {len(pixels)} "
            f"(w={width} h={height})"
        )
    return width, height, maxval, pixels


def diff_stats(a, b, width, height):
    """Compares two equal-size raw RGB byte buffers. Returns a dict with
    pixel-diff count/percentage and the bounding box of differing pixels
    (None if identical)."""
    assert len(a) == len(b)
    if a == b:
        return {"identical": True, "diff_pixels": 0, "diff_pct": 0.0, "bbox": None}

    min_x, min_y, max_x, max_y = width, height, -1, -1
    diff_pixels = 0
    for y in range(height):
        row = y * width * 3
        for x in range(width):
            o = row + x * 3
            if a[o:o + 3] != b[o:o + 3]:
                diff_pixels += 1
                if x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y

    total = width * height
    return {
        "identical": False,
        "diff_pixels": diff_pixels,
        "diff_pct": 100.0 * diff_pixels / total,
        "bbox": (min_x, min_y, max_x, max_y),
    }


def list_ticks(run_dir):
    ticks = {}
    for name in os.listdir(run_dir):
        m = re.match(r"tick(\d+)\.ppm$", name)
        if m:
            ticks[int(m.group(1))] = os.path.join(run_dir, name)
    return dict(sorted(ticks.items()))


def maybe_write_pngs(outdir, held_ticks, control_ticks):
    try:
        from PIL import Image
    except ImportError:
        print("(--png requested but Pillow is not installed; skipping PNG export)")
        return

    for label, ticks in (("held", held_ticks), ("control", control_ticks)):
        for tick, path in ticks.items():
            w, h, _, pixels = read_ppm(path)
            img = Image.frombytes("RGB", (w, h), bytes(pixels))
            png_path = os.path.splitext(path)[0] + ".png"
            img.save(png_path)
    print(f"wrote PNGs alongside every .ppm under {outdir}/held and {outdir}/control")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outdir", help="directory passed to hobo_playability_probe")
    parser.add_argument(
        "--png", action="store_true", help="also write .png copies via Pillow (for human viewing)"
    )
    args = parser.parse_args()

    held_dir = os.path.join(args.outdir, "held")
    control_dir = os.path.join(args.outdir, "control")
    if not os.path.isdir(held_dir) or not os.path.isdir(control_dir):
        print(f"error: expected {held_dir} and {control_dir} to exist", file=sys.stderr)
        return 1

    held_ticks = list_ticks(held_dir)
    control_ticks = list_ticks(control_dir)

    common = sorted(set(held_ticks) & set(control_ticks))
    if not common:
        print("error: no matching tick numbers between held/ and control/", file=sys.stderr)
        return 1

    print("=== 1. held vs control, per sampled tick (THE playability signal) ===")
    any_held_vs_control_diff = False
    for tick in common:
        w, h, _, hp = read_ppm(held_ticks[tick])
        w2, h2, _, cp = read_ppm(control_ticks[tick])
        if (w, h) != (w2, h2):
            print(f"  tick {tick:3d}: SIZE MISMATCH held={w}x{h} control={w2}x{h2}")
            continue
        stats = diff_stats(hp, cp, w, h)
        if stats["identical"]:
            print(f"  tick {tick:3d}: identical")
        else:
            any_held_vs_control_diff = True
            print(
                f"  tick {tick:3d}: DIFFERS -- {stats['diff_pixels']} px "
                f"({stats['diff_pct']:.3f}%), bbox={stats['bbox']}"
            )

    first, last = common[0], common[-1]

    print(f"\n=== 2. control[tick{first}] vs control[tick{last}] (does ANYTHING animate w/o input?) ===")
    w, h, _, c_first = read_ppm(control_ticks[first])
    _, _, _, c_last = read_ppm(control_ticks[last])
    c_self_stats = diff_stats(c_first, c_last, w, h)
    if c_self_stats["identical"]:
        print("  identical -- control run is fully static after tick 0")
    else:
        print(
            f"  DIFFERS -- {c_self_stats['diff_pixels']} px "
            f"({c_self_stats['diff_pct']:.3f}%), bbox={c_self_stats['bbox']}"
        )

    print(f"\n=== 3. held[tick{first}] vs held[tick{last}] (does anything animate WITH input held?) ===")
    w, h, _, h_first = read_ppm(held_ticks[first])
    _, _, _, h_last = read_ppm(held_ticks[last])
    h_self_stats = diff_stats(h_first, h_last, w, h)
    if h_self_stats["identical"]:
        print("  identical -- held run is fully static after tick 0")
    else:
        print(
            f"  DIFFERS -- {h_self_stats['diff_pixels']} px "
            f"({h_self_stats['diff_pct']:.3f}%), bbox={h_self_stats['bbox']}"
        )

    print("\n=== Verdict ===")
    if any_held_vs_control_diff:
        print(
            "held vs control DIFFERS at one or more sampled ticks: holding "
            "Left/Up/Right/Down + 'A'/'S' produces a visible effect distinct "
            "from doing nothing. This is real evidence of engine-level "
            "playability (though it doesn't by itself prove the game is "
            "correctly playable -- inspect the actual frames)."
        )
    elif not c_self_stats["identical"] or not h_self_stats["identical"]:
        print(
            "held vs control are pixel-identical at every sampled tick, but "
            "the movie is NOT static (frames change over time regardless of "
            "input) -- animation is happening but it is not observably "
            "input-driven within this sampled window. This does not confirm "
            "playability; it suggests either the sampled window is too "
            "coarse (try a smaller --sampleEvery) or the input isn't "
            "reaching a code path with a visible effect."
        )
    else:
        print(
            "held vs control are pixel-identical AND both runs are fully "
            "static after tick 0: nothing observably changes at all, with "
            "or without input, across the sampled ticks. This points at the "
            "engine (or this movie) not producing any visible per-frame "
            "change here, independent of the input question."
        )

    if args.png:
        maybe_write_pngs(args.outdir, held_ticks, control_ticks)

    return 0


if __name__ == "__main__":
    sys.exit(main())
