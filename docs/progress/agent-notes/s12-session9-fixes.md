# S12: frame cap, audio noise offset, camera-point 3D (session 9)

This note is the audit trail for the three fixes after the ninth hardware run
(log-9) and for the process problems hit while landing them. The living state
is [STATE.md](../STATE.md); the user-facing rules are in
[3DS-Pitfalls.md](../../wiki/3DS-Pitfalls.md).

## What log-9 showed

- **87.0 Hz.** `tools/frametime_report.py`: 40 samples, mean `total_us`
  **11.49 ms**, `logic_us` under 1 ms. The emulated DS clock is the V-blank
  raised once per `NitroPort_OnFrame`; `gfxSwapBuffers` only waits while the
  previous frame is still presented, so once the composite fit under one
  refresh the loop free-ran. Cutscenes finished early.
- **The title Ho-oh had a gap** after the S10 drop workaround; the S10 photo's
  black fan was gone but the drop had removed real geometry.
- **Audio hung a note** until the next note; individual sounds and slower
  songs/percussion distorted.

## Fix 1: 60 Hz frame cap (`952bdf2`)

`NitroPort_OnFrame` ends with `WaitForFrameCap`: frame starts are held to a
`SYSCLOCK_ARM11 / fps` grid from `svcGetSystemTick()`, and only the remainder
is slept with `svcSleepThread`. It is a deadline, not a second V-blank: a frame
that already overran returns immediately, and more than one period behind
re-anchors the grid rather than bursting to catch up. `DrawFrame`, both blits
and `gfxSwapBuffers` stay unconditional and before the wait.

`port.ini`'s `fps` accepts **60** (default) or **30**. 30 is a comparison mode
and halves the game's speed, because the game clock advances once per frame;
invalid values use 60. Startup logs `[perf] frame cap 60 Hz`.

Issue met: the first implementation was reviewed against a build whose result
arrived after the agent had finished; the build was still running. It was
waited out and verified (9,278,584 B at that stage).

## Fix 2: audio noise-channel offsets, patch 1407 (`2110dc8`)

Patch 1405 moved `NitroPort_SoundRegisters` from "relative to 0x400" to the DS
low 16-bit address (channel *c* at `0x400 + 16c`, timer at `0x408 + 16c`). It
fixed `SND_SetupChannelPsg`, `SND_StopChannel` and the surround helper but
missed `SND_SetupChannelNoise`'s two direct writes, which still used
`[channelOffset]` and `[8 + channelOffset]`. Those are DS
`0x040000E0`/`0x040000E8` after the remap, an unused hole, so a noise note left
the channel's previous PCM control word (enable + repeat) in place. The mixer
kept retriggering the loop until a later PCM setup: the hung note.

Patch **1407** writes `0x400 + channelOffset` / `0x408 + channelOffset`;
`tests/sdk/arm7_sound` leaves a running looped PCM on 14/15, runs the noise
setup, and checks the real registers. Without the patch: 24 failures; with it:
**122 checks, 0 failures**.

Issue met: the first audio agent's final `build/upstream` was transiently at 77
patches while `patches/` had 78 (a clean/bootstrap race). The reported passing
test/build was discarded; `bootstrap.py --clean` was re-run, `rev-list` showed
78, the noise writes were verified at `0x400`, and the tests/build were re-run.
The 3DS artifact is 9,278,592 B / `d6c56cf5…` at that stage.

The **DS gate at 78** then passed for both games (HG
`4fcded0e2713dc03929845de631d0932ea2b5a37`, SS
`f8dc38ea20c17541a43b58c5e6d18c1732c7e582`), proving the `#else` DS text is
unchanged.

## Fix 3: the camera-point 3D vertex (port, `920c573`)

The S10 workaround dropped every stored triangle with a `w <= 0` vertex. It
removed the black fan, but the title photo then showed a transparent gap
between Ho-oh's wings and body, and the log's `camera-plane drops` reached
**80,411** in the title (0 in the field).

