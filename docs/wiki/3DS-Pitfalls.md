# 3DS shell pitfalls

Everything on this page is a hardware symptom or a defect found while
bringing the hardware up. Each entry is a symptom, the cause, the fix, and
the rule to keep. The shells
are `hgss/game/app/main_3ds.c` (3DS) and `main_pc.c` (PC); the port layer
(`3ds_port/`) is vendored from Platinum and carries one recorded 3D fix (the
camera-plane triangle drop), so all of this is shell-side unless stated.

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
| 3D shows but updates about once a second; `3D images:` barely moves | [A worker on the game's core runs at 1 fps](#a-worker-on-the-games-core-runs-at-1-fps) |
| Ho-oh's wings fanned out to a corner of the 3D image | [A camera-plane triangle has no screen area](#a-camera-plane-triangle-has-no-screen-area) |
| Log text drawn over the game's bottom screen | [No console on the game's screens](#no-console-on-the-games-screens) |
| 13 Hz with drawing at 78 ms | [Ask for the New 3DS clock](#ask-for-the-new-3ds-clock) |
| Silence with `NDSP started` in the log | [NDSP must be fed](#ndsp-must-be-fed) |
| A note hangs until the next note starts; sounds distort | [The NNS volume table is an empty alias](#the-nns-volume-table-is-an-empty-alias) |
| Notes sound right but the mix clips, even at low console volume | [The 16-bit mix has 6 dB more level than the DS](#the-16-bit-mix-has-6-db-more-level-than-the-ds) |
| A noise note writes an unused hole (1407; related) | [A noise note writes an unused hole](#a-noise-note-writes-an-unused-hole) |
| Game runs at 87 Hz; cutscenes finish early | [A frame under the refresh is not a 60 Hz frame](#a-frame-under-the-refresh-is-not-a-60-hz-frame) |

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

## A worker on the game's core runs at 1 fps

**Symptom.** The 3D appears (the title's Ho-oh, the bedroom) but updates about
once a second: walking looks frozen and the whole game feels unplayably slow.
The log says `3D images: 10 (scenes drawn: 10)` per 600 frames -- **105 images
in 4,680 frames** -- while the geometry builder reports 120 scenes every 240
frames (30 a second).

**Cause.** The GPU/display thread was created on the game's core, one priority
below the game (`priority + 1`). The port's software 2D composite holds that
core for 20-30 ms of every frame, so the worker only ran in the V-blank wait --
and a frame that already overruns the V-blank never waits. It drew about one
scene a second and the game's thread converted each image itself.

**Fix.** On a New 3DS the display thread goes on **core 2**, the geometry
thread's core (which sleeps between scenes); on an Old 3DS core 2 does not
exist, so it stays on the game's core. The log says
`[gx3d] display on core 2`. App objects are also compiled at `-O2` (they were
being built at `-O0` by the harness while the port archives were `-O2`).

**Rule.** A worker that must keep 30-60 fps needs a core the composite does not
own. Priority alone cannot schedule a thread on a core another thread is
holding.

## A camera-plane triangle has no screen area

**Symptom.** Ho-oh's left wing stretches to an off-screen point at the
top-left corner of the title's 3D image; on the PICA it can be a black or
grey fan. The bird looks deformed rather than like the DS title.

**Cause.** HGSS's title quads and quad strips contain vertices whose clip
coordinates are exactly `(0, 0, 0, 0)` — on the camera plane. The port's
clipper keeps it (it satisfies all six planes ±x/y/z <= w with equality), and
the software rasteriser stored its screen position as `(0, 0)` and drew a real
triangle from the corner. Both edges from a camera-plane vertex to a real
vertex project to that vertex's screen point, so the triangle has **zero
screen area**: the DS's clipper stores no polygon for it. The port was
drawing a fan that the DS never draws. (A real DS title capture in melonDS
shows Ho-oh intact.) Two `w` workarounds were tried first (`clip[3] = 1`, then
a huge `w` to make its `1/w` negligible); both only changed the fan's shading,
because the triangle has no area to draw.

**Fix.** `3ds_port/nitro/src/port_gx3d.c` `stage_triangle()` detects the exact
`(0, 0, 0, 0)` vertex (`camera_plane()`) and drops the polygon before it is
staged, for both the software reference and the PICA scene. Do **not** drop
every triangle with `w <= 0`: real wing geometry crosses the near plane, and
the old HGSS `pica_scene.c` drop removed it and left a transparent gap.
`nitro/tests/test_gx3d.c` (`test_camera_plane_triangle`) checks that the
synthetic camera-plane triangle stores no polygon. The PC title render now
shows the complete bird.

**Rule.** A vertex on the camera plane is degenerate, not a corner to project
to. Drop its polygon; do not give it a `w`, and do not drop the whole
near-plane-crossing set.

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
its own thread; `NitroPort_OnFrame` pumps the ARM7's alarm replies with
`NitroPort_SoundPumpReplies()`. Each block is flushed out of the ARM11's
write-back cache (`DSP_FlushDataCache`) before it is queued — the DSP's DMA
does not see that cache, and libctru does not flush for you — and the channel
announces NDSP's own rate (`NDSP_SAMPLE_RATE`, ~32728.5 Hz), not the mixer's
32768 Hz, so the DSP copies samples instead of resampling them (see "The mix
is level-right but still sounds rough"). The exit log prints
`audio: N dropped frames of M`, which is how starvation shows.

The thread's priority matters on a slow frame: it was `0x31` (below the game's
`0x30`), and a 20-30 ms software composite kept it off the core for longer than
a 15.6 ms block. It is now `0x2e`, above the game and the geometry thread. The
GPU/display worker also left the game's core (see "A worker on the game's core
runs at 1 fps"), so the audio thread no longer shares it with a starving 3D
worker.

**Rule.** Initialising an output device is not connecting it. Log a counter
that proves it was fed, and give the feeder a priority above the work that can
hold its core.

## A noise note writes an unused hole

**Symptom.** A note hangs and is only replaced when the next note starts;
single sounds and slower songs/percussion are distorted.

**Cause.** Patch 1405 changed `NitroPort_SoundRegisters` from "relative to
0x400" to the DS low 16-bit address (channel *c*'s control at `0x400 + 16c`,
timer at `0x408 + 16c`, which is what `port_sound_chip.c`'s `CHANNEL_BASE`
reads), but two direct writes in `SND_SetupChannelNoise` kept the old relative
index (`sub/lib/src/sound_channel.c:127,133`). A noise note (channels 14 and
15) therefore wrote DS `0x040000E0`/`0x040000E8` — an unused hole inside the
array, not the channel's control word — and left the previously set PCM
control in place. If that word came from a looped PCM (enable bit 31 and
repeat bit 27 set), the mixer kept reading it and retriggering the loop until
a PCM setup replaced it: the note that hung until the next one. The
pre-1405 relative index made the same write mean channel 14; either way the
mixer reads channel 14 at `0x4E0`, never `0x0E0`.

**Fix.** Patch **1407** writes `0x400 + channelOffset` and
`0x408 + channelOffset`, like the Psg branch. `tests/sdk/arm7_sound` leaves a
running looped PCM on 14/15, runs the noise setup, and checks the real
registers (`0x4E0/0x4E8`, `0x4F0/0x4F8`) while `0x0E0/0x0E8` and channel 0 stay
at their sentinels. Before the patch the suite reports 24 failures; after it,
122 checks, 0 failures. This fixed a real bug but was **not** the whole
held-note cause: the NNS volume table below also held notes at full volume.
See that entry.

**Rule.** After the 1405 remap, every `NitroPort_SoundRegisters[...]` index is
a DS low 16-bit address: channel *c* at `0x400 + 16c`, `SOUNDCNT` at `0x500`,
captures at `0x508`-`0x51C`. Never add a relative channel offset without
`0x400`.

## The NNS volume table is an empty alias

**Symptom.** Every note stays at full volume until the next note replaces it;
instead of a note and its fade there is a sustained, high-pitched crunch. The
whole mix clips (4-14% of samples at full scale). Patch 1407 (the noise
register offsets) is necessary but does not fix it.

**Cause.** NitroSystem's sequence player converts its 0..127 player volume to
decibels through `_0210D750` (`lib/NitroSystem/src/snd_player.c`). In the DS
build that symbol is an alias of NitroSDK's `SNDi_DecibelTable`:
`lib/asm/nitro_sdk_data.s` emits an **empty** `.rodata.alias_d750` section,
and the DS link order places it immediately before `SND_util.o`'s
`.rodata.snd_decibel`, so both names share the table. The port links with the
host's default section order, where the empty section landed on unrelated
`.rodata` (a function-pointer table). `NNSi_SndPlayerMain` then read pointer
bytes as decibels, the external fader became a large positive gain, every
channel volume clamped to 0 dB, and `SND_ExChannelMain`'s release stop
(`volume <= -723`) could never fire: each note kept playing until the next
key-on. A PC trace showed 15 channels at `ctrl` volume `0x7f`/shift 0 (0 dB)
and only 4 natural ends in 90 s; a fixed run showed volumes 41-103 with 192.

**Fix.** `hgss/compat/src/nns_volume_table.c` supplies the real 128-entry table
(identical to `SNDi_DecibelTable`); `tools/hgss_game.py` removes the empty
`.rodata.alias_d750` from the SDK data object for the port links and checks
after the link that `_0210D750` is 256 bytes
(`[sound] _0210D750 -> nns_volume_table.c (256 bytes)`). Tests:
`tests/test_hgss_game.py` keeps the table equal to the NitroSDK source and
fails the build if the link loses it; `test_sound_chip`/`test_sound_sequence`
still cover the mixer and driver. The DS gate is untouched: it links with its
own order and never runs the port's object step.

**Rule.** A zero-size data alias is only valid where the linker's section order
is pinned. When a port links the same objects with a different order, give the
symbol its real data; an empty alias silently points at whatever follows.

## The 16-bit mix has 6 dB more level than the DS

**Symptom.** After the volume-table fix the notes and fades are right, but the
mix still sounds clipped/crunchy. Turning the console's volume down does not
remove it: the damage is in the samples, not the amplifier.

**Cause.** The DS's output stage is 10-bit. A retail title captured in melonDS
and calibrated with a full-scale tone through the same path peaks at about
**0.42** of full scale (RMS 0.071); the port's 16-bit mix of the same
registers peaks at **1.0** (RMS 0.139). The port was about 6 dB hot, so
percussion peaks hit the rails and hard-clipped. PSG duty 7 was also emitted
as 100% high instead of GBATEK's 0% (silence), adding a full-scale DC step
whenever a sequence used it.

**Fix.** The 6 dB headroom is applied **inside `NitroSound_Mix`, before the
16-bit `saturate()`** (`NitroSoundChip.outputShift`, set to 1 by
`NitroPort_ResetSoundState`). The first version trimmed in
`NitroPort_SoundRender`, *after* the mixer had already clipped at ±32767: the
trim halved the flat-topped peaks to ±16384 (18-44 runs of 4-20 samples in
`trim.wav`, invisible to a 0.999-FS clip detector) and the crunch remained.
Attenuating before saturation keeps loud peaks smooth. The final render is
0.0708 RMS, peak 0.586, **no flat-top runs** (down from 18). The
channel/volume maths stays in the mixer (whose tests still check it), except
that PSG duty 7 is now silence in `port_sound_chip.c` and the test reference.

**Rule.** "Sounds like the amp" is not proof it is the amp: lower the volume
and listen. If the distortion remains, it is baked into the samples; compare
the digital level against a calibratable reference before adding gain.

## The mix is level-right but still sounds rough

**Symptom.** After the 6 dB trim the mix is not hot and does not clip, yet
tones are still not smooth: "rough", "crunchy". Loudness is fine; the timbre
is wrong.

**Cause.** The mixer's own output is not the problem. `/work/shift.wav` (the
final render with the 6 dB headroom applied before saturation) measures clean:
peak 0.586, RMS 0.0708, **no flat-top runs**. The earlier `/work/trim.wav` had
the same RMS but 18 flat-top runs at ±16384 from the post-saturation trim
(fixed above); otherwise both are clean: 26k distinct 16-bit levels, and no
periodic component at 39.5/64/170.7/192 Hz in the 6-14 kHz band (all below the
music's own 9.5 Hz rhythm peak). The high band is 5-6 dB *below* the DS
capture above 9 kHz. The two defects below are in the NDSP staging the WAV
bypasses:

1. **No cache flush.** The DSP reads a wave buffer with its own DMA, which
   does not see the ARM11's write-back L1, and libctru's `ndsp` never flushes
   (`ndspChnWaveBufAdd` only queues; every NDSP example calls
   `DSP_FlushDataCache` itself). Unflushed, the DSP reads stale FCRAM — the
   previous contents of that buffer, or, when the game evicts 64-byte lines
   at random, a mix of the current block and 16-frame fragments from ~62 ms
   ago. Recognisable music, scratchy.
2. **The channel asked for a resample.** `ndspChnSetRate(0, 32768)` against
   NDSP's 32728.5 Hz is a ratio of 1.0012, and libctru maps any
   `1.0 < rate <= 4/3` to a **2-tap linear** interpolator (even for
   `NDSP_INTERP_POLYPHASE`). A 2-tap interpolator's gain depends on the
   fractional position, which at that ratio sweeps 0..1 every ~25 ms, so the
   top octave is amplitude-modulated at 39.5 Hz: 0 to -2.9 dB at 8 kHz,
   **-7.8 dB at 12 kHz**, -12.9 dB at 14 kHz. On a real 4 s music segment the
   resample error is -16.9 dB broadband, and the 6-14 kHz band shows a
   +7.6 dB spike at 39.5 Hz. A per-buffer interpolation-phase reset would add
   a click train at the 64 Hz buffer rate.

**Fix.** `DSP_FlushDataCache` the rendered block before
`ndspChnWaveBufAdd`, and announce the DSP's own rate
(`ndspChnSetRate(0, NDSP_SAMPLE_RATE)`) so the ratio is exactly 1.0 and the
DSP copies samples. The price is that the 32768 Hz stream is consumed 0.12%
slow (two cents flat; the soundtrack also runs 0.12% longer), which is
inaudible. A channel rate of 32768 does *not* introduce a pitch error — the
source is consumed at exactly 32768 samples/s — so "two cents sharp" in
earlier notes was wrong.

**Rule.** A digital stream that measures clean in a file can still be
mangled by the output staging. When the mix is right but the timbre is not,
check (a) who flushes the cache the DMA master reads and (b) whether the
channel announces the device's own rate or asks the device to resample.

## A frame under the refresh is not a 60 Hz frame

**Symptom.** The game runs about 1.45x fast: scripted cutscenes finish early,
and `tools/frametime_report.py` reports **87.0 Hz** (40 samples, mean
`total_us` 11.49 ms) while `draw_us` stays under one 16.7 ms refresh.

**Cause.** The emulated DS's only clock is the V-blank the port raises once per
`NitroPort_OnFrame`. Nothing spaced those calls except `gfxSwapBuffers`' wait
for the display — and that wait ends as soon as the previous frame has been
presented. Once the composite got faster than a refresh, the swap returned
early and the loop free-ran. A frame that fits under the refresh is not a
60 Hz frame.

**Fix.** `WaitForFrameCap` holds frame starts to a `SYSCLOCK_ARM11 / fps` tick
grid with `svcGetSystemTick()` and sleeps only the remainder with
`svcSleepThread`. It is a deadline, not another V-blank: a frame that already
overran returns without sleeping, and one more than a period behind starts a
fresh grid so a slow frame cannot cause a catch-up burst. `DrawFrame` — both
blits and the swap — stays unconditional and runs before the wait. `fps` in
`port.ini` is 60 by default; `fps=30` halves the game's speed because the game
clock is per frame, so it is a comparison mode only.

**Rule.** If the display swap can return before a refresh, pace the frame loop
yourself and measure the interval between frame starts.

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
