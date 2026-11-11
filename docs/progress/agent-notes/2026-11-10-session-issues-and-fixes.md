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
| 11 | 96 game sources fail with missing `dwc/` etc. headers | Build with Windows Python: the mingw gcc silently drops `-idirafter /home/...` dirs |
| 12 | The `py` launcher follows the script shebang to MSYS Python | Invoke `C:\Program Files\Python315\python.exe` explicitly |
| 13 | Windows Python resolves `DEVKITARM=/opt/...` to `C:\opt\...` | Export `DEVKITPRO`/`DEVKITARM` in `C:\` form for the build |
| 14 | 3 lobby/pokewalker objects fail: objcopy/readelf must be on PATH | Prepend `devkitARM/bin` to `PATH` |
| 15 | A campaign-kept flag bundle fails its own A/B (P2a revert) | Re-verify inherited flags; revert as a bundle, record it |
| 16 | A counter double-counts its own sub-part (take ⊃ flatfill) | Define partition boundaries before summing; disjoint timers |
| 17 | Hardware logs attributed without checking the banner | Grep every log's runtime banner; key findings by file+console |
| 18 | 96 missing-header failures with identical flags | Print the compiler's `-E -v` search list first |
| 19 | Baseline artifacts overwritten by later rebuilds | Fresh `--out` dirs per experiment; stage packets promptly |

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

## 11. MSYS Python breaks the game-library headers

**Symptom.** After a tree normalisation (every source touched, full rebuild),
96 game sources fail with `dwc/net_common_internal.h: No such file or
directory` (also `nitrowifi/`, `vct/`, `dsprot.h`); the other 996 compile.
The headers exist under `build/upstream/lib/include/`, reached only through
`-idirafter`.

**Cause.** The driver ran under MSYS Python, so every path renders as
`/home/...`. The mingw `arm-none-eabi-gcc` silently drops an `-idirafter`
directory in that form: `gcc -E -v` prints every `-I` dir translated to
`C:/...` but the `-idirafter` dir is absent from the search list, while the
`C:/` form of the same flag appears and preprocesses cleanly. Only the 96
sources including the game-library headers need that directory, so only they
fail. An incremental build on warm objects hides this for months: the failure
appears only on a full rebuild.

**Rule.** On Windows, run the driver under Windows Python
(`C:\Program Files\Python315\python.exe`, the same 3.15.2 the staged packets'
manifests record). Bisecting the flag list is the fast diagnostic: the break
appears exactly when the `-idirafter` dir leaves the `-E -v` search list.

## 12. The `py` launcher follows the shebang

**Symptom.** `py tools/hgss_game.py ...` still produces `/home/...` paths and
the §11 failure, even though `py -c` runs Windows Python 3.15.

**Cause.** The script starts with `#!/usr/bin/env python3`, which the `py`
launcher honours: it resolves `python3` on `PATH` and finds MSYS Python
(`c:\devkitPro\msys2\usr\bin\python3.exe`, on the system `PATH`).

**Rule.** Invoke the Windows interpreter by explicit path. Do not rely on
`py script.py` for any tool that passes paths to native binaries.

## 13. MSYS-style `DEVKITARM` under Windows Python

**Symptom.** The Windows-Python driver dies in `alias_tool_environment`:
`C:\opt\devkitpro\devkitARM\bin\arm-none-eabi-objcopy` not found.

**Cause.** The machine-wide `DEVKITARM` is `/opt/devkitpro/devkitARM`. MSYS
Python tolerates it (own-path translation); Windows Python joins it onto the
current drive, giving `C:\opt\...`.

**Rule.** Export `DEVKITPRO=C:\devkitPro` and
`DEVKITARM=C:\devkitPro\devkitARM` in the build shell.

## 14. objcopy/readelf must be on `PATH`

**Symptom.** With §11–13 fixed, exactly 3 objects (lobby ×2, pokewalker ×1)
fail with `arm-none-eabi-objcopy/readelf must be on PATH` from the
post-compile alias step.

**Cause.** The build shell did not carry `devkitARM/bin` on `PATH`; only the
files needing object aliasing reach that step, so the failure is a confusing
3-file subset, not the whole tree.