The stopped 3D investigator's scratch diagnostic (`/work/diag-title.txt`,
`/work/diagpc/`) showed every dropped triangle is opaque, textured, cull-both,
and has one vertex at clip `(0,0,0,0)` with valid fixed `s/t/colour`
(`s=-16.4, t=15.6, colour=0x18c6`). A scratch instrumented port counted the
`BEGIN_VTXS` primitives for those vertices over the title run: **quads 17,016,
quad strips 22,688, triangle strips 0, triangle lists 0**. The zero vertex is a
real model-space point transformed to the camera (mechanism A), not a
strip-restart sentinel. The PC reference and the DS put it at screen `(0,0)`
and draw the fan from that corner.

The PICA cannot divide by `w = 0`. A viewport-centre mapping was tried and
rejected: it moved the fan and cut Ho-oh's left wing off, changing the
software frame. The final fix is one hunk in
`3ds_port/nitro/src/port_gx3d.c` `to_screen()`: for the exact `(0,0,0,0)`
vertex, store `clip[3] = 1` (20.12) and leave the screen position, depth and
`inv_w` unchanged. `put_vertex()` then gives the GPU `w = 1/4096`, NDC
`(-1,+1)`, valid texture/colour; the PICA draws the fan, and the software
reference at title frame 4800 is byte-identical (`7d7ea8f5…`).

`test_camera_point_vertex` in `3ds_port/nitro/tests/test_gx3d.c` covers the
synthetic case. The HGSS drop workaround (`pica_scene.c`, its app-source line,
the `GpuMain` call, the `camera-plane drops` fields, `tests/game/pica_scene/`)
is removed. `3ds_port/` is deliberately no longer byte-identical to
`platinum.lock`; the upstream PR content is the one hunk plus the test.

## Process issues from the session

- **A shared-tree `git checkout -- hgss/game/app/main_pc.c`.** Another agent
  ran it while this work was in progress. The file carried only an abandoned
  `--render-3ds` experiment; it is present again, 27 insertions/1 deletion, and
  none of this session's commits touch it. Nothing of the port work was lost,
  but the outcome was luck. Rule added to
  [Contributing](../../wiki/Contributing.md): no mutating git commands in a
  shared worktree; stage explicit paths.
- **Subagents were stopped mid-investigation** after the checkout alarm. Both
  had changed no tracked files; their scratch evidence was recovered from
  `/work` and used for the audio and 3D fixes.
- **The `build/upstream` 77/78 race** above. The lesson is in
  [Daily-Workflow](../../wiki/Daily-Workflow.md): verify
  `python3 tools/bootstrap.py` and
  `git -C build/upstream rev-list --count <pin>..HEAD` yourself before trusting
  a build or a test count from another session.
- **The SDK-suite count was documented as 21.** There are 20 `run.sh` suites;
  the 21st `tests/sdk/` entry is `port_memory.c`, a helper. The current docs
  say 20.

## Verification

- `make test`: **334 Python tests, OK**, 20 SDK suites, 0 failed.
- `make port-tests`: **exit 0**; gx3d **337 + 239**, nitrosystem **17,122
  checks, 0 failed**.
- host32: 1092/1092, 0 unresolved.
- 3DS: 1092/1092, 0 unresolved, `.3dsx` **9,278,120 B**, sha256
  **3f4936616ff68e32413b6b2c2e3a1630b8c27cc9a5e0a36b079e1f5aa9fb7f00**,
  `3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages).
- DS gate at 78: HG and SS retail SHA1s, `sha1sum -c` OK.
- PC title reference at frame 4800: byte-identical to the clean reference.

## Pending the owner's hardware run

- Frame times near 16,700 us (60 Hz), not 11,500.
- No hung audio note; `audio: N dropped frames of M` near zero.
- Title Ho-oh without the black fan and without the transparent gap; the fan
  is textured.
- `3D images:` near 300 per 600 frames; walking animates.
- The standard 2D/APT/save checks.
