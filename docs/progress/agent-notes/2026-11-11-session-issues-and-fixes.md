# Session issues and fixes (2026-11-11)

The second half of the 2026-11-10 Old-3DS performance session
(`000.md`): finishing P2c, then implementing and measuring P3 (the drawer's
flat-picture skip), then answering P4's question and staging the hardware
packets. The point of this note is the next session recognising the shape
faster. The previous session's issues are in
[2026-11-10-session-issues-and-fixes.md](2026-11-10-session-issues-and-fixes.md).

## What this session did

- **P2 closed.** P2c's walk A/B finished (all metrics overlap; the mixer
  never measurably executes in Azahar), so P2 is closed: P2a **reverted**
  (the four geometry flags together drop composes 28-33%), P2b **reverted**
  (no front-region BG3 commit exists in either frozen script), P2c
  **deferred pending hardware** (bit-identical on the host, no functional
  regression, but the accept needs the console). No default was flipped.
- **P3 implemented, and kept.** The flat-picture skip
  (`845cae5`), the VCOUNT proof (`fc2a19b`), the pre-draw VRAM signature
  (`0c00d78`). Measured in Azahar: **600 skipped, 0 redrawn per 600-tick
  window at rest (100%)**, key bytes 7,372,800/115,200 → 0/0, drawer wall
  **2.69 → 0.88 ms/frame**, core 0 idle 41.1% → 51.9%. Four clean A/Bs
  (PROFILE and OFF, field and walk) agree: 2.69-2.72 → 0.87-0.89 ms.
  The diagnostic reports **0 flat disagreements** over boot → title → field.
- **P4 answered early.** The live-VRAM race count is **0** at rest and
  walking (3 draws hashed before and after, 0 writes landed in between),
  so P4's "stop and ask if the count is non-zero at rest" does not trigger.
- **The hardware packets are staged**: four SD layouts with manifests, all
  `--check-manifest` clean, PROFILE and OFF from one clean commit
  (`9759707`) plus the matching P1 baseline pair.
- **Two claims were corrected** rather than left standing: the frozen walk
  script's legs do not walk the player (issue 10), and the OFF numbers
  quoted mid-session came from a contaminated run (issue 9).

