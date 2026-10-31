# Phase 2 progress

Status: **in progress.** Everything needed to compile the game exists and works; nothing links yet.
Game sources compile (1044 of 1094), all 150 ARM9 `.s` assemble, and the host link reaches the
resolver with **966 unique unresolved symbols** — that list is the work queue. `--check-overlays`,
the symbol cross-check and the 3DS shell are not done. The detailed numbers are in
[agent-notes/p2-slice-f.md](agent-notes/p2-slice-f.md) and
[agent-notes/p2-sources-overlays.md](agent-notes/p2-sources-overlays.md).

## What exists

| Piece | Tool / file | Verified by |
| --- | --- | --- |
| Game + library source list | `tools/game_sources.py` | `tests/test_game_sources.py` (23 tests); real tree: 1012 C, 56 C++, 150 ARM9 `.s`, 259 port-lib sources, 129 overlays |
| Overlay table + sections | `tools/game_overlays.py` | `tests/test_game_overlays.py` (33 tests); 129 overlays, `OVY_45` = 133 objects, `.sinit` + `.init_array` merged into `ovl_sinit_<name>`, `gHeartgoldOverlays` emitted |
| Generated headers | `tools/prepare_game_headers.py` | `tests/test_prepare_game_headers.py` (22 tests); 828 `msg_*.h` + 205 `.naix` + `fx_const.h`, **byte-identical** to `/work/pokeheartgold`'s matching build |
| `.s` data files | `tools/s_to_gas.py` | `tests/test_s_to_gas.py` (24 tests); 150/150 assemble under `gcc-10 -m32` and `arm-none-eabi-gcc -march=armv6k`, 0 symbol/section/alignment differences; ARM9 `.s` are pure data except one ARM7 veneer |
| Network / anti-piracy stubs | `tools/make_game_wifi_stubs.py`, `hgss/game/stubs/{wm_stubs.c,dsprot_stubs.c}` | `tests/test_game_stubs.py` (9 tests); WVR returns 1 and calls back, `DWC_Init` returns 0, 53 `WM_*` report wireless-off, dsprot answers "genuine cart" through all five game prime-factor checks |
| Game build harness | `tools/hgss_game.py`, `hgss/game/app/{heartgold.c,main_pc.c}`, `make game-pc` / `game-3ds` | `tests/test_hgss_game.py` (21 tests); `--target host32 --keep-going` compiles and links |

## Measured state (host32, `--keep-going`)

```
python3 tools/hgss_game.py --target host32 --keep-going --out /work/out/game1
  1044/1094 C/C++ compiled        50 files failed (424 errors)
  150/150 .s assembled
  libnitro.a built                libnitrosystem.a FAILED (nns_all_headers.c unity file)
  186 Wi-Fi stubs generated
  link: 966 unique unresolved symbols (3989 reference lines)
```

3DS (`--target 3ds`): 1018/1094 compiled, 76 failed (511 errors), 150/150 `.s`; no archives because
the ARM7 rule rejects `ds_sound_tables.c` under devkitARM (`junk at end of line`, `unrecognized symbol
type ""` — the C file is fine for gcc, the 3DS rule pipes it through the assembler path).

## The 50 files that do not compile yet

Grouped from the harness output; the per-file list is in the slice-F note:

- `weather_manager.c` label-before-declaration (12 errors; the array assignments are fixed by 1503).
- `comm_state.c` `MATHRandContext32` conflicting types, `global.h:30` assignment of read-only
  location, label-before-declaration.
- `src/to91_msl_*_new.cpp` MSL `operator new` first-parameter type (1505 removed the exception specs).
- `src/lobby/*.cpp` and `wifi_setup`/`NitroDWC` headers on 64-bit-size assumptions; the 3DS target adds
  `va_list` arithmetic and implicit math declarations.
- Remaining implicit-declaration sites that the 32-bit host tolerates but the 3DS build does not.

## Next actions, in order

1. **Fix `libnitrosystem.a` for both targets.** The generated `nns_all_headers.c` unity file fails on
   `NNSSndHandle` and on `SND_RecvCommandReply`/`SND_ReadDriverInfo`/`SND_SetMasterVolume`/`SND_PrepareSeq`
   conflicting with `hgss/compat/include/nitro/snd/ARM9/interface.h`. Fix the compat header (one
   declaration set, no duplicates) and rerun; that also unblocks `test_nitrosystem` in Phase 1.
2. **Author the remaining GCC-fix patches** for the 50 failing files (numbering: `15xx` game GCC fixes,
   continuing after 1508). Each needs the `gcc -E -P` identity proof and, where it is a behaviour fix,
   a host test.
3. **Work the 966-symbol queue** from the harness link: every symbol is either a missing source in the
   list (fix the list), a real game/SDK object not compiled (compile it), or a documented stub
   (Wi-Fi/DWC/VCT/CPS/dsprot/CTRDG). No blanket game-symbol stub.
4. **3DS ARM7 sound path**: `ds_sound_tables.c` must assemble/compile under the devkitARM ARM7 rule.
5. **`--check-overlays`**: `main_pc.c` must load all 129 overlays twice and verify `.data`/`.bss`.
6. **`tools/symbol_crosscheck.py`** against `/work/pokeheartgold/build/heartgold.us/main.elf`, with the
   replaced/stubbed allow-list.
7. **Docs**: this file, STATE, decisions and the wiki as each wave lands.

## Decisions taken for Phase 2

- The game build is driven by `tools/hgss_game.py` + command-line overrides, not by editing
  `3ds_port/game/Makefile` (the vendored tree stays byte-identical).
- The `.s` files are converted to GAS rather than reimplemented in C: symbol names, section flags and
  alignment are preserved exactly, and `.word` becomes `.4byte` (x86 `.word` is 16-bit).
- `lib/MSL_C/asm/*.s` are pure data but are **not** linked: their errno/ctype tables clash with glibc;
  newlib/MSL C sources are used instead.
- Two duplicate globals (`FrontierScript_ReadByte`, `sListMenuTemplate`) are weakened by the harness
  for now; they need a real per-overlay/local fix before the overlay check can be trusted.
