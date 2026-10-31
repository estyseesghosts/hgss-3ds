# P1 Wave 2: remaining Phase 1 patches (0502, 0302, 0602–0604, NNS 0302, 1401, 1402)

## Done

Scratch: `/work/wt/p1_wave2` (container), 8 commits on top of `build/upstream`
`9d1aaa7` (the 16-patch stack). Each commit exported to `patches/`; the exports
are what was verified below (see "Rebuild" for why the hashes moved once).

| Patch | Area (Platinum basis) | Files | Suite (checks) |
| --- | --- | --- | --- |
| NitroSDK `0502` | remaining DIV/SQRT writers (0002/0501 leftovers) | `fx/{fx_vec,fx_vec_mult_add,fx_vec_normalize,fx_mtx44_ortho,fx_mtx44_perspective}.c`, `tp.c` | `div_sqrt_writers` (69) |
| NitroSDK `0302` | arena/thread/IRQ stacks (0006) | `os/{os_arena,os_thread,os_interrupt}.c` | `os_arena` (22) |
| NitroSDK `0602` | timers in software (0013) | `os/os_tick.c` (+ `timer_shared.h` in 1402) | `os_timer` (23) |
| NitroSDK `0603` | libpak ROM archive + overlay hooks (0010/0011/0014) | `fs/{FS_overlay,FS_rom}.c` (+ `overlay.h` in 1402) | `fs_overlay` (41) |
| NitroSDK `0604` | 3D command ports + GXSTAT error (0024/0025) | `gx/{g3imm,g3x}.c` (+ `g3imm.h`, `g3x.h`, `uncompress.h` in 1402) | `gx_3d` (119) |
| NitroSystem `0302` | GXFIFO words, early 3D transfers, hidden OAM (NNS 0001–0005) | `g3d_{gecom,sbc}.c`, `gfd_vram_transfer_manager.c`, `g2d_oam.c` (+ transfer header in 1402) | `nns_port` (31) |
| pokeheartgold `1401` | ARM7 sound driver (0018–0023) + static init (0027) | `lib/dsprot/include/static_init.h`, `sub/lib/src/sound_{main,command,alarm,channel,driver,ex_channel}.c`, `lib/NitroSDK/src/snd/SND_work.c` | `arm7_sound` (42) |
| pokeheartgold `1402` | lib/include halves of 0602/0603/0604 + NNS 0302 | `timer_shared.h`, `overlay.h`, `g3imm.h`, `g3x.h`, `uncompress.h`, `gfd_VramTransferManager.h` | (covered by the suites above) |

Why headers ride in 1402: NitroSDK-stack patches apply with
`git am --directory=lib/NitroSDK`, so a patch touching `lib/include` cannot
live there (precedent: 1202/1301 carry SDK headers in the root stack). 1402 is
exactly those six header hunks, one per area, documented per hunk.

## Acceptance (all in the dev container)

- Identity (`gcc-10 -E -P`, survey flags minus the macro, `-DSDK_ARM9`, ARM7
  files with `-DSDK_ARM7`): **0 diff lines for all 26 touched files**
  (6 + 3 + 2 + 3 + 5 + 5 + 8 incl. the static-init includer TU).
- `python3 tools/bootstrap.py --clean`: **24 patches apply** (`Done:
  build/upstream (24 patches applied)`).
- `make test`: **exit 0, 18 suites**, incl. the 7 new ones
  (69 + 22 + 23 + 41 + 119 + 31 + 42 new checks). `port_hooks` needed two
  stub additions (timer + GX-error no-ops; no assertion changed) because 0602
  and 0604 add imports to `os_tick.o`/`g3x.o`.
- Survey (`gcc-10 --m32`): **269/284, same 15-file failing set** as before
  the wave; warnings 563 (unchanged).
- devkitARM syntax (`armv6k/mpcore/vfp`, port flags): all 15 touched C files
  parse under `SDK_PORT_3DS` (ARM7 define for `sub/`, ARM9 otherwise).
- Every new suite: green on the patched tree, red on pristine (link failure
  or value failures: 37 / link / 15 / 22 / link / link / link), and a
  drop-the-hook mutation fails (6 / 1 / 1 / 2 / 3 / 9 / 1 check failures).
  Two mutations caught real bugs while writing: `SND_StartAlarm`'s restart
  path missed the port stop (stuck port alarms), and `SND_StopChannel`'s
  struct overlay mis-addresses the port array (rewritten as plain RMW).

## Deviations from the brief (all measured, none guessed)

1. `u$3681` / `initialized$3619` needed no rename: gcc-10 compiles all nine
   sound files cleanly already (0 errors), and GAS/`$` link names are unchanged.
2. `divfixed()` in `fx_mtx44_ortho.c` is dead code but writes DENOM, so it is
   hooked like `inv()`.
