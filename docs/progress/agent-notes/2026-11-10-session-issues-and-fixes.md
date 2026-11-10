# Session issues and fixes (2026-11-10)

Everything that went wrong in this session, how it presented, and what fixed
it. The point is the next session recognising the shape faster.

| # | Issue | Fixed by |
|---|---|---|
| 1 | No git identity configured; the first commit refused | Repo-local `user.name`/`user.email`, then the owner's rule: `terezi <jules@foxtails.me>` (recorded in `AGENTS.md`) |
| 2 | A counter read by another thread published a wrapped `u64` | Time into a local, publish once, atomically (`port_compose_pica.c`) |
| 3 | A work reduction that did not touch the wall (pacing) | Reverted; rule written into `decisions.md` |
| 4 | A correct fast path that never ran (engine A's hash) | Path proven with byte counters; the finding moved to the setup block |
| 5 | An emulator A/B that cannot measure the hardware cost | Byte counts + hardware logs instead of Azahar milliseconds |
| 6 | A hardware pair that was not matched (PROFILE vs OFF) | Recorded as an open claim; no overhead number quoted |
| 7 | Artifacts built from a dirty tree | Rebuilt against the clean commit; `dirty: no` in the build info |
| 8 | The Windows test suite has 22 environment failures | Baseline run on a stashed tree; only *new* failures count |
| 9 | The emulator driver aborted before launching | Process handling made non-fatal; run script made idempotent |
| 10 | Two hardware logs with no run manifest | Packets now stage one; `bench_manifest.py` re-hashes the inputs |

## 1. No git identity

`git commit` failed with "Author identity unknown". The repo's history is
`A4 <a4@example.invalid>`, so the session set that identity locally to match.
The owner then specified the real one: the only identity an agent may use here
is **`terezi <jules@foxtails.me>`**, set per repository, recorded in
`AGENTS.md` with the reason (an artifact's `build-info` names its commit, so
re-authoring history is a last resort, not a tidy-up).

## 2. A counter that publishes a transient negative

`stage 16102.78 ms/draw` and `end 274113216854.52` in the OFF hardware log,
while the PROFILE log of the same code was sane. Cause: `sDbgStageTicks -= now
... += now` left a negative `u64` for the whole phase, and the shell samples
from another thread every 600 frames. Fix: locals plus one atomic publication,
and atomic reads. Full entry in [3DS-Pitfalls](../../wiki/3DS-Pitfalls.md).

## 3. The pacing shortcut

Geometry built ~27 scenes/s for the ~6 the display could take. A
consumer-paced throttle was built, measured and reverted: it left the drawer's
92 ms untouched, the first design halved Azahar composes, and the ratio that
justified it came from counters `STATE.md` already records as double-counting.
The rule is in [decisions.md](../../decisions.md) and the account in
[the session note](2026-11-10-o3ds-baseline-and-the-pacing-mistake.md).

## 4. A fast path that is correct but never taken

The referenced-range VRAM signature was exact (0 disagreements against the
full hash over boot → title → field) and changed nothing, because engine A
never reaches its hash: it shows the 3D, so a new 3D image forces a re-render.
Engine B does reach it, and there it is a real 4x on the pass (2.52 → 0.63
ms). Evidence that a fast path fires is a byte counter, not a green test.

## 5. Azahar cannot price an Old 3DS memory stream

The emulator made the same hash look like ~1 ms; the hardware spends ~40 ms
of a 50 ms pass in that region. Emulator milliseconds are a shape, not a
price; byte counts and per-phase percentages from real hardware are the
currency. This is also why the "geometry is binding, the drawer is cheap"
ranking from the 2026-11-09 Azahar baseline was wrong by 20-40x.

## 6. A hardware A/B that was not matched

PROFILE and OFF hardware logs from identical inputs disagreed by 2x in both
directions (OFF faster on the title, slower in the field) while every cost the
gate cannot touch matched within 3%, and the OFF log's counters were broken.
It is recorded as "not matched, claim open" rather than as an overhead number.
`tools/bench_ab.py` alternates trials for exactly this reason.

## 7. Artifacts built from a dirty tree

The PROFILE `.3dsx` came out as `408a28a-dirty` because it was linked before
the commit that described it. Rebuilt against the clean commit; the packet
manifests now record `dirty: no`. Never compare a dirty artifact with a clean
one — the build info says which it is.

## 8. The Windows test suite is not green, and never was

`make tool-tests` reports 22 failures on a Windows checkout: tests that assert
POSIX symlinks, `/tmp`, or a Linux-target `gcc`. The way to judge a change is
a **baseline on the unmodified tree** (a stash, run, pop), not the raw count.
This session's split introduced exactly two new failures (a Wi-Fi command test
and the tick-source test); both were updated to the new behaviour and the count
returned to the baseline.

## 9. The emulator driver aborted before it launched anything

A run script set `$ErrorActionPreference = 'Stop'`, and `taskkill` writing "not
found" to stderr counted as a terminating error, so the script died before
copying the artifact. Fixed by handling the process list explicitly
(`Get-Process azahar | Stop-Process`) and keeping the rest idempotent: kill,
copy artifact/save/script/ini, delete the old log, launch, wait, kill, collect.

## 10. Hardware logs with no identity

The first two hardware logs arrived with only the *game-data* `manifest.json`
next to them, so the save/script/ini hashes could not be re-checked after the
fact. Packets now carry a run manifest written by `tools/bench_manifest.py`,
and `tools/bench_report.py --check-manifest` refuses a stale artifact, save or
script.
