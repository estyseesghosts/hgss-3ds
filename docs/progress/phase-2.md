# Phase 2 progress

Status: **complete for the Phase 2 exit criterion.** `pokeheartgold-pc` links with **0 unresolved
symbols**, `--check-overlays` passes on the real binary (129 overlays, 0 failed), and
`tools/symbol_crosscheck.py` reports **0 missing** against the DS reference. The 3DS side compiles
1092/1092 game files, builds both archives and links `pokeheartgold.3dsx` with 0 unresolved. Phase 1's
DS gate is the remaining formal gate (see below).

Status: **in progress — the game compiles completely and the host link is 34 symbols from zero.** 1092 of
1092 C/C++ files compile on host32, 150/150 ARM9 `.s` assemble, both port archives build, and the
host link is down from **966 to 34 unique unresolved symbols**. `--check-overlays` and
`tools/symbol_crosscheck.py` exist but have not run against a real linked binary yet. The 3DS target
still has its own blockers (`std_sprintf`, the ARM7 C-file rule, ~45 devkitARM-only compile errors).

> **Update 2026-10-31 (session 5).** This supersedes the 39-patch measurement below.
>
> | Check | 39 patches | 61 patches (now) |
> | --- | --- | --- |
> | host32 compile | 1044/1094 (50 fail) | **1092/1092 (0 fail)** |
> | ARM9 `.s` | 150/150 | 150/150 |
> | `libnitro.a` | builds | builds |
> | `libnitrosystem.a` | fails in `nns_all_headers.c` | **builds** |
> | host link | 966 unresolved | **34 unresolved** |
> | `make port-tests` | hangs in `test_arm7` | **exit 0** (all suites, nitrosystem 17,122/0) |
> | 3DS compile | 1018/1094, 76 fail | ~1049/1094, ~45 fail (devkitARM-only) |
>
> **Compile fixes (patches 1509–1525).** Batch 1 (45 files): C99 label-before-declaration (38 sites),
> eight files redeclaring SDK functions with the wrong types, WiFi Plaza `static` linkage, `battle_main`
> and `ball_capsule_editor` static initializers, `MATHRandContext32`, `ARRAY_ASSIGN` const, the hooked
> square-root helpers, medal-table copies, MSL/lobby `operator new` `__SIZE_TYPE__`, PPW `std::` names
> and designator order. Batch 2: `wifi_plaza_news` and `nintendo_wfc_connect_unused_session` `static`
> linkage, and `ball_capsule_editor`'s parse cascade. The two `lib/syscall/src/_secure_IP*.c` files are
> excluded from source selection, not stubbed (embedded MWCC secure-area images, no consumer).
>
> **Library set.** `libnitrosystem.a` is linked (NitroSystem patch **0303** fixes a real `.gfd` `.bss`
> layout bug). All 21 `lib/NitroDWC/asm` objects are data (no `T` symbols) and are linked; the four that
> reference a non-TLS `errno` are excluded. The link's unresolved set then dropped by ~600.
>
> **The 34 that remain** (see the grouping below): MSL soft-float/string runtime, C++ runtime, `MB_*`
> multiboot, and five Pokéwalker interior aliases that need `tools/object_aliases.py`.
>
> **Infrastructure.** `hgss/game/app/main_3ds.c` (libctru/citro3d shell, real 3dsx link flags),
> `--check-overlays` (129 overlays, green on a temporary link) and `tools/symbol_crosscheck.py` +
> allowlist. Full audit: [agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md).

## Session 6 update (2026-10-31): the 34 symbols are closed

| Check | Session 5 | Now |
| --- | --- | --- |
| host32 compile | 1092/1092 | 1092/1092 |
| host32 link | 34 unresolved, no binary | **0 unresolved, `pokeheartgold-pc`** |
| `--check-overlays` | not on a real binary | **129 overlays (2 with static initializers), 0 failed** |
| `symbol_crosscheck.py` | not on a real binary | matched 21,407 · allowlisted 3,328 · **missing 0** |
| 3DS compile | ~1049/1094, 45 fail | **1092/1092, 0 failed** |
| 3DS archives | `std_sprintf` + ARM7 fail | **both build, 0 errors** |
| 3DS link | none | **0 unresolved, `pokeheartgold.3dsx`** |
| Python tests | 204 | **277** (+ 21 SDK suites) |