3. Timer sync uses plain guarded calls, not Platinum's null macro: the macro
   would leave `((void)0);` lines (4 diff lines); guarded calls keep 0 diffs.
4. `FS_LoadOverlayImageAsync` opens the overlay ROM file instead of Platinum's
   `FS_CreateFileFromMemory` (absent in this SDK revision); the game
   (`src/poke_overlay.c`) waits on and closes it, which a bare file would not
   survive (`FSi_SendCommand` dereferences `arc`).
5. Matrix loads use per-word `NitroPort_GxDirectWrite` loops (Platinum 0024),
   not `NitroPort_GxFifoWords` (the port DMA/MI path already uses that for
   bulk sends; verified in `mi_memory.c`/`port_dma.c`).
6. `g3x.c` hooks the four `GXSTAT |= SE_MASK` clears (Platinum 0025 only
   touched the inline, which HGSS lacks, so the inline is added under the
   macro). The `GXSTAT = 0x0` full reset is one-time init on a fresh engine.
7. Port function declarations use `unsigned int`, not `u32`: HGSS `u32` is
   `unsigned long` while the port uses `uint32_t` (`unsigned int`), and port
   TUs include both SDK and port headers — the first port build caught this
   as a hard conflicting-types error (`port_gx3d.c`). `u16`/`void` decls are
   unaffected (`u16` is `unsigned short` everywhere).
8. SND shared-work locking (Platinum 0022) uses duplicated getter bodies, not
   the statement-expression macro: the macro changes DS-preprocessed text
   (parens); duplicated bodies keep 0 diffs. It lives in 1401 (root stack can
   touch `lib/NitroSDK/src/snd/`).
9. NNS 0001 (all 7 hunks) and 0002 are N/A with per-hunk evidence in the 0302
   message: the constructs do not exist in HGSS's revision (flat `src/`,
   game-specific small files), and the whole of `lib/NitroSystem` already
   parses under the survey flags. `NNSi_GfdDoVramTransfer3D` needs no `OS_Halt`
   change: vendored `port_os.c` already calls it (weak).
10. 0005 covers `NNS_G2dResetOamManagerBuffer` only (brief scope); the
    module-init fills (`NNS_G2dInitOamManagerModule`) park entries the same
    way and are a follow-up.

## Rebuild (port-decl types)

After the first export, the host32 port build failed on
`NitroPort_GxDirectWrite` conflicting types (deviation 7). Fixed in scratch by
rebuilding the four commits from `343083f` with the saved messages
(`/tmp/msg*.txt`); file sets verified per commit; 0604, NNS-0302 and 1402
re-exported; identity and the `gx_3d`/`nns_port` suites re-run green.

## Environment notes for the lead

- Another session is working concurrently in this repo (uncommitted `Makefile`,
  `hgss/compat/*`, `tools/hgss_nitro.py` changes, `p2-*` notes). Nothing of
  theirs was touched; nothing of this slice lives in their paths. Two
  observations from the overlap: (a) the host32 port build outcome moved
  during the wave as their compat/tooling changed — the only failures seen
  with the final tree are the pre-existing `mi_dma_gxcommand.c` ones plus
  whatever their flux adds; (b) `hgss_nitro.py` already lists HGSS
  `ARM7_SOUND_FILES` with N7_ renaming, so the 1401 wiring half exists in
  `tools/` (lead scope).
- `make test` flaked twice under load (10 CPUs, load ~7): once `gcc_fixes`
  (a), once two Python tool-tests — both green on immediate rerun without any
  tree change, green in isolation (gcc_fixes ×2, tool-tests ×3), and green in
  the final full gate. Final `make test` exit 0.
- Transport lesson (for future subagents): the Mac-side shell parses
  single-quotes/`[]`/`<<` inside `devcontainer exec … bash -c '…'`; heredocs
  with `<<'EOF'` plus `chr(10)`/`chr(39)` in Python survived, backslash
  escapes and literal apostrophes did not. `stat` a path when a fresh file
  "disappears" (virtiofs listing staleness); always commit scratch before
  mutating (`git checkout --` reverts uncommitted work silently).
- Follow-ups (out of this slice): OAM module-init fills; ARM9-side sound
  getter audit beyond `SND_work.c` (if any); N7_ port wiring (`tools/`,
  lead); `G3X_GetCommandFifoStatus`/`GX_FIFOSTAT_*` + `MATH_CountPopulation`
  compat items (other slices); DS-side gate with all 24 patches.

## Resume

```bash
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && python3 tools/bootstrap.py --clean && make test'  # exit 0, 18 suites
bash tests/sdk/arm7_sound/run.sh build/upstream   # 42 checks (needs the 24-patch tree)
git -C /work/wt/p1_wave2 log --oneline | head -9  # the 8 commits + base
```
