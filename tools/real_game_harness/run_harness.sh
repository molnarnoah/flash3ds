#!/usr/bin/env bash
# Real-game regression test harness (real-game-corpus phase, 2026-08-18).
#
# Deterministic mechanism for the desktop `flash_runtime` CLI to load each
# corpus game and parse/initialize/render frames 1-5 where supported.
# Records: successful init, parser failures, runtime exceptions/crashes,
# missing features (via exit code + stderr capture). Does NOT require full
# gameplay -- per the current phase's explicit scope, this is analysis and
# harness-building only, no new Flash runtime feature work.
#
# Usage:
#   tools/real_game_harness/run_harness.sh <build-dir> <output-dir>
#
# Example:
#   tools/real_game_harness/run_harness.sh build /tmp/harness_out
#
# For each game listed in GAMES below, this script:
#   1. Runs `flash_runtime --quiet <file>` once (load/parse/init check).
#   2. Runs `flash_runtime --render <n> <out.ppm> <file>` for n=1..5,
#      recording exit code and (if it succeeded) an MD5 of the resulting
#      PPM as that frame's golden-output baseline.
#   3. Writes one summary line per game to <output-dir>/harness_summary.txt
#      and full stdout/stderr per (game, frame) to
#      <output-dir>/<game>/frame<n>.log.
#
# Golden-output baselines are NOT required to be byte-identical across
# runtime changes when the renderer has known nondeterministic behavior
# (see docs/known-limitations.md) -- this script only *records* the MD5s;
# comparing a later run's MD5s against a previously-recorded baseline
# file is left to whoever is checking for regressions (e.g. `diff` the two
# harness_summary.txt files).

set -u

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-/tmp/real_game_harness_out}"
FLASH_RUNTIME="${BUILD_DIR}/flash_runtime"

if [ ! -x "$FLASH_RUNTIME" ]; then
    echo "error: '$FLASH_RUNTIME' not found or not executable -- build it first:" >&2
    echo "  cmake --build $BUILD_DIR --target flash_runtime" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
SUMMARY="$OUT_DIR/harness_summary.txt"
: > "$SUMMARY"

# name|path -- name is the tests/games/<name> subdirectory this game
# corresponds to; path is the external absolute .swf path (see that
# directory's manifest.md for provenance/checksums).
GAMES=(
    "hobo1|/home/claude/hobo-testing/hobo.swf"
    "hobo2|/home/claude/game-corpus/hobo2/hobo2.swf"
    "hobo3|/home/claude/game-corpus/hobo3/hobo3.swf"
    "hobo4|/home/claude/game-corpus/hobo4/hobo4.swf"
    "hobo5|/home/claude/game-corpus/hobo5/hobo5.swf"
    "hobo6|/home/claude/game-corpus/hobo6/hobo6.swf"
    "hobo7|/home/claude/game-corpus/hobo7/hobo7.swf"
    "extreme_pamplona|/home/claude/game-corpus/extreme_pamplona/extreme-pamplona.swf"
    "cat_ninja|/home/claude/game-corpus/cat_ninja/cat_ninja.swf"
)

for entry in "${GAMES[@]}"; do
    name="${entry%%|*}"
    path="${entry##*|}"
    game_out="$OUT_DIR/$name"
    mkdir -p "$game_out"

    if [ ! -f "$path" ]; then
        echo "$name: MISSING ($path not found -- not staged in this environment)" >> "$SUMMARY"
        continue
    fi

    # Step 1: load/parse/init check.
    "$FLASH_RUNTIME" --quiet "$path" > "$game_out/init.log" 2>&1
    init_rc=$?
    if [ $init_rc -ne 0 ]; then
        echo "$name: INIT_FAILED (exit=$init_rc) see $game_out/init.log" >> "$SUMMARY"
        continue
    fi

    # Step 2: render frames 1-5.
    frame_results=()
    for n in 1 2 3 4 5; do
        ppm="$game_out/frame${n}.ppm"
        "$FLASH_RUNTIME" --render "$n" "$ppm" "$path" > "$game_out/frame${n}.log" 2>&1
        rc=$?
        if [ $rc -eq 0 ] && [ -f "$ppm" ]; then
            md5=$(md5sum "$ppm" | cut -d' ' -f1)
            frame_results+=("f${n}=OK(md5=${md5:0:8})")
        else
            frame_results+=("f${n}=FAIL(exit=$rc)")
        fi
    done

    echo "$name: INIT_OK  ${frame_results[*]}" >> "$SUMMARY"
done

echo "---"
cat "$SUMMARY"
echo "---"
echo "Full logs and rendered frames under: $OUT_DIR"