The 34 were closed as designed: `tools/object_aliases.py` runs as a build stage (the five Pokéwalker
aliases); `hgss/compat/src/msl_runtime.c` implements the bit-pattern MSL ABI and patch 1760 reconciles
the three TUs that declared the float form; `hgss/game/app/cpp_runtime.c` defines the six MWCC-mangled
C++ runtime names; `hgss/game/stubs/mb_stubs.c` is the documented multiboot stand-in; patch 1761 makes
the two duplicate globals file-local. The 3DS work is patches 1701-1703 (va_list/type fixes),
1710-1712 (the 41 devkitARM-only files), the ARM GAS `%progbits`/`%object` fix in the compat sound/NNS
data, and the two packaging fixes (enum overlay IDs, `.version` segment padding). Full audit:
[agent-notes/p2-session6.md](agent-notes/p2-session6.md).

## The 34 remaining unresolved symbols (session-5 measurement, kept for provenance)

| Count | Group | Cause / next step |
| --- | --- | --- |
| 11 | MSL runtime: `_fadd`, `_fdiv`, `_ffix`, `_fflt`, `_f_lltof`, `_f_ulltof`, `_ll_mul`, `_ll_udiv`, `_s32_div_f`, `strnicmp`, `__lower_mapC` | `msl_fp_runtime.c` is MWCC embedded asm; newlib has no such symbols. Implement them in a compat C file with the **bit-pattern** signatures the game declares (`u32 _fadd(u32,u32)`, `u32 _fflt(s32)`, `s32 _ffix(u32)`), plus `strnicmp` and `__lower_mapC`. The game is inconsistent: `pokeathlon_relay_run.c` declares `f32 _fflt(int)`, so a `SDK_PORT_3DS` patch or forced compat header must make the declarations agree. |
| 6 | C++ runtime: `Bytes::~Bytes()`, `Deque::~Deque()`, `operator new[]` (nothrow), `operator delete[]`, `std::__vector_common<true>::throw_length_error()`, `std::nothrow` | MWCC emits the two destructors (defined implicitly in `ppw_lobby.o`); GCC does not, and the port builds `-fno-exceptions -fno-rtti`. Either link libstdc++ or add explicit small definitions (recommended). |
| 12 | `MB_*` multiboot | No DS download-play consumer on PC/3DS; the allowlist already classifies MB as stubbed. Add documented no-op/error stand-ins (decision pending). |
| 5 | `gPokewalkerIr{ImageSizes,StrollSizes,MessageQueue,Transfer,SendBuffer}` | Not missing code: `pokewalker_ir_requests.alias-map.json` asks `tools/object_aliases.py` to add labels inside `gPokewalkerIrBuffers`/`Sync`/`Progress` at 0x4c/0x54, 0x7c/0x80, 0xc0. The DS build runs it; the harness does not. |

**Trap:** a link that prints `0 unresolved symbols` may have aborted before scanning every object (the
`errno` TLS error did exactly that). Read the whole `link.log`, not just the count.

## What exists (from the earlier sessions; still true)

