# tests/games/ — real-game compatibility corpus (Hobo 1–7, Extreme Pamplona)

**Real-game corpus phase (2026-08-18).** This directory is the anchor for
the project's real-content compatibility corpus, distinct from both
`tests/SwfTestFixtures.cpp` (253 in-memory unit `TEST_CASE`s — synthetic,
minimal, fast) and `tests/swf/` (small standalone hand-crafted `.swf`
files, one isolated feature each). This directory covers real, unmodified,
full-size commercial Flash games — Hobo 1 through 7, and Extreme Pamplona.

## Why the `.swf` binaries are NOT committed here

Per the project's existing (already-established, pre-dating this phase)
convention — confirmed by inspecting `.gitignore`, the absence of any
`.gitattributes`/git-lfs configuration, and the fact that the pre-existing
`hobo.swf` baseline itself was never committed to git — real compatibility
SWFs stay **external to the repo**, referenced by absolute path + checksum.
These files total ~52 MB (Hobo 2–7 alone are 5–8 MB each; the full Extreme
Pamplona package is another ~15 MB across 24 files) and are also, per the
user's own explicit instruction this phase, **never to be modified,
recompressed, or re-exported** — they are the fixed, byte-for-byte
compatibility reference corpus, so there is no "regenerate if lost"
fallback the way `tests/swf/`'s generated files have. Treat every path
below as a pointer to game data the user separately supplied, not as a
build artifact.

Each game subdirectory contains only:

- `manifest.md` — the external absolute path, exact file size, MD5
  checksum, and role (primary loader vs. `loadMovie`-style sub-content) of
  every `.swf` this game's compatibility profile depends on.
- `diagnostic.txt` (or `diagnostic_main_loader.txt` / `content/*.txt` for
  Extreme Pamplona's multi-file structure) — the full, verbatim stdout of
  `swf_diagnostic <file>` (see `tools/swf_diagnostic/main.cpp`) captured
  against that exact file. This is small, plain-text, and safe to commit;
  it is the durable, in-repo evidence trail this phase's
  `docs/real-game-compatibility.md` report is built from.

## Regenerating a diagnostic

```sh
cmake --build build --target swf_diagnostic
./build/swf_diagnostic <absolute-path-to-swf> > tests/games/<game>/diagnostic.txt
```

If the checksum in a `manifest.md` no longer matches the file at its
recorded path, the file has changed since this phase's analysis and the
diagnostic (and every downstream doc derived from it) must be regenerated
before being trusted again.

## Games and canonical local paths (this environment, 2026-08-18)

| Game | Primary file | Stable external path | Status |
|---|---|---|---|
| Hobo 1 | `hobo.swf` | `/home/claude/hobo-testing/hobo.swf` | pre-existing baseline, **must remain primary** per user instruction — not duplicated here |
| Hobo 2 | `hobo2.swf` | `/home/claude/game-corpus/hobo2/hobo2.swf` | staged this phase from the connected device (`G:\3DS\hobo 2 - prison brawl.swf`) |
| Hobo 3 | `hobo3.swf` | `/home/claude/game-corpus/hobo3/hobo3.swf` | staged this phase (`G:\3DS\hobo 3 - wanted.swf`) |
| Hobo 4 | `hobo4.swf` | `/home/claude/game-corpus/hobo4/hobo4.swf` | staged this phase (`G:\3DS\hobo 4 - total war.swf`) |
| Hobo 5 | `hobo5.swf` | `/home/claude/game-corpus/hobo5/hobo5.swf` | staged this phase (`G:\3DS\hobo 5 - space brawl attack of the hobo clones.swf`) |
| Hobo 6 | `hobo6.swf` | `/home/claude/game-corpus/hobo6/hobo6.swf` | staged this phase (`G:\3DS\hobo 6 - hell.swf`) |
| Hobo 7 | `hobo7.swf` | `/home/claude/game-corpus/hobo7/hobo7.swf` | staged this phase (`G:\3DS\hobo 7 - heaven.swf`) |
| Extreme Pamplona | `extreme-pamplona.swf` (main loader) + 23 `content/*.swf` | `/home/claude/game-corpus/extreme_pamplona/` | staged this phase (`G:\3DS\extreme-pamplona\`) — multi-SWF, `loadMovie`-style package; see that game's `manifest.md` for the full file list |

**Important — this environment is ephemeral.** `/home/claude/game-corpus/`
and `/home/claude/hobo-testing/` live in this session's container, not in
git. A future session continuing this work will need the same source files
re-supplied (the user's device still has them under `G:\3DS\`) and
re-staged at these same paths, or the paths in each `manifest.md` updated
to wherever they land next. The MD5 checksums recorded here are exactly
what lets a future session verify a re-staged file is bit-identical to the
one this phase's analysis was actually run against.

See `docs/real-game-compatibility.md` for the full per-game report built
from these diagnostics, and `docs/compatibility-matrix.md`'s cross-game
matrix for the feature-by-feature YES/NO/UNKNOWN comparison.
