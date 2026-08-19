# Hobo 1 — manifest

**Status: pre-existing baseline. Must remain the primary reference per
explicit user instruction ("the existing hobo.swf must remain the primary
baseline") — not duplicated into `/home/claude/game-corpus/` this phase.**

| Field | Value |
|---|---|
| External path | `/home/claude/hobo-testing/hobo.swf` |
| File size | 4,967,978 bytes |
| MD5 | `d59ed276a15e17fe6e08254ad0e51738` |
| SWF version | 6 |
| Compression | zlib (`CWS`) |
| Stage | 600.0 x 450.0 px |
| Frame rate | 25.00 fps |
| Declared frame count | 13 |
| ActionScript | AVM1 (`DoAction`, 2884 tag occurrences) |
| Role | single self-contained movie, no `loadMovie`/multi-SWF structure detected |

Two other, larger, newer files named `hobo.swf` exist on the user's device
(`G:\3DS\hobo.swf`, `G:\3DS\testttttttttt\hobo.swf`, ~7.58 MB each) — these
are explicitly **not** this corpus entry. They were not staged or analyzed
this phase, per the instruction that the existing baseline "must remain
the primary baseline."

Full diagnostic output: [`diagnostic.txt`](diagnostic.txt) (generated via
`swf_diagnostic /home/claude/hobo-testing/hobo.swf`, 2026-08-18).