| Piece | Tool / file | Verified by |
| --- | --- | --- |
| Game + library source list | `tools/game_sources.py` | `tests/test_game_sources.py` (23 tests); real tree: 1012 C, 56 C++, 150 ARM9 `.s`, 259 port-lib sources, 129 overlays. `lib/syscall/src/_secure_*.c` are filtered. |
| Overlay table + sections | `tools/game_overlays.py` | `tests/test_game_overlays.py` (33 tests); 129 overlays, `OVY_45` = 133 objects, `.sinit` + `.init_array` merged into `ovl_sinit_<name>`, `gHeartgoldOverlays` emitted. Harness output: 828 objects, 85 not in the build, 50 overlays with data, 2 with static initializers. |
| Generated headers | `tools/prepare_game_headers.py` | `tests/test_prepare_game_headers.py` (22 tests); 828 `msg_*.h` + 205 `.naix` + `fx_const.h`, byte-identical to `/work/pokeheartgold`'s matching build. |
| `.s` data files | `tools/s_to_gas.py` | `tests/test_s_to_gas.py` (24 tests); 150/150 assemble under `gcc-10 -m32` and `arm-none-eabi-gcc -march=armv6k`, 0 symbol/section/alignment differences. |
| Network / anti-piracy stubs | `tools/make_game_wifi_stubs.py`, `hgss/game/stubs/{wm_stubs.c,dsprot_stubs.c}` | `tests/test_game_stubs.py` (9 tests); the harness generated 273 Wi-Fi stand-ins. |
| Game build harness | `tools/hgss_game.py`, `hgss/game/app/{heartgold.c,main_pc.c,main_3ds.c}`, `make game-pc` / `game-3ds` | `tests/test_hgss_game.py` (28 tests now); `--target host32 --keep-going` compiles 1092/1092. |
| Overlay self-check | `Heartgold_CheckOverlays` (`heartgold.c`/`main_pc.c`) | 129 overlays (0 with static initializers), 0 failed — on a temporary link with unresolved symbols ignored. |
| Symbol cross-check | `tools/symbol_crosscheck.py` + allowlist | `tests/test_symbol_crosscheck.py`; synthetic symbol tables. Not yet run against a real PC ELF. |

## Commands

```bash
python3 tools/hgss_game.py --target host32 --keep-going --out /work/main-g61 -j 5
# 1092 compiled, 0 failed; 150 assembled, 0 failed; link 34 unresolved

python3 tools/hgss_game.py --target 3ds --keep-going --out /work/main-3ds -j 5
# ~1049/1094; 3DS libnitro still fails in std_sprintf.c (va_list arithmetic)
```

## The 3DS target

- **`hgss/std_sprintf.c`**: `invalid operands to binary + (va_list and int)` under devkitARM, which
  stops the 3DS `libnitro.a`. Fix at the source (a `SDK_PORT_3DS` patch or a compat `va_list` shim).
- **ARM7 rule**: the vendored rule feeds `hgss/compat/src/ds_sound_tables.c` (C) to the assembler path
  (`junk at end of line`, `unrecognized symbol type ""`). Needs a `.S`/`.c` distinction in the
  `tools/hgss_nitro.py` override (or build it as C with the ARM7 defines and rename it like the other
  compat files).
- **~45 devkitARM-only compile failures** remain (mostly `-Wincompatible-pointer-types`: Pokeathlon
  course helpers, Swalot Plop, dress-up, map-object/touch helpers, DWC error, and
  `ball_capsule_editor.c:3165`). These are 3DS-only diagnostics; host32 compiles the same files.
- `hgss/game/app/main_3ds.c` now exists and the libctru link resolves `main`/libctru symbols; the
  1556-unresolved figure was measured before the shell and before a 3DS port archive.

## Loose ends

- The duplicate-global census is 0 (patch 1761); the weaken pass remains for genuine header inlines.
- The overlay check and the cross-check both ran on the real binaries (see the session-6 update).
- The C++ runtime is six explicit definitions in `hgss/game/app/cpp_runtime.c`; the lobby builds with
  `-fno-exceptions -fno-rtti` and the vector throw helper aborts, as on a DS without exceptions.
- The cross-check allowlist is now explicit (3,398 named rows, no wildcards); its 48 game-source rows
  were audited: every owner is compiled and in the link set, and `--gc-sections` drops the unreferenced
  function (the DS linker kept every static object).

## Historical measurement (39 patches, kept for provenance)

```
python3 tools/hgss_game.py --target host32 --keep-going --out /work/out/game1
  1044/1094 C/C++ compiled        50 files failed (424 errors)
  150/150 .s assembled
  libnitro.a built                libnitrosystem.a FAILED (nns_all_headers.c unity file)
  186 Wi-Fi stubs generated
  link: 966 unique unresolved symbols (3989 reference lines)
```

3DS at 39: 1018/1094 compiled, 76 failed (511 errors), no archives because the ARM7 rule rejected
`ds_sound_tables.c`.

The old 50-file list is preserved in
[agent-notes/p2-slice-f.md](agent-notes/p2-slice-f.md). The session-5 compile work is in
[agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md).
