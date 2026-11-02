# S15: raw DS address literals (the wild-encounter crash)

The owner's tenth hardware run (log-10) crashed on the first tall-grass wild
encounter. This note records how the log was decoded, why the fault was not in
the battle's 3D code, how the whole class of the same bug was found, and what
is still unverified.

## Decoding log-10

The run ends with:

```
[13138] [nitro] overlay OVY_6 loaded
[13138] [nitro] overlay OVY_7 loaded
=== exception (type 1) pc 008bcf0c lr 008bca5c cpsr 600f0010 ===
r0 00000001 r1 00000000 r2 00000059 r3 04000000
```

The artifact was `pokeheartgold-3f493661.3dsx` (the rollback on the SD card;
its build is `/work/slice-3d/pokeheartgold-3ds.elf`, same sha256). Mapping
the two addresses in that ELF:

- `pc 0x008bcf0c` is `ldr r3, [r3]` in `WirelessStatusIcon_LoadChar`, just
  after `mov r3, #0x04000000` (DISPCNT).
- `lr 0x008bca5c` is the instruction after `bl WirelessStatusIcon_LoadChar`
  in `WirelessStatusIcon_New`.

So the battle reached `Battle_CreateWirelessStatusIcon` (called from the
battle setup at `battle_main.c:563`), which reads DISPCNT to pick the OBJ
character mapping, and the read killed it. `r3 = 0x04000000` in the dump
matches the `mov`, so this is not a guess.

## Why it faults only on the port

Patch 1301 makes the DS memory areas ordinary arrays: `HW_IOREG` is
`NitroPort_IoReg`, main memory is `NitroPort_MainMem`, and so on. Code that
uses the SDK's `reg_*`/`HW_*` macros follows them automatically. Code that
writes the literal does not: the literal is a real register or RAM address on
the DS, and unmapped on the 3DS (and on the PC). The decomp is matching; this
is a latent game bug the DS hardware tolerates by having the address mapped.

## Finding the class

A naïve grep finds noise (numeric tables, comments, DS-only `#else` copies).
The reliable method is the preprocessor: compile each game source with the
port's flags and `-E`, then look for a raw `0x02`-`0x07` address that is cast
to a pointer. That found 17 sites in `src/`, and none elsewhere in compiled
game code:

| area | sites |
| --- | --- |
| DISPCNT / DB_DISPCNT | `wireless_status_icon.c` (the crash), `spin_trade_group_app.c` |
| G3 swap-buffers (`0x04000540`) | `pokemon_summary_scene.c` |
| DTCM IRQ-check (`0x027E0000`) | hall of fame, wireless trade, GTS, global terminal, battle factory, trade sequence, link battle, battle room results, plaza game |
| LCDC VRAM (`0x06840000`) | `encounter_effect_particles.c` |
| shared main memory | `system_card.c` (`0x027FFC2C`), `wireless_manager.c` (`0x027FFC3C`) |

The battle's own 3D code was clean: the fault was in the battle *setup* UI,
before any 3D scene. Trainer and link battles use the same
`Battle_CreateWirelessStatusIcon`, so the one crash explains all of them.

`system_card.c`'s ROM-header mirrors were already `#ifdef SDK_PORT_3DS`; the
reboot flag next to them was missed.

## The fix

Patch 1905 replaces each literal with the macro the rest of the engine uses:
`reg_GX_DISPCNT` / `reg_GXS_DB_DISPCNT`, `reg_G3_SWAP_BUFFERS`, `HW_DTCM`,
`HW_LCDC_VRAM + 0x40000`, `HW_MAIN_MEM + offset`. Every hunk is
`#ifdef SDK_PORT_3DS` with the original line in the `#else`, so the DS text is
unchanged. The DTCM writes keep their arithmetic (`HW_INTR_CHECK_BUF -
HW_DTCM`) and only swap the base, which is the same linker symbol on the DS.

The encounter effect's clear says "VRAM bank D" but `0x06840000` is bank C in
the LCDC view (A at `0x06800000`, B at `+0x20000`, C at `+0x40000`), and the
`reg_GX_DISPCAPCNT = 0xC0320C04` capture destination is bank C too. The
comment was corrected. Display capture itself is not implemented in the port,
so that effect still will not show the captured field; it no longer faults.

## The guard

`tests/test_hw_addresses.py` models the port's preprocessor for
`SDK_PORT_3DS` (the only macro that picks the branch) and scans
`src/**/*.c` plus the ARM7 sound driver's `sound_*.c`. The DS-only `#else`
copies are ignored, and a synthetic-source test checks the model. A new raw
literal fails `make test` with the file and line.

The ARM7 sound driver's `REG_POWCNT2` stays literal: only
`SND_BeginSleep`/`SND_EndSleep` use it, and their caller
(`power_manager_driver.c`) is not built for the port. The wireless `sub/`
sources (`wl_*`, `wmsp_*`, `wvr_*`) are the stub Wi-Fi stack and are not
compiled.

## Verification

- Pre-1905 `wireless_status_icon.c` is reported by the scanner (two lines);
  the fixed tree reports zero in `src/`.
- The 3DS ELF's `WirelessStatusIcon_LoadChar` now loads `NitroPort_IoReg`
  (`0x00d889e0`) from the literal pool instead of `mov r3, #0x04000000`.
- The 3DS build is 1092/1092; `make test` is 344 tests; the artifact is
  9,278,828 B, sha256 `44e13f9c…5f6b`.
- **Not yet: an end-to-end battle.** The scripted PC new game has no starter,
  and the west exit of New Bark Town is blocked until Elm gives one, so the
  runner cannot reach tall grass without a save. The owner's
  `heartgold.sav` (the path log-10 prints) would make the battle reproducible
  on the PC; otherwise the hardware run is the check.

## Notes

- The port raises extra V-blanks during long loads, so the 3DS log's frame
  numbers and the script's frame schedule drift; do not use the log's `[N]`
  prefix as the script input time.
- `log-10` belongs to the `3f493661` artifact, which predates the camera-plane
  drop, the NNS table and the NDSP staging. Re-check anything else you see in
  it against the current build first.
