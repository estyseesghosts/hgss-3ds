# Phase 4 progress

Status: **in progress.** The headless PC build now boots from cold start through the Game Freak
logo, the full opening movie, the title screen, the main menu, Oak's speech (portrait, text, touch
buttons, gender choice, naming keyboard) and into the first field scene. The remaining exit items
are the golden comparison (needs the user's melonDS captures), a port-written save verified in
melonDS, the sanitizer replay and the upstream drafts.

Last verified: **2026-11-01**, container `/workspaces/pokeheartgold-3ds`, 76 patches
(NitroSDK 15, NitroSystem 3, pokeheartgold 56).

## What the scripted run does

`tests/inputs/newgame-buttons.txt` is calibrated against the PC build:

| PC frame | Event |
| --- | --- |
| 600 | Game Freak logo |
| 5400 | opening movie ends |
| 6300 | Ho-Oh title screen |
| 6550 | START → main menu |
| 6701 | overlay `OVY_74` (main menu) loads |
| 6800–7900 | main menu; three D-pads reach **NO INFO NEEDED** |
| 8100 | A → Oak's speech |
| 8400–19200 | A advances the speech pages, gender, name, confirmation |
| 19380 | end of Oak's speech; overlay `OVY_36` / `OVY_124` / `field` load |
| 19500+ | first bedroom frames |

No crash, missing file or stall through 20,000 frames on the real HG pack.

Screenshots from these runs show: the beach/Game Freak logo, Lyra in New Bark Town (movie), the
Ho-Oh title, the main menu, Oak's portrait with the "I will first tell you important things" page,
the gender portraits, the naming keyboard, and the bedroom.

## Runner features (P4-A)

`hgss/game/app/main_pc.c` has the Platinum runner's diagnostics: `--stop-at-missing-file`,
`--stall-seconds` (default 5), `--screenshots`, `--screenshot-every`, `--press`, `--touch`, `--wav`,
`--dump-3d`, `--trace-pxi`, `--trace-irq`, `--events` (JSON lines). A crash prints the signal, fault
address, pc and a frame-pointer backtrace; `--stall-seconds` dumps every thread before stopping.

`--trace-pxi` / `--trace-irq` use linker `--wrap` and `hgss/game/app/pc_trace.c`, so `3ds_port/`
stays byte-identical. `tools/hgss_game.py` compiles that file for host32 only.

## Boot fixes so far

Each is a numbered patch with a `gcc -E -P` identity proof under `#else`:

- **1801** — `SystemCard_InitRomArchive` used the literal DS addresses `0x027FF000` / `0x027FFA80` /
  `0x027FF00C`; spell them from `HW_MAIN_MEM` under `SDK_PORT_3DS`.
- **1802** — the main-menu work area is a run of adjacent DS bss globals aliased with
  `&gMainMenuUtilCryptoHeapId + 1`. The port's per-symbol sections broke that alias, so the fade
  state was lost and the menu re-ran its graphics setup every frame until the heap ran out. Make
  `gMainMenuUtilWork` a real `MainMenuUtilWork` object under `SDK_PORT_3DS`.
- **Harness** — define `PM_KEEP_ASSERTS`, as the DS build's `config.mk` does. `GF_ASSERT` operands
  in this code have side effects (Oak's speech creates its char transfer tasks inside
  `GF_ASSERT`); without the define those tasks were never made and sprite setup dereferenced NULL.

## Tools

- `tools/boot_smoke.py` — synthetic pak by default, `--pak/--manifest` for the real one; reports
  `missing_file` / `crash` / `stall` / `frame_limit` as JSON with stage, frame, signal, address,
  symbol and log tail. `--expect-stage` makes it a gate. Tests: `tests/test_boot_smoke.py`.
- `tools/golden/run.py` — golden diff runner (see its section below once landed).

## Sanitizer replay (P4-F)

`python3 tools/hgss_game.py --target host32 --sanitize --out …` works: the container's gcc-10 `-m32`
has i386 libasan/libubsan. The instrumented boot replay found and fixed two out-of-bounds reads that
are harmless on the DS (they land in the next bss object) but real on the host:

- **1901** `SaveData_InitSubstructs` reads `hdr[i + 1]` before checking `i + 1 <
  gNumSaveChunkHeaders` (frame 0, `SaveData_New`).
- **1902** `TouchscreenMenu_LoadIconResources` reads `gTouchscreenMenuLayouts[layout][index]` for
  resource slots 8–10 although a row has eight bytes.

The ASan link also needs `std::__new_handler` (the MSL throwing `operator new` is only pulled in by
the sanitizer runtime); it is defined as a NULL pointer in `cpp_runtime.c`.

**Known instrumentation limitation:** the overlay loader restores an overlay's `.data` with one
`memcpy` over a linker section that ASan models as many separate globals, so the copy crosses global
redzones. That is not a game bug but ASan cannot see the section as one object. The full replay
therefore runs with `ASAN_OPTIONS=report_globals=0`; the boot phase (before the first overlay reload
that trips it) runs with global checks on, which is how 1901 was found. Heap and stack checks stay on
for the whole run.

**Result:** the 20,000-frame scripted replay with 1901/1902 applied exits 0 with **0 ASan errors and
0 UBSan runtime errors** (`grep -cE "ERROR: AddressSanitizer|runtime error"` = 0).

## Open items

- **Golden frames are still pending the user's BizHawk/melonDS run** on Linux or Windows. Until
  then `run.py` compares the PC build against its own previous runs (regression mode); a melonDS
  match is **not** claimed.
- **The save is not written yet.** The run reaches the field with a 512 KiB all-`0xFF` save; the
  in-game save flow has not been scripted (the field's first cutscene may gate input). `savetool.py
  validate` therefore reports no consistent footers. This is the next P4-E item.
- **Sanitizer build** has not been attempted yet (`tools/hgss_game.py --sanitize`).
- **Touch input** does not currently register on Oak's speech menu (D-pad works). The calibration
  path and the naming keyboard's touch targets are unverified.
