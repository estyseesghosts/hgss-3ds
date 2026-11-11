# Session issues and fixes (2026-11-11)

The second half of the 2026-11-10 Old-3DS performance session
(`000.md`): finishing P2c, then implementing and measuring P3 (the drawer's
flat-picture skip). The point of this note is the next session recognising
the shape faster. The previous session's issues are in
[2026-11-10-session-issues-and-fixes.md](2026-11-10-session-issues-and-fixes.md).

| # | Issue | Fixed by |
|---|---|---|
| 20 | A multi-line shell command stopped after its first statement and printed nothing | One command line, `;` separated, or a `.ps1` run with `powershell -File` |
| 21 | A compiler check that used the wrong SDK include path was reported as "clean" | The real include set (`build/hgss-sdk`), and reading the warnings, not the exit code |
| 22 | New diagnostic code compared a pointer with a byte | `((const u8 *)sFlatLastRegs)[b]`; caught by the check from issue 21 |
| 23 | A run manifest said `dirty` although the artifact was byte-identical to a clean one | Read the artifact's SHA-256 in the manifest: `dirty` describes the tree, not the build |
| 24 | The first P3 measurement was "0 skipped", with no way to say why | Per-reason counters in the first implementation, plus the A0 redraw attribution that already existed |

## 1. A multi-line command stops after its first statement

**Symptom.** A `Select-String` over the six walk logs reported no
`[perf] audio` line and no `[sndprof]` line — which would have meant "the
audio counters are absent from these logs", a very different claim from "the
command never ran". The tell: the script printed *nothing at all*, not even
the `=== base-t1 ===` headers it was supposed to print between the loops.

**Cause.** The harness runs a PowerShell command; a command written with
newlines between the statements executed only the first one, and the rest
were dropped. The same happened to a `git add` + `git commit` pair (the add
worked, the commit did not) and to a devkitARM syntax check that was
therefore "silently clean".

**Rule.** One statement per line, separated by `;`, or put the script in a
`.ps1` file and run it with `powershell -NoProfile -ExecutionPolicy Bypass
-File`. A command that should print *something* and prints nothing has
failed, not found nothing: re-run it as one line before believing the
result. The walk logs' audio line is there (`[perf] audio: 0.0% of core 0
(0.0 ms mixing in 10.0 s), 0 dropped of 0`); the same command with
`Get-Content` instead of `Select-String` finds it.

## 2. A compiler check with the wrong include path

**Symptom.** `arm-none-eabi-gcc -fsyntax-only` on `port_render2d.c` printed
nothing, which I read as "no warnings". It had actually failed to find
`sections.h` — the error was swallowed by issue 20.

**Cause.** The SDK the port builds against is not `build/NitroSDK` (that
directory has the headers but no `libraries/`); it is `build/hgss-sdk`,
whose `libraries/hgss` is a symlink to `build/upstream/lib/NitroSDK/src`
(what `tools/hgss_nitro.py` sets up). The include set that works is:

```text
-I3ds_port/nitro/include -I3ds_port/nitro/build/3ds/gen
-Ibuild/hgss-sdk/include -I3ds_port/pak/include
```

**Rule.** A syntax check that reports nothing is not a check. Run it as one
command line and look at the output: on this tree the compiler immediately
prints two pre-existing `%x`-vs-`u32` warnings in `port_render2d.c` (the
`[2d] gate dsp:` and `[2d] gate kind:` logs), which is how to know it
really ran.

## 3. A pointer where a byte was meant

**Symptom.** The register-word diagnostic (added to name the word that
changes on every flat pass) compared `aio[b]` with
`(const u8 *)sFlatLastRegs[b]` — a pointer cast applied to an index, not to
the array. GCC warned: "comparison between pointer and integer".

**Fix.** `((const u8 *)sFlatLastRegs)[b]`.

**Rule.** The diagnostic that explains a negative result deserves the same
care as the change it explains; a broken diagnostic is worse than none,
because it produces a confident wrong answer.

## 4. `dirty` in a manifest is about the tree

**Symptom.** The P2c walk A/B's manifest reads `commit e4943ff-dirty`,
because the P3 work was in the working tree while that A/B ran, and the
P2c claim sheet's own field A/B (run earlier) reads `dirty: no`.

**Cause.** `tools/hgss_game.py`'s build info marks any non-empty
`git status --porcelain` as dirty. It describes the repository state at
build time, not the artifact.

**Rule.** Compare the artifact's SHA-256 in the manifest with the one the
clean run cites. Here they are the same (`dc183349…`), so the walk A/B's
numbers are usable; the manifest's `dirty` flag is a statement about the
checkout, and it is the artifact that the measurement is about. Keep the
tree clean anyway for anything that is staged as a hardware packet.

## 5. A negative result must explain itself

**Symptom.** P3's first measurement: `0 skipped, 600 redrawn` per 600-tick
window. With only the counters the plan names (skipped / redrawn /
not-modelled) the session would have had to guess.

**Fix.** The implementation counted *why* each pass did not skip (small /
vram / scroll / buffers) from the start. That turned "0 skipped" into "the
small signature differs on every pass", and the A0 redraw attribution that
already existed in the repository (`sA0Redraw.regsChanged/paletteChanged/
oamChanged/layoutChanged/miscChanged`, printed as `[a0] redraw A:`) then
split "small" into registers / palette / OAM / layout / per-line registers
with one A0-only line of new code.

**Rule.** A counter that can only say "no" is not evidence; a counter that
says "no, and this is why" is. When a packet's accept is "the path fires",
the counters that decide it have to name the blocker.

## What went right (keep doing)

- **The premise was checked before the change.** The field's top pass really
  is flat (`0 lines, 0 empty, 0 blend, rows 0/0`, `gpubg taken 1200`,
  `region 0/600`), so "the skip does not fire" means the blocker is
  elsewhere, not that the picture was never flat.
- **The existing instrumentation was reused instead of replaced.** The A0
  attribution answered "which input changed" without a new framework.
- **The tool-test baseline was measured on a stashed tree**, not assumed:
  517 tests / 54 environment failures before, 525 tests / the same 54 after
  — zero new failures, proven by name, not by counting.
- **One change per commit.** P2c's verdict (documentation) and P3's
  behaviour change are separate commits, so the P3 artifact is cited against
  a clean parent.
- **The P2c walk A/B was finished rather than left half-done**, and its
  result (every metric overlapping, the mixer never measurably executing in
  the emulator) is recorded as a *deferred* verdict, not as a pass or a
  failure.
