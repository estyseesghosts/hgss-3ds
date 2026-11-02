# S14: closing the in-game Settings crashed

The owner's hardware run reported that entering the game's Settings and
closing them to return to the field crashed the program. The tree had no save
in the field, no crash dump and no log for that run, so the work was to
reproduce the close on the PC runner, find the exact faulting code, and only
then change anything.

## Reproduction

All the local saves (`/work/pcfield.sav`, `title-3d.sav`, `audio-test.sav`,
`asan-full*.sav`, …) booted to `intro_title`: the raw 512 KiB files were
blank. `~/Documents/pkx.sav` is 415,232 B and `port_card.c` requires exactly
524,288; padded to 512 KiB the game still started a new game rather than
offering CONTINUE, so the reader does not accept it as a HGSS save either.

So the run scripts a new game through the intro to a playable field:

1. Replay `tests/inputs/newgame-buttons.txt` (title → NO INFO NEEDED → Oak →
   gender → name → the bedroom at frame 19,392).
2. Walk up to the stairs and left/up onto them (`UP` then `LEFT`, 6-frame
   presses 8 frames apart): the map reloads at 22,022 on the ground floor and
   mother's dialogue starts.
3. Press `A` every 300 frames through her dialogue. The start menu is open by
   frame ~34,000.
4. Touch OPTIONS at `(125, 115)` — the `gTouchscreenMenuIconHitboxes`
   coordinates — and QUIT at `(222, 183)`.

`--touch` is only held for 6 frames, but the hitbox is stable, so one tap is
enough.

Before the fix this aborts at frame **35,028** with `rc 139` (SIGSEGV).

## Trace

`gdb` cannot attach inside the container (`ptrace` is blocked by its seccomp
profile: "Couldn't get registers: Input/output error"), and the runner's
signal handler prints only one frame because the build has no frame pointers.
The PC binary is a non-PIE ELF, though, so a temporary diagnostic in
`Transfer()` (`3ds_port/nitro/src/port_dma.c`) was enough:

```c
if (src < 0x08000000u || dest < 0x08000000u) {
    void *frames[32];
    int n = backtrace(frames, 32);
    fprintf(stderr, "[dma-bad] dma%u cnt=%08x src=%08x dest=%08x count=%u\n",
            dmaNo, cnt, src, dest, count);
    backtrace_symbols_fd(frames, n, 2);
    abort();
}
```

The `backtrace()` call is in normal code (not in the signal handler), so the
`.eh_frame` unwinder works, and `addr2line -e /work/…/pokeheartgold-pc`
resolves the addresses. It reported:

```
[dma-bad] dma3 cnt=81600160 src=02c002c0 dest=0e8a1d80 count=352
  0x868f55c fprintf
  0x868f63b NitroPort_DmaStart        port_dma.c:158
  0x868017c MIi_DmaSetParams_wait     mi_dma.c:55
  0x867d8ff GXi_DmaCopy16             gx_load2d_private.h:19
  0x82d84ca LoadBgVramScr             bg_window.c
  0x82deb12 BgConfig_HandleScheduledBufferTransfers
  0x82de96e DoScheduledBgGpuUpdates
  0x8498978 OptionsApp_OnVBlank       options_app.c
  0x846a0b0 NitroMain
```

`count = 0x2C0 = 704` bytes and `src = 0x02C002C0` — the count duplicated
into both halves — is a pointer-shaped value from freed memory, not a real
buffer.

## Cause

`OptionsApp_Free` runs over two frames:

- `setupAndFreeState == 0`: `GF_DestroyVramTransferManager`,
  `OptionsApp_FreeWindows`, the strings and msgdata, `OptionsApp_FreeBgConfig`
  (frees the five tilemap buffers and the `BgConfig` itself) and the sprite
  renderer. Returns `FALSE`.
- `setupAndFreeState == 1`: `Main_SetVBlankIntrCB(NULL, NULL)`,
  `HBlankInterruptDisable`, planes off. Returns `TRUE`.

The port raises the frame's V-blank between the two calls, and
`gSystem.vBlankIntr` is still `OptionsApp_OnVBlank` (`system.c:38`). That
handler runs `DoScheduledBgGpuUpdates(data->bgConfig)` on the freed config.
NNS's free-list header occupies the block's first words, so
`bufferTransferScheduled` contains set bits and the `bgs[]` tilemap pointers
are free-list values; `LoadBgVramScr` copies from one of them.

The decomp is a matching decomp, so this is the retail game's own behaviour.
On the DS the garbage values are RAM addresses: the DMA reads them, writes one
frame of noise into VRAM and the next frame clears the callback. The port's
DMA dereferences the value as a host pointer, so the same frame is fatal.

`OptionsApp_Free` is the only one of the start menu's apps with this split:
`Bag_Exit`, `PartyMenuApp_Exit` and `PokedexApp_MainSeq_01` all stop the
callback in the same call that frees their BG layers.

## Fix

Patch **1904** (`patches/pokeheartgold/1904-options-stop-the-v-blank-callback-
before-freeing-the-app.patch`):

```c
static BOOL OptionsApp_Free(OptionsApp_Data *data) {
    switch (data->setupAndFreeState) {
    case 0:
#ifdef SDK_PORT_3DS
        Main_SetVBlankIntrCB(NULL, NULL);
#endif
        GF_DestroyVramTransferManager();
        ...
```

It only adds lines under the port macro; the `#else` text is unchanged, as
the patch-stack rule requires. The later `Main_SetVBlankIntrCB(NULL, NULL)` in
case 1 stays and is now redundant.

The alternative — validating addresses in `NitroPort_DmaStart`/`Transfer` —
was rejected: the port cannot know which addresses are game heaps, and a guard
would hide every other stale-pointer bug instead of this one.

## Verification

- PC, same script: `rc 0`; the field overlay reloads at frame **35,032** and
  the run reaches frame 42,000. Two more open/close cycles (touch MENU at
  `(80,8)`, OPTIONS, QUIT) also return to the field.
- `bootstrap.py --clean`: `Done: build/upstream (79 patches applied)`.
- `make test` and `make port-tests`: see `docs/progress/STATE.md`.
- 3DS build and the new artifact: see `docs/progress/STATE.md`.

## Process notes

- **The container's `build/` is not the host's `build/`.** The dev container
  mounts `/workspaces/pokeheartgold-3ds/build` from the `hgss-build` Docker
  volume, while the rest of the checkout is the host bind mount. An experiment
  edited on the host under `build/upstream/src/` never reached the compiler;
  the first "fix confirmed" run was really the unpatched tree. Edit and commit
  the patch inside the container (`build/upstream` is a git checkout there),
  or apply the patch file and re-bootstrap. `3ds_port/`, `hgss/`, `patches/`
  and `docs/` are shared.
- **No field save, no field bug.** The scripted new-game walk is the way in
  until the save round-trip exists; the frame numbers above are PC-run
  calibration.