**Rule.** Prepend `C:\devkitPro\devkitARM\bin` to `PATH` in every build shell.
Full recipe: explicit Windows Python + `C:\`-form `DEVKITPRO`/`DEVKITARM` +
`devkitARM/bin` on `PATH`.

## 15. A campaign-kept flag bundle that fails its own A/B

**Symptom.** Four geometry flags the Azahar campaign kept (`LISTCACHE`,
`STATEDEDUP`, `FUSEHASH`, `LAZYINSERT`) drop composes 28–33% when enabled
together: built scenes 176 → 150 per window, ms/built 46 → 52.5.

**Cause.** The flags engage correctly (9,238 list-cache hits, audits clean,
zero fuse mismatches), but their per-scene bookkeeping (tags, audits,
census, lazy filter, evict traffic) pushes the saturated geometry worker over
the skip cliff: fewer scenes built, fewer composes. "Kept by the campaign"
describes the campaign's scene and binaries, not a guarantee — the plan's
accept (per-built-scene work falls) is the verdict, and it failed.

**Rule.** Re-verify inherited flags with a matched A/B before flipping any
default; a bundle that fails reverts as a bundle (no per-flag fishing when
the packet scoped the bundle). Record kept-but-failing explicitly — negative
results are deliverables (P2a claim sheet).

## 16. A counter that double-counts its own sub-part

**Symptom.** Top-pass parts summed to 112% of the drawer wall — over 100%,
which is impossible for same-clock active-vs-wall accounting.

**Cause.** `takeTicks` spans take-evaluation through commit, which *includes*
the two flat-plane fills that `flatFillTicks` also measures, plus the SortObjs
time already counted in `objTicks`. Summing take + flatfill + obj counts the
same ticks two or three times.

**Rule.** Define partition boundaries before claiming closure: take *includes*
flatfill and SortObjs, so the honest sum is parts + take (not parts + take +
flatfill). Prefer disjoint timers by construction (stop the take clock around
the fills); where overlap is structural, subtract and document it. The
corrected sum closed at ~97% of wall.

## 17. Attributing logs to hardware without checking the banner

**Symptom.** Two fresh hardware logs were assumed to be the Old 3DS pair the
packets were staged for — until the owner pointed out New 3DS runs existed
and the folder also held old SoulSilver N3DS logs with a similar shape.

**Cause.** The build banner (`Old 3DS: 268 MHz` vs `New 3DS: 804 MHz`) is a
*runtime* detection (`main_3ds.c`, `isNew3ds`), but it was read once and then
assumed for every file in the folder. Filenames date from the console clock
(which is unset) and the rotation name comes from the *current* boot, not the
log's contents — neither identifies the console.

**Rule.** Grep every new log for its banner line before any analysis, and
report findings keyed by file+build+console. Do not infer the console from
speed alone (an Old 3DS build on New 3DS hardware is still the Old 3DS
configuration).

## 18. Proving a dropped include dir with `-E -v`

**Symptom.** 96 game sources failed with missing `dwc/` headers under MSYS
Python while 996 compiled fine with identical flags.

**Cause.** The mingw gcc silently drops an `-idirafter /home/...` directory
(POSIX form untranslated); `-E -v` prints the *resolved* search list, where
the directory is simply absent, while the `C:/` form appears and
preprocesses cleanly.

**Rule.** When a header is present but unfound, print the compiler's resolved
search list before theorizing about flags — it distinguishes a bad path from
a dropped one in one step.

## 19. Baselines that evaporate under rebuilds

**Symptom.** The P0 baseline `.3dsx` files (referenced by manifests and trial
logs) were overwritten by later builds into the same `--out` directories.

**Cause.** Rebuilding in place destroys the artifact the evidence points to.
Manifests pin SHAs, but the bytes were gone; only the logs survived.

**Rule.** Treat a built baseline artifact as read-only: fresh `--out`
directories per experiment (`build/game-3ds-p2{a,b,c}`), and stage packets
(`stage_bench.py` + manifest + `--check-manifest`) promptly after the build
that produced them. Never rebuild into a directory whose artifact is cited
anywhere.

## What went right (keep doing)

- **Claim sheet before code** (P1, P2a/b/c, P3): writing the hypothesis,
  acceptance bars and revert triggers first kept every packet to one question
  and made the P2a revert a one-line verdict instead of an investigation.
- **Stashed-tree baselines**: tool-test failures were judged against a stashed
  run, not the raw count — zero mine-only failures, proven, in one diff.
- **Mechanical accepts**: `p1check.py` turned "sums close" from eyeballing
  into 11/11, 9/9, 8/8 assertions (and caught the take⊃flatfill double count
  when the wall ratio came out impossible).
- **Artifact identity**: manifests pinning artifact/save/script/ini SHAs plus
  `--check-manifest` meant the line-ending normalization, the dirty-tree stop
  and the log-rotation confusion were all decidable from hashes, not memory.
- **Negative results as deliverables**: P2a's revert and the open hardware
  questions went into the record with the same care as the wins.