| # | Issue | Fixed by |
|---|---|---|
| 20 | A multi-line shell command stopped after its first statement and printed nothing | One command line, `;` separated, or a `.ps1` run with `powershell -File` |
| 21 | A compiler check that used the wrong SDK include path was reported as "clean" | The real include set (`build/hgss-sdk`), and reading the warnings, not the exit code |
| 22 | New diagnostic code compared a pointer with a byte | `((const u8 *)sFlatLastRegs)[b]`; caught by the check from issue 21 |
| 23 | A run manifest said `dirty` although the artifact was byte-identical to a clean one | Read the artifact's SHA-256 in the manifest: `dirty` describes the tree, not the build |
| 24 | The first P3 measurement was "0 skipped", with no way to say why | Per-reason counters in the first implementation, plus the A0 redraw attribution that already existed |
| 25 | The skip still did not fire: one register word changes on every pass | A register-word diff (the AND over the per-pass masks) named it: VCOUNT. It draws nothing, so it is left out of the hash — proved, not assumed |
| 26 | The retained VRAM signature was taken *after* the draw, so the skip could certify a stale picture | The signature is now taken before the draw (as the legacy path does); the race is measured (P4's question) |
| 27 | An A/B labelled "off-field" ran the *walk* script: the runner matched the literal string "field" | Match the whole label, and read the script's hash in the manifest before quoting the numbers |
| 28 | Two emulators ran at once, sharing one `log.txt`: the trial logs and the timings were contaminated | One measurement at a time, a stray-kill before and after each, and a distinct output directory per pair |
| 29 | The "walk" measurement did not walk: a button line is held for **six frames**, so the frozen script's legs are taps | Read the script's own format; a second script (`hg-field-walk2.txt`) holds each direction for 120 frames, and the walking case is measured with it |
| 30 | The capture runner lost the comma between two frame numbers and waited for a frame that never comes | Take the frames as one string and split them inside the script; read the runner's own output |
| 31 | The capture stole the owner's focus by activating the emulator window | `PrintWindow` reads the window without activating it; no fullscreen, no focus change |
| 32 | The A0 build failed with "implicit declaration of NitroPort_HBlankScanCheck" | The shell declares the port's functions locally, not through `nitro_port.h`: the declaration goes there too |
| 33 | Six capture PNGs broke the repository's content gate ("image/audio not on the allowlist") | They are the game's pixels: keep them out of the tree, keep the script (`tools/capture_bench_frames.ps1`) and the descriptions, and say so |

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

## 6. A register that changes every pass: name the word, then prove it

**Symptom.** After issue 25's fix the skip still did not fire — still "why
small" on every pass, but now the *same* word every time.

**Fix.** A diagnostic that compares the register block of this pass with the
one the last flat render used, keeping the **AND** over the per-pass masks
(the words that differ on *every* pass, not the OR over all of them):

```
[a0] flat regdiff: 15858 passes, word mask 0x01000083, bytes0-7 0x42, VRAMCNT 0, POWCNT1 0, every 0x00000002, dispcnt1 0x07 (was 0x07)
```

`every 0x00000002` is word 1 alone — the 0x04-0x07 block — and the byte
mask says the high byte of DISPSTAT never changes, so it is VCOUNT's low
byte: the beam counter, whose value in the snapshot moves with how much the
game did since the V-blank.

**Rule.** The OR over the per-pass masks answers "which words ever differ";
the AND answers "which word always differs". The first is a list to read;
the second is the answer. And a register that always differs is a *claim*
that needs proof before it can be left out of a hash — not a licence to
mask it. The proof for DISPSTAT/VCOUNT: they draw nothing (the port's own
per-line registers leave them out for exactly that reason), the renderer
never reads them (`grep` finds only the masking lines), and nothing else
depends on them. The wiki's case study of this port says it plainly: "Do
not mask a changing register merely to increase hits: first prove which
pixels, metadata and future states depend on it."

## 7. A signature taken after the draw certifies a picture that is not there
**Symptom.** None — this one was found by reading, not by a failure. The
wiki's case study of this port lists it as an open question ("A signature
may cover the right bytes but race a writer between hashing and rendering"),
and the plan's P4 packet asks for the measurement. Reading the code
confirmed it: `RememberSplit()` runs at the *end* of the draw, so the VRAM
signature `FlatStore()` kept described the tiles as they were *after* the
picture was drawn. The game writes tiles from its own thread while the
display thread draws (only the registers, palettes and OAM are taken at the
handover), so a write landing in that window made the stored signature
newer than the picture: the next pass's fresh signature equalled it, the
skip fired, and the stale picture stayed up until the game's next write.

**Fix.** The referenced-range signature is now taken **before** the draw, in
the flat branch, and that one value is both what the skip compares and what
the draw retains. The legacy path already did this (`last[e].vram = vram;`
then `Render(...)`), which is why the bug was P3's alone.

**Rule.** When a signature stands in for "the picture is the same", it must
be taken at a time that is *not newer* than the picture. A hash taken
after the work is a hash of the future. Ask of every retained signature:
"does this describe the state the pixels on the screen came from?" — and if
it does not, take it earlier.

## 8. A label that is matched loosely runs the wrong experiment

**Symptom.** The A/B labelled "off-field" (the OFF pair on the field
script) reported a window of 12000+ — the *walk* window — and its manifest
named `hg-field-walk.txt`. The runner picked the script with
`if ($Which -eq "field") { field script } else { walk script }`, and
"off-field" is not "field".

**Fix.** Match the whole label (`$Which -eq "field" -or $Which -eq
"off-field"`), and read the *script's hash and the frame window* in the
manifest before quoting any number from an A/B.

**Rule.** A runner's label is not metadata: it selects the experiment. When
a result comes back with a different window than expected, the cheapest
check is the manifest's `script` hash and `--from-frame`, not a re-read of
the logs.

## 9. Two emulators, one log file

**Symptom.** The owner noticed two Azahar processes running at once. One
was the current A/B; the other was a stray from the previous one. Both
write to the same `sdmc/3ds/pokeheartgold/log.txt`, and both had been
launched by two A/B runs that also wrote to the same output directory (the
second because its label matched the first's).

**Why it matters.** Two emulators on one log file interleave their output
into each other's trial logs, and they compete for the same CPU — so both
the counts and the timings are contaminated. Neither run can be used, and
the failure is invisible in the summary: the numbers look plausible.

**Fix.** One measurement at a time. Kill any stray emulator before and
after each run, and give every pair its own output directory (`--label`
becomes the directory name, so the labels must differ). The OFF pair was
re-measured from scratch on both scripts.

**Rule.** A measurement harness that shares mutable state with anything
else on the machine (a log file, a device, a window) needs a guard, not a
convention. Check the process list before trusting a run's numbers — and
when the owner spots it, say so plainly: the numbers were contaminated and
are being re-measured.

## 10. A "walk" script that does not walk

**Symptom.** The walk A/B came back with the same numbers as the rest
A/B, and the 3D geometry counters were byte-identical across every leg of
the walk window:

```
[5794]  [gx3d] 120 scenes: 1683600 vtx, 539520 polys -> 113520 tris ...; lost: 482280 clipped, 12960 culled
[12262] [gx3d] 120 scenes: 1683600 vtx, 539520 polys -> 113520 tris ...; lost: 482280 clipped, 12960 culled
```

The camera never moved, so the "walking" measurement was a second rest
measurement — and the session had already reported it as a walking one.

**Cause.** `hgss/game/app/3ds_input.h` documents the format: *"A button
line holds its buttons for six frames, exactly like the PC runner's
--press. `every` is ignored on the 3DS (it only controls how often the PC
writes screenshots) but is kept so the same file works on both."* The
frozen walk script's `every: 10` is a PC-runner setting; on the 3DS each
leg is a **6-frame tap** (0.1 s). A D-pad tap in a DS game turns the
player; it does not walk them.

**Fix.** A second script, `tests/inputs/hg-field-walk2.txt`, holds each
direction for 120 frames (twelve entries ten frames apart). With that the
player walks: the clipped and culled counts move with the camera and the
translucent triangle count rises (the characters move), and the captures at
frames 11400 and 17400 show two different parts of Goldenrod. The frozen
script stays frozen and is reported as what it is; the walking case gets
its own script and its own numbers. The result: **the skip still fires on
600 of 600 passes while walking** (the field's ground and characters are
3D, so the top screen's 2D layers do not change — `why scroll 0`).

**Rule.** Before measuring "what happens while X happens", check that X
happens in the measurement. A script that reads as a walk on the page (a
direction per leg) can be a tap. The tell is a counter that *should* move
and does not.

## 11. A runner parameter that lost its comma

**Symptom.** The capture watcher printed `MISSED frame 1380014400` — it
had been asked for one frame, 1380014400, which of course never comes.

**Cause.** The two frame numbers were passed as one argument and arrived
joined. The script's own output said exactly what was wrong; the mistake
was not reading it before concluding that the capture was broken.

**Fix.** Take the frames as one string and split them inside the script
(`$FrameList = $Frames.Split(',')`), and read the runner's output when a
capture does not appear.

## 12. The capture stole the owner's focus

**Symptom.** The capture helper called `SetForegroundWindow` on the
emulator before grabbing the screen — on a machine the owner was using for
other work. The owner's instruction: never fullscreen it.

**Fix.** `PrintWindow` asks a window to render itself into a bitmap
without being activated: no focus change, no fullscreen, the owner's
windows and work untouched. The image comes out with the emulator's render
surface in the lower right (the window is wider than the surface), so the
artifact is cropped when it is saved.

**Rule.** A measurement harness runs on someone's desktop. It may *read*
the screen; it must not *take* it. Never activate a window to look at it,
and never take the whole screen for a windowed app.

## 13. A START press that does nothing — not a defect after all

**Symptom.** A scripted START at frame 14000 changed no counter at all:
the OAM counts, the 2D counters and the drawer's "drawn" count are
identical at 13800 and 14400, and the capture at frame 27000 still shows
the field. It looked like a broken input path.

**Resolution.** The owner: *the game does not use START for any menus, it
uses X.* So the menu did not open because START is not the menu button in
this game — the measurement was right and my expectation was wrong. The
input path is fine, and the proof stands: the title screen is drawn every
frame through frame 8400 and the field is up by 9000, exactly after the
scripted A presses at 8000-8049.

**Rule.** "The scripted press did nothing" has three possible causes —
the input path is broken, the button is not the one the game listens for,
or the game ignores it in that state — and the cheap way to tell them
apart is to ask what the game *does* use. (And to check the effect the
press was supposed to have: a press with no effect cannot be told apart
from a press that was never delivered.)

## 10. A screenshot of the game is game content

**Symptom.** The repository's own content gate (`tools/check_repo_content.py`)
failed the six capture PNGs with "image/audio not on the allowlist" — the
allowlist is for original Platinum/shell assets, "not game content".

**Resolution.** The captures *are* the game's pixels (the field's tiles, the
party HUD), so the rule says what it says: they do not go in the tree. They
were removed from the index and from the working copy; the script that
takes them (`tools/capture_bench_frames.ps1`) and the descriptions of what
each one shows went in instead, so the evidence is reproducible on demand
rather than stored.

**Rule.** "The plan wants captures" does not override the repository's
content gate — the gate is there so the tree never holds the game's assets.
When the two disagree, the evidence goes *outside* the tree (a script, a
description, a log line) and the gate stays. Run `tools/check_repo_content.py`
before committing anything binary.

## Wiki feedback (what the coffeepot wiki still lacks, from this session)

The wiki's CTR section was re-read after the owner's new commits (`30f8c93`,
`9582a97`, `56a8dc7`, `869bb68`) and the new material agrees with P3: the
"worked invalidation and scanline plan" (2D rendering) asks for exactly what
P3 does — key the cache by content, invalidate conservatively, keep the
software path as the oracle — and the testing page's "record attempts,
successful takes, rejection reasons and work removed" is the discipline the
flatskip counters follow.

Three things the wiki could take from this session:

1. **The pre-draw-hash rule.** The case study of this port lists the
   live-VRAM race as an open question ("the notes do not establish its
   frequency or a safe general fix"). Both are now answered: the frequency
   is **0** (3 draws hashed before and after, 0 writes landed in between, at
   rest and walking), and the general fix is an ordering rule: *a signature
   that stands in for "the picture is unchanged" must be taken no later than
   the draw, never after it* — a hash taken after the work describes the
   future, can match the next pass's fresh hash, and then certifies a
   picture that is one write behind. The same rule applies to any DS port:
   the tiles are written by the game's thread while a renderer reads them.
2. **A register block that contains a counter or a status flag cannot be
   hashed whole.** The DS's DISPSTAT/VCOUNT (and the 3DS's equivalents) hold
   values that depend on *when* they are read, not on what was drawn; hashing
   them makes every frame look changed. Hash only the registers that
   describe the drawing, and prove the rest is never read (this port's
   per-line registers already exclude 0x04-0x07 for that reason). The wiki's
   2D page says this about textures ("key every cached texture by tile data
   and effective palette"); the same rule for the *register block* is not
   stated anywhere.
3. **The case study's P3 entry is now stale.** It says the changing
   register's significance and the race's frequency "remain open". The
   register was VCOUNT, its significance is "none — it draws nothing", the
   skip now fires on 100% of the top passes at rest, and the race count is
   0. The hardware gain is still open (the run is pending).

## What went right (keep doing)
- **The premise was checked before the change.** The field's top pass really
  is flat (`0 lines, 0 empty, 0 blend, rows 0/0`, `gpubg taken 1200`,
  `region 0/600`), so "the skip does not fire" means the blocker is
  elsewhere, not that the picture was never flat.
- **The existing instrumentation was reused instead of replaced.** The A0
  attribution answered "which input changed" without a new framework.
- **The tool-test baseline was measured on a stashed tree**, not assumed:
  517 tests / 54 environment failures before, 525 tests / the same 54 after
  — zero new failures, proven by name, not by counting. (The later
  re-measurement, after the race fix, says 526 tests and **47** distinct
  failing names: the same set by name — the earlier "54" also counted four
  stderr lines that a case-insensitive `Select-String` swept up. The names
  are the comparison; the count is not.) The last measurement of this
  session: **529 tests, the same 47 failing names** (the four extra are
  P3's and P6's own tests).
- **One change per commit.** P2c's verdict (documentation) and P3's
  behaviour change are separate commits, so the P3 artifact is cited against
  a clean parent.
- **The P2c walk A/B was finished rather than left half-done**, and its
  result (every metric overlapping, the mixer never measurably executing in
  the emulator) is recorded as a *deferred* verdict, not as a pass or a
  failure.
- **The wiki was read for the code, not for the report.** The case study of
  this port names the live-VRAM race as an open question for this project;
  reading the code against that sentence found the stale-picture bug in the
  new skip before it was ever measured. The owner's "read the wiki" rule
  (`AGENTS.md`) earns its keep here.
- **The A/B's own counters were explained instead of waved away.** The table
  said "base better" for `composes/s` and `publishes/s`, which looked like a
  regression. The compositor's cause census (`cause scene 176 set 31` vs
  `cause scene 177 set 0`) showed the falling numbers were the set-driven
  composes — the ones that existed only because the 2D kept changing. The
  scene-driven rate (the 3D being presented) was unchanged. A number that
  falls is either a win or a loss; the cause counters say which.
- **The wiki was re-read when it grew, and the new text was checked against
  the code.** The owner's new commits (a `case-studies/` split, the
  "worked invalidation and scanline plan" section in the 2D page, the
  "attempted / admitted / presented" capture discipline in the testing
  page) agree with what P3 does; the one place the wiki is now out of date
  is the P3 entry in the case study, which still says the changing register
  and the race's frequency are open. Both are answered now — the register
  was VCOUNT and the race count is 0 — and that is the wiki feedback to
  hand back.
- **The owner's doubt was checked instead of defended.** "I have no idea if
  your simulated button presses are actually working" led to reading the
  script's own format, finding the six-frame hold, and **correcting a
  measurement that had already been reported** — the claim sheet, the
  results record and STATE.md all say now that the frozen walk script does
  not walk the player. A correction that goes into the documents is worth
  more than a defence.
- **The proof that the presses work does not depend on the window.** The
  title screen → field transition is in the log, at the frame the script
  says it should be. The owner could not see it in the emulator window
  (Azahar's menu bar, a small render surface); the log could be checked
  by anyone, at any time.
- **The walking case got its own script, not a reinterpretation.** The
  frozen script stays frozen and is reported for what it is; `hg-field-walk2`
  is a separate, committed input with its own numbers, its own captures and
  its own answer (the skip still fires on 600 of 600 passes).
- **The hardware run is one copy-paste away.** Four SD layouts, staged
  with the repo's own tools, each with a manifest that names the artifact's
  SHA-256, the commit and the exact save/script/ini — and
  `tools/bench_report.py --check-manifest` passes for all four. The owner
  does not have to rebuild anything to run them.
- **The open items are named as open.** The START press that does nothing,
  the legacy reuse record's same-shaped exposure (a pre-existing path, not
  touched), the hardware gain (not measured) — each says so in the place a
  reader would look for it, instead of being quietly dropped.
- **P6 followed the wiki's own instruction for this kind of change.** "Time
  dispatch, work and scheduling separately before changing them" — the
  sub-timers already existed (P1), so the packet started by *reading* the
  split rather than by guessing: the scan, not the dispatch, was the part
  that could go. The change then touched nothing the wiki warns about: no
  dispatch skipped, no interrupt reordered, nothing the game observes
  changed, and the register array's contents are bit-for-bit what they were
  (the A0 diagnostic computes both ways of answering the question on every
  walk: 7,826 walks, 0 disagreements).
- **The measurement that decides a packet was found before the code was
  written.** P6's claim sheet was written from the console's own sub-timers
  (irq 831 / cmp 768 / keep 483 of a 2639 µs phase), and the change was
  aimed at the second number. It was then measured against a baseline run
  in the same session, not against a number from an earlier one.
