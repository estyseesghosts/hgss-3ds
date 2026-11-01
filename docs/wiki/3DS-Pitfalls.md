# 3DS shell pitfalls

Everything on this page was found on hardware, in one session, in this order.
Each entry is a symptom, the cause, the fix, and the rule to keep. The shells
are `hgss/game/app/main_3ds.c` (3DS) and `main_pc.c` (PC); the port layer
(`3ds_port/`) is vendored and byte-identical, so all of this is shell-side
unless stated.

Read this before changing the display, the audio output, or the startup
sequence.

| Symptom | Entry |
| --- | --- |
| Luma crash dump, pc in a process called `loader`, no `log.txt` | [A weak hook binds to NULL](#a-weak-hook-binds-to-null) |
| Loader aborts with `udf #0` on a `.3dsx` that links fine | [One relocation held `symbol - 0x06800000`](#one-relocation-held-symbol---0x06800000) |
| Program stops at `frame 0` with the game thread still starting | [APT has one owner](#apt-has-one-owner) |
| `threadJoin` says the thread ended while it is still running | [threadJoin does not mean "ended"](#threadjoin-does-not-mean-ended) |
| Log lines missing, or out of order, after a race | [The log needs a lock](#the-log-needs-a-lock) |
| Black screens, `draw_us` of 1 us | [A weak hook binds to NULL](#a-weak-hook-binds-to-null) |
| Flicker; one screen alternating current/stale | [The blit may never be skipped](#the-blit-may-never-be-skipped) |
| 3D scene black while 2D text draws | [Only engine B may skip composition](#only-engine-b-may-skip-composition) |
| Black 3D, `3D images: 0` in the log, scene-builder lines present | [The 3D needs a GPU worker](#the-3d-needs-a-gpu-worker) |
| Log text drawn over the game's bottom screen | [No console on the game's screens](#no-console-on-the-games-screens) |
| 13 Hz with drawing at 78 ms | [Ask for the New 3DS clock](#ask-for-the-new-3ds-clock) |
| Silence with `NDSP started` in the log | [NDSP must be fed](#ndsp-must-be-fed) |

## A weak hook binds to NULL

**Symptom.** The game runs (frames advance, 3D pipeline stats in the log) and
the screens stay black. `draw_us` is 1 microsecond — `DrawFrame` returned
early.

**Cause.** The shell declares the port's hooks by hand (it cannot include
`nitro_port.h`, whose NitroSDK types clash with libctru's), and
`NitroPort_RenderScreens` was declared `__attribute__((weak))`. A weak
*undefined* reference does not make the linker extract the definition from
`libnitro.a`: it binds to NULL and the call is skipped. The relic:

```text
final-3ds5 elf:  w NitroPort_RenderScreens   (weak undefined)
libnitro.a:      T NitroPort_RenderScreens   (defined, never extracted)
```

The PC shell links it strongly through `nitro_port.h`, which is why captures
worked and only the console drew nothing.

**Fix.** Every port hook is a strong declaration with `nitro_port.h`'s
signature; a test fails if one is declared weak again. If the port ever stops
providing a hook, the build now fails loudly.

**Rule.** A vendored-static-archive symbol that the program must call is never
weak. Make the link break instead.

## One relocation held `symbol - 0x06800000`

**Symptom.** The `.3dsx` links, boots, and the console aborts in the Loader:
Luma's dump shows an **undefined instruction** (`udf #0`) in the process named
`loader`, with a pc far from any game code, and no `log.txt` (the program
never started).

**Cause.** `LcdcAddress()` (patch 1406) computed
`NitroPort_LcdcVram + (address - 0x06800000)`. GCC reassociates that to
`(NitroPort_LcdcVram - 0x06800000) + address` and folds the constant into a
literal-pool word: an `R_ARM_ABS32` with addend `-0x06800000`, resolving to
`0xFA607000` at ELF `0x008e5a20`. `3dsxtool` treats any relocated word at or
above the image base (`0x00100000`) as an absolute pointer, so it packed a
pointer about 4 GB outside the image. `3dsxdump` refuses the same file with
`Cannot write segment data!` (return 7).

The PC build, every host test and the DS gate all pass with that word in
place — nothing but the console looks at the `.3dsx`.

**Fix.** Patch **1903** reads the base through a `noinline` call so the
subtraction cannot fold across it, and `tools/hgss_game.py` runs
`validate_3dsx()` on the packaged file, failing the build if a loader check
would reject it.

**Rule.** Never let GCC fold a symbol together with a negative constant into a
relocatable word. Validate the packaged artifact, not just the ELF.

## APT has one owner

**Symptom.** The program logs `starting the game` and then
`stopping (frame 0)`; `NitroMain` never returned, `OS_Terminate` never ran.

**Cause.** `WaitForGame` polled `aptMainLoop()` 100 ms after starting the game
thread, while overlays were still loading, and got `false`.
`aptMainLoop()` is `aptHandleSleep(); aptHandleJumpToHome(); return
!aptShouldClose();` and libctru documents it as a per-frame call, from one
caller.

**Fix.** The game thread's `NitroPort_OnFrame` is the only APT caller; it logs
`the HOME menu asked to close (frame N)`, flushes the card and leaves its
thread so teardown stays ordered. `WaitForGame` only joins. Platinum's shell
does the same (`Watch()` waits on an event, it never polls APT).

**Rule.** One thread owns APT, once per frame.

## threadJoin does not mean "ended"

**Symptom.** `the game's thread ended at frame 0` while the thread is
demonstrably inside `NitroMain` (its log lines follow).

**Cause.** libctru:

```c
Result threadJoin(Thread t, u64 timeout) {
    if (!t || t->finished) return 0;          /* success, without waiting */
    return svcWaitSynchronization(t->handle, timeout);
}
```

Success means `Thread.finished` was already set — not that the thread returned.
Nothing in the port or the shell sets it in that case.

**Fix.** `GameThread` sets `sGameThreadAlive` on entry and clears it after
`Heartgold_Run`; `WaitForGame` polls *that*, and warns if the join disagrees.
The game thread's stack also went 512 KiB → 1 MiB, because libctru allocates
the `Thread` block at the base of the same allocation the stack grows down
into, so an overflow clobbers the struct the join reads.

**Rule.** Do not use a library's "did it finish" as your own liveness. Keep a
flag the thread itself writes.

## The log needs a lock

**Symptom.** A log line that must exist is missing, or appears after a line
that should follow it.

**Cause.** Two threads write `log.txt`, and stdio is not reentrant. One
debugging round was spent on an ordering that was an artifact.

**Fix.** `NitroPort_Log` takes a `LightLock`; the exception handler writes
without it (it must not block).

**Rule.** Every log line goes through one lock, or the ordering is not
evidence.

## The blit may never be skipped

**Symptom.** Flicker in the intro; in the field, one screen looks stale or
black.

**Cause.** The 3DS framebuffer is **double buffered**, and the shell skipped
the *blit* of an engine whose picture had not changed:

- frame N: copy into buffer A, swap, display A
- frame N+1: no copy, swap, display buffer B — still holding the picture from
  two frames ago

Every skipped screen alternates current/stale at 60 Hz.

**Fix.** `DrawFrame` copies both screens every frame, unconditionally. Only the
*composite* (`NitroPort_Render2DIfChanged`) may be skipped; the copy is what
makes each buffer whole.

**Rule.** The display's swap is also the clock the emulated DS runs on — it
stays unconditional too (the port raises one V-blank per `OnFrame`).

## Only engine B may skip composition

**Symptom.** The 3D environment is black while 2D text draws correctly — the
house in the photo, with the dialogue box visible and no room behind it.

**Cause.** Engine A is the only engine that can show the 3D layer, and a new
3D image is a change even when engine A's 2D registers are identical.
`NitroPort_Render2DIfChanged` knows this, but only when its
`(dispcnt & 0x0108) == 0x0108` test holds, and it does not for this scene.
Watching `NitroPort_Gx3dImageCount()` from outside does not work either: the
port publishes images *lazily*, as part of a composite request, so the counter
does not move before the decision is made.

**Fix.** Engine A is composed every frame, exactly as
`NitroPort_RenderScreens` does. Engine B — which cannot show 3D — is the only
one skipped when unchanged.

**Rule.** Skip composition only where the skipped engine physically cannot
carry the content you would lose.

## The 3D needs a GPU worker

**Symptom.** The 3D environment is black while the 2D layers (dialogue box,
touch panel) draw correctly. `log.txt` carries `[gx3d] ... scenes: N vtx`
lines and `3D images: 0`, and `NitroPort_Gx3dImageCount()` never advances.

**Cause.** On the PC, `NitroPort_Gx3dAcquireImage` rasterises the newest scene
as part of acquiring it. On the 3DS it does **not**: it can only convert an
image that a GPU worker handed over with `NitroPort_Gx3dSubmitRGBA8`. The HGSS
shell had no worker, so `sPublished` stayed `-1`, `AcquireImage` returned NULL,
and engine A composited only its 2D layers. The scene-builder lines prove the
geometry ran; they do not prove anything was drawn.

**Fix.** Start Platinum's classic pipeline: a geometry thread
(`NitroPort_Gx3dStartThread` + `NitroPort_Gx3dThreadMain`, one
`NitroPort_Gx3dFlush` per frame), and a GPU thread that runs
`NitroPort_PicaInit`, then acquires scenes, calls `NitroPort_PicaRender`,
releases them and converts the handed-over image. `NitroPort_PicaRender` is in
`3ds_port/game/app/port_gx3d_pica.c`; its shaders are Platinum's `.v.pica`
files assembled with `picasso` and embedded with `bin2s` (both already in the
container). The GPU thread is woken from the port's `NitroPort_Gx3dScenePublished`
callback, which is weak by default. If PICA init or either thread fails, the
shell logs it and stays 2D rather than crashing.

**Rule.** An image API fed by a worker needs the worker started, not only its
consumer called. Log the image counter; a scene counter is not an image
counter.

## No console on the game's screens

**Symptom.** Scrolling log text drawn over the bottom screen, once the
unchanged-screen repaint was removed.

**Cause.** `consoleInit(GFX_BOTTOM, NULL)` at startup, plus `NitroPort_Log`
writing to `stderr`, which the console draws on screen.

**Fix.** The console comes up only for the "can't start" screen
(`PakErrorScreen`).

**Rule.** On the 3DS, `stderr` is a screen. Do not leave a console up while the
game owns the display.

## Ask for the New 3DS clock

**Symptom.** 13.2 Hz, with `draw_us` at 78 ms and the game at 0.4 ms.

**Cause.** The shell never called `osSetSpeedupEnable(true)` or
`PTMSYSM_ConfigureNew3DSCPU(3)`, so the program ran at the Old-3DS clock with
the L2 cache off while doing a memory-bound software composite.

**Fix.** Both calls, before `gfxInitDefault` (no-ops on an Old 3DS), and a log
line saying which mode was requested. That alone took the same scenes from
13.2 Hz to 35.7 Hz.

**Rule.** A New 3DS port must ask for the New 3DS clock; nothing does it for
you.

## NDSP must be fed

**Symptom.** `NDSP started` in the log and complete silence.

**Cause.** `ndspInit()` succeeds, and nothing is ever submitted to the DSP.
The DS mixer side already exists: `NitroPort_SoundRender(int16_t *stereo,
size_t frames)` mixes the ARM7's 32768 Hz stereo.

**Fix.** `AudioThread` renders 512-frame blocks into four NDSP wave buffers on
its own thread (priority below the game's); `NitroPort_OnFrame` pumps the
ARM7's alarm replies with `NitroPort_SoundPumpReplies()`. The channel runs at
32768 Hz against NDSP's ~32728.5 Hz (about two cents sharp, inaudible) and the
DSP resamples. The exit log prints `audio: N dropped frames of M`, which is
how starvation shows.

**Rule.** Initialising an output device is not connecting it. Log a counter
that proves it was fed.

## Reading a Luma crash dump

`crash_dump_*.dmp` on the SD card starts with `DEADC0DE DEADCAFE` and follows
Luma's `ExceptionDumpHeader`:

| Offset | Field |
| --- | --- |
| `0x00` | magic `DEADC0DE`, `DEADCAFE` |
| `0x08` | version major/minor (u16 pair), processor, core |
| `0x10` | exception type, total size, register size, code size, stack size, extra size |
| `0x28` | 23 registers: `r0-r12`, `sp`, `lr`, `pc`, `cpsr`, then banked words |
| after | code window (96 bytes ending at `pc`), stack window, then the process name (8 bytes) and title id |

`type` is `0` FIQ, `1` undefined instruction, `2` prefetch abort, `3` data
abort. The pc is already adjusted to the faulting instruction, so
`arm-none-eabi-addr2line -e <elf> <pc>` maps directly — *if the process is
yours*. A fault in a process named `loader` with a pc outside your image means
the console rejected the program before it ran; look at the artifact, not the
game.
