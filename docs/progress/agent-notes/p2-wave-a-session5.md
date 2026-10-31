# Session 5 (2026-10-31): Phase 1 closes, Phase 2 compiles and nearly links

Container `/workspaces/pokeheartgold-3ds`, start commit `ea1b578` (39 patches), end working tree
**61 patches** (`NitroSDK` 15, `NitroSystem` 3, `pokeheartgold` 43). This note is the detailed audit
trail for the session: every failure, its root cause, the fix, and the evidence. It is written so the
next person does not rediscover any of it.

Headline results:

| Check | Start | End |
| --- | --- | --- |
| `make test` | green, 182 tests | green, **204 tests** + all SDK suites |
| `make port-tests` | hung in `test_arm7` | **exit 0**, every suite in all three subprojects |
| host32 game compile | 1044/1094 (50 fail) | **1092/1092, 0 fail** |
| ARM9 `.s` | 150/150 | 150/150 |
| `libnitro.a` / `libnitrosystem.a` | nitro only | **both build** |
| host32 link | 966 unresolved | **34 unresolved** |
| DS gate | not run since 10 patches | **still not passed** (new pin class found; see §7) |
| 3DS | 1018/1094, no archive | ~1049/1094, still blocked (see §9) |

---

## 1. The `test_arm7` spin was two NitroSDK revisions, not a missing pump

**Symptom.** `test_arm7` passed backup/RTC/power/touch, then spun at 99.9 % CPU in `test_sound`.

**Why the handoff diagnosis was stale.** `3ds_port/nitro/src/port_snd.c` already calls
`N7_SND_CommandProc()` synchronously in `NitroPort_HandleSound` when `sReady` is set, and
`NitroPort_SoundRender` drains the queue too. Nothing was missing there.

**Root cause.** The decompilation mixes two NitroSDK revisions in the sound library:

- `lib/NitroSDK/src/snd/SND_command.c` owns a **4-byte** `sFreeList` pointer, plus `sReserveList`,
  `sWaitingCommandListQueue` and the tag counters.
- `lib/NitroSDK/src/snd/snd_flush_command.c` is a later revision: it reads `sFreeList` as a
  **40-byte** private `CommandState` and writes `head/tail/pending/tag` through it.

On the DS these are separate objects and only the later one defines `SND_FlushCommand`. The port
links every SDK C file into one archive, so the later definition read a 4-byte pointer as a struct,
never saw `sReserveList`, and its stores landed on the physically adjacent `sCurrentTag` /
`sFinishedTag`. No command reached the virtual ARM7, so `finishedCommandTag` never advanced and
`SND_RecvCommandReply(SND_CMD_FLAG_BLOCK)` spun on `while (sFinishedTag == tag)`.

**Fix.** Patch `1404-sound-keep-the-ARM9-command-flush-with-the-state-it-updates.patch`:

- under `SDK_PORT_3DS`, `SND_command.c` gains the NitroSDK 4.2 `SND_FlushCommand` next to the state
  it updates;
- `snd_flush_command.c` compiles its body only under `#else`.

Both files are byte-identical under `gcc-10 -E -P` without `SDK_PORT_3DS`. New
`tests/sdk/arm7_sound/test_arm9_flush.c` (24 checks) proves a blocking flush hands over the pushed
list and that the tag is finished once the reply is reaped. `test_arm7`: **676 checks, 0 failures**.

**Lesson.** "The port links two files the DS kept apart" is a recurring class. When a vendored test
spins, check whether two decomp files disagree about a shared global's type before assuming a missing
thread or pump.

## 2. NitroSystem: three declaration sets, then a real `.bss` layout bug

`libnitrosystem.a` failed in the generated `nns_all_headers.c` unity file.

**Header problems.**

1. `nnsys/snd/main.h` (uses `NNSSndHandle`) came before `nnsys/snd/player.h` (defines it), because the
   unity file includes headers in path order. `hgss/compat/include/nitro/snd.h` now includes
   `nnsys/snd/heap.h` and `player.h` before any NNS header.
2. Four `SND_*` functions had three disagreeing sets of declarations: HGSS's flat
   `SND_interface.h`/`SND_command.h`, `lib/include/nnsys/snd_internal.h`, and the compat
   `nitro/snd/ARM9/interface.h`. The flat headers **cannot** simply be included in the compat header:
   their `SND_work_shared.h` redefines `SNDChannelInfo`/`SNDTrackInfo` against the public headers.
   Resolution:
   - `nitro/snd/common/command.h` keeps only an incomplete `struct SNDCommand`;
     `nnsys/snd_internal.h` is the single `SND_RecvCommandReply` declaration.
   - `nitro/snd/ARM9/interface.h` leaves the four disputed functions **unprototyped** (C accepts that
     against both `snd_internal.h` and patch 1509) and documents why.
   - `nitro/snd/common/work.h` tags the compat `SNDDriverInfo` as `struct SNDDriverInfo` with the
     ntrtwl member names (`workAddress`, `chCtrl`), same 0x11E0 layout.
   - new `hgss/compat/include/nnsys/hgss_compat.h` supplies ntrtwl names HGSS's partial
     decompilation lacks (fnd heap wrappers, GFD alloc/key-size helpers, an OAM-manager mirror,
     `NNS_G2dGetCellDataByIdx`, `NNS_G3dGetMdlByName`).

**The crash was a real decomp bug.** After the headers, `test_nitrosystem` segfaulted in
`NNS_GfdInitLnkTexVramManager`:

- `lib/NitroSystem/src/gfd_linked_list_tex_vram_man_reset.c` defines `gLnkTexVramFreeList4x4`,
  `gLnkTexVramFreeList` and `gLnkTexVramWork` as three separate globals.
- `gfd_linked_list_tex_vram_man.c` casts `&gLnkTexVramFreeList` to `LnkTexVramManager` and expects
  `freeList`, `freeList4x4`, `work` at +0/+4/+8.
- GCC's `.bss` order was `gLnkTexVramWork=0x0`, `gLnkTexVramFreeList=0x14`,
  `gLnkTexVramFreeList4x4=0x18`. Init wrote its block pool through unrelated storage.

**Fix.** `patches/NitroSystem/0303` emits one ordered `.bss` block with the three names at the DS
offsets under `SDK_PORT_3DS`; the `#else` is the original text. Identity proof: 5155 lines, 0-line
diff. The ARM constructs also assemble with `arm-none-eabi-gcc`.
Result: `test_nitrosystem` **17,122 checks, 0 failed**.

**Harness fixes in `tools/hgss_nitro.py`.**

- `NNS_INLINE_SYMS` was hardcoded to `3ds_port/nitrosystem/build/<target>/gen/...` even with
  `--out`; it now lives under `<out>/gen`.
- HGSS's NNS headers are all `static inline`, so `nm` legitimately produces an empty weaken list and
  binutils 2.42's `objcopy --weaken-symbols=` fails on it with exit 1 and no message. An `NM` wrapper
  appends one harmless nonexistent `T` name when nm runs on `nns_all_headers.o`.

## 3. The whole vendored suite order became reachable — and exposed four more defects

Fixing `test_arm7` and `test_nitrosystem` let the run continue into suites nobody had executed at 39
patches. Each failure was a real port defect:

1. **Silent sequence (`test_sound_sequence`, 1 check).**
   - The port modelled the BIOS `SVC_GetVolumeTable` as `round(127 * 10^(dB/20))`. NitroSDK 4.2's
     real 724-entry table keeps low entries non-zero. With the synthetic bank's `attack = 0`, every
     early envelope step clamped to the formula's 0 and the channel was silent.
   - Patch 1401 also mapped the ARM7 register writes **0x400 too low**. `NitroPort_SoundRegisters`
     is indexed by the DS address's low 16 bits (SOUNDCNT at 0x500, channel 4 at 0x440), but the
     `R8/R16/R32` macros subtracted 0x400 and the direct writes used `channelOffset` alone. The
     mixer read all-zero channel/master registers.
   - Fixed in `hgss/compat/src/ds_sound_tables.c` (the real 724-entry table) and patch **1405**
     (absolute offsets in five ARM7 sound files).
2. **`test_gx3d` (6 matrix/texture-coordinate checks).** `hgss/compat/include/nitro/gx/g3imm.h`'s
   `G3_StoreMtx`, `G3_RestoreMtx`, `G3_Scale`, `G3_TexCoord` wrote hardware registers directly. The
   port's geometry engine only sees `NitroPort_GxDirectWrite`. Forwarded under `SDK_PORT_3DS`.
3. **`test_vram_transfer` (segfault).** `GX_LoadTex`/`GX_LoadTexPltt` in
   `lib/NitroSDK/src/gx/gx_load3d.c` still copied to raw `0x06800000`; nothing is mapped there, so the
   queued 3D texture transfer crashed when `OS_Halt` ran it. Patch **1406** maps the table's LCDC page
   numbers onto `NitroPort_LcdcVram` (zero entries stay zero so the two-block path is not mistaken
   for a mapped address).
4. `tests/sdk/arm7_sound/test_arm7_sound.c` expectations moved to the absolute offsets (same number
   of checks, not weakened).

After these, the whole nitro subproject is green: `sound_sequence` 12/0, `render` 2071/0, `gx3d`
326/0 (+228 on its thread), `gx3d_plumbing` 231/0, `vram_transfer` 24/0, `upscale` 160/0,
`lowermenu` 246/0, `options` 49/0, `xbrz` 266/0, `shadow` 375/0.

**Follow-up noted but not done.** `gx_load3d.c`'s *clear-image* loaders still use raw LCDC addresses;
the tested texture and texture-palette paths are mapped. Fix if the game uses clear images.

## 4. Everything compiles: patches 1509–1525

Three batches, 17 patches, all `SDK_PORT_3DS`-guarded with `#else` original text and a `gcc-10 -E -P`
identity proof.

**Batch 1 (1509–1522, 45 files).**

- **1509** `nnsys/snd_internal.h` repeats four `SND_*` declarations with types that disagree with the
  port's headers; under the port it repeats theirs. This alone fixed eight game TUs.
- **1510** 38 case labels had a declaration as the next statement. C99 6.8.1 requires a statement;
  MWCC accepted the declaration. An empty `;` under the port fixes it and clears the cascaded
  "undeclared" errors.
- **1511** eight files redeclared SDK functions with the wrong types
  (`CTRDG_IdentifyAgbBackup`, `MI_CompressLZImpl`, `RTC_GetTime/Date/DateTime`, `strncmp`,
  `__msl_assertion_failed`, `MATHCRC32Table`, `GX_IsHBlank`, `GXx_SetMasterBrightness_`).
- **1512** WiFi Plaza: drop `static` where the internal header declares the function non-static
  (1502's pattern).
- **1513** `battle_main` registers its overlay static initializer with
  `__attribute__((section(".sinit"), used))`, as 1401 did for `static_init.h`.
- **1514** `comm_state_private.h` takes `MATHRandContext32` from the port's `rand.h` (1201's CRC
  class).
- **1515** `comm_state` drops `const` from the `linkedStates` copy destination so `ARRAY_ASSIGN`
  (which assigns a struct through a pointer) is legal in GCC.
- **1516** `battle_command` uses the port's hooked square-root helpers; the local bodies bypassed
  `CPi_PortUpdateSqrt`.
- **1517** `pokeathlon_box_graphics` copies the 5-byte medal row instead of assigning it.
- **1518** `battle_hp_bar` includes its data header from beside the file (the harness has no `-Isrc`).
- **1519/1520** MSL and lobby `operator new` take `__SIZE_TYPE__`; the scalar nothrow overload drops
  its `try/catch` under `-fno-exceptions`.
- **1521** PPW lobby takes `va_list`/`memcmp`/`memcpy`/`memmove`/`memset`/`strlen` from the global
  namespace (glibc does not put them in `std`).
- **1522** PPW lobby writes the `PlayerRecord` designators in declaration order.

**Batch 2 (1523–1525).**

- **1523/1524** `wifi_plaza_news` (33 helpers) and `nintendo_wfc_connect_unused_session` (57 state
  functions) repeated their header prototypes as `static`; a file-scope macro (nothing on the port,
  `static` on the DS) fixes the linkage without touching the DS text.
- **1525** `ball_capsule_editor` repeats the `SDK_FORCE_EXPORT`/`#pragma section` static-initializer
  pattern; GCC parsed `__declspec` as a function declaration and swallowed the rest of the file
  (164 of 165 errors were cascades). `__attribute__((section(".sinit"), used))` under the port.

Result: **1092/1092 compile, 0 failures**, `150/150` `.s`.

**The two `lib/syscall/src/_secure_IP*.c` files are not patched.** They are embedded MWCC asm images
of the DS secure area (one per game, selected by `lib/syscall/Makefile`), whose SVC aliases the
matching build adds with `tools/object_aliases.py`. No source references them, GCC cannot parse them,
and the bytes are proprietary firmware. `tools/game_sources.py` filters
`lib/syscall/src/_secure_*.c`; no stub was added, so the source-list issue is visible.

## 5. Library set and the 966 → 34 symbol grind

The unresolved set was classified by joining the link errors against `nm` over the 1,762 objects of
the matching DS build (`/work/pokeheartgold.ref/build/heartgold.us`):

| Start | Group | Action | Result |
| --- | --- | --- | --- |
| 233 | `lib/NitroSystem/src/*` | patch 0303 + link `libnitrosystem.a` | resolved |
| ~300 | `lib/NitroDWC/asm/*` (lobby chat + wifi_setup tables) | link the data, keep the functions stubbed | resolved |
| 345 | `src/**` game objects | patches 1509–1525 | resolved |
| 7 | `lib/MSL_C/src/msl_fp_runtime.c`, `msl_string_extras.c` | not yet | **open** |
| 2 | C++ runtime (`Bytes::~Bytes`, vector throw) | not yet | **open** |
| 13 | `MB_*` multiboot | not yet | **open** |

**NitroDWC is data, not just stubs.** All 21 `lib/NitroDWC/asm/*.o` objects have no `T` symbols: they
are protocol tables, rodata and bss that the game reads directly. The harness now links them via
`LINK_DATA_EXCEPTION_PREFIXES = ("lib/NitroDWC/asm/",)`, while `lib/NitroDWC/src` stays stubbed.

**Four of them must stay out.** `dwc_gamespy_bss.o`, `dwc_lib_bss.o`, `dwc_lib_data.o` and
`dwc_lib_rodata.o` reference `errno` as an ordinary symbol; glibc defines `errno` as TLS and `ld`
aborts with `TLS definition ... mismatches non-TLS reference`. They are DWC-internal libc state that
nothing in the game reads, so `LINK_DATA_EXCLUDED` drops them.

**Trap: "0 unresolved" can be a lie.** The first link with all DWC data printed
`link failed (0 unresolved symbols)` because `ld` aborted on the `errno` TLS error before it finished
scanning objects; the harness parses only the lines `ld` emitted. Only after removing the clash did
the real count appear: 34.

**The 34 remaining, grouped.**

- MSL soft-float/runtime (11): `_fadd`, `_fdiv`, `_ffix`, `_fflt`, `_f_lltof`, `_f_ulltof`, `_ll_mul`,
  `_ll_udiv`, `_s32_div_f`, `strnicmp`, `__lower_mapC`.
- C++ runtime (6): `Bytes::~Bytes()`, `Deque::~Deque()`, `operator new[]` (nothrow),
  `operator delete[]`, `std::__vector_common<true>::throw_length_error()`, `std::nothrow`.
- Multiboot (12): `MB_CommGetChildUser`, `MB_CommIsBootable`, `MB_CommResponseRequest`,
  `MB_CommSetParentStateCallback`, `MB_DisconnectChild`, `MB_End`, `MB_GetSegmentLength`, `MB_Init`,
  `MB_ReadSegment`, `MB_RegisterFile`, `MB_SetParentCommParam`, `MB_StartParentFromIdle`.
- Pokéwalker interior aliases (5): `gPokewalkerIrImageSizes`, `gPokewalkerIrStrollSizes`,
  `gPokewalkerIrMessageQueue`, `gPokewalkerIrTransfer`, `gPokewalkerIrSendBuffer`.

**The pokéwalker five are a missing build stage, not missing code.**
`src/application/pokewalker/pokewalker_ir_requests.alias-map.json` asks
`tools/object_aliases.py` to add labels *inside* `gPokewalkerIrBuffers`/`gPokewalkerIrSync`/
`gPokewalkerIrProgress` (offsets 0x4c/0x54, 0x7c/0x80, 0xc0). The DS build runs that tool after
compiling; `tools/hgss_game.py` does not. The same mechanism covers runs of hand-written Thumb stubs.
**Next:** run `object_aliases.py` over the compiled game objects that have an `.alias-map.json`.

**MSL soft-float is an ABI trap.** The game declares the same runtime names with *different*
signatures in different TUs: `wifi_plaza_instrument.c` uses bit-pattern forms
(`u32 _fadd(u32,u32)`, `u32 _fflt(s32)`, `s32 _ffix(u32)`), while `pokeathlon_relay_run.c` declares
`f32 _fflt(int)` and passes a float. On the DS soft-float ABI both are one register; on x86 and on
ARM hard-float they are different calling conventions. A naive C implementation would silently
misinterpret one set. The fix needs a compat header with the bit-pattern prototypes plus a
`SDK_PORT_3DS` patch for the odd declaration(s), not just a C file.

**C++ runtime.** `Bytes::~Bytes()` is defined by MWCC in `src/lobby/ppw_lobby.o` but GCC does not
emit it: `struct Bytes` (with a declared out-of-line destructor) appears only in
`ppw_lobby_update_my_profile.cpp`, and the TU that uses it gets MWCC's emission but GCC's does not.
`Deque::~Deque()` is the same class of missing C++ runtime emission. The vector throw helper is the
libstdc++/MSL runtime piece. Options: link `libstdc++`, or add explicit definitions under the port
(the port builds with `-fno-exceptions -fno-rtti`).

## 6. Phase 2 infrastructure (from the parallel slice, commit `07ca172`)

- `hgss/game/app/main_3ds.c`: libctru/citro3d shell, console profile → DS firmware settings, game on
  its own thread, HOME menu/frame clock/buttons. The harness now links a normal 3dsx program
  (`-specs=3dsx.specs`, `-lcitro3d -lctru`) and selects this main for `--target 3ds`. The 3DS static
  link no longer misses `main`/libctru symbols.
- `--check-overlays`: `Heartgold_CheckOverlays()` registers `gHeartgoldOverlays`, then for all 129
  overlays corrupts `.data`/`.bss`, loads twice through `FS_LoadOverlay`, and verifies `.data`
  restored, `.bss` zero, loaded/unloaded state. Proof on a temporary link (unresolved ignored):
  `129 overlays (0 with static initializers), 0 failed`.
- `tools/symbol_crosscheck.py` + allowlist: compares the PC ELF's global C symbols against the DS
  reference ELF, classifies matched/allowlisted/missing, exits 1 on missing. Unit tests use synthetic
  symbol tables. Not yet run against a real PC ELF.

## 7. The DS gate: five pinning tools, three pin classes, and a path that must match

This consumed several cycles. The gate has still **not passed** at the final stack; the evidence so
far is per-patch preprocessor identity.

**Patches 1601/1602.** Five decomp build tools refuse to run when an input hash changes:

| Tool | Error | Pins |
| --- | --- | --- |
| `tools/arm7-alarm-partition/build.py` | `physical input drift` | `C_header_pins` |
| `tools/alarm-partition/build.py` | `production closure drift` | `closure` |
| `tools/msl-new-partition/build.py` | `header drift`, `unregistered source`, `complete producer … drift` | `headers`, `producers.*.source_sha256`, `*-shape.json` |
| `tools/msl-d1-partition/build.py` | same | `headers`, `source_sha256` |
| `tools/dwc-iterator-advance/build.py` | same | `headers`, `source_sha256`, shape |

1601 re-pinned the two alarm profiles. 1602 re-pinned the three MSL/DWC profiles for:

- `lib/include/nitro/types.h` — patch 1001;
- `lib/NitroDWC/src/tree-iterator4.h` — the pinned upstream commit itself (`8cbb4a1`);
- `src/to91_msl_{scalar,array}_new.cpp` — patches 1505 **and** 1519 (the second was missed on the
  first pass and 1602 had to be amended).

**The path is part of the pin.** The MSL/DWC tools pin the *complete producer object*, and MWCC
embeds the absolute source directory (`Z:\work\pokeheartgold\src\…`) in a payload. A build from
`/work/patched` can therefore never match, even with identical sources; the gate must clone to
**`/work/pokeheartgold`**. The documented procedure used `/work/patched`; both
`docs/wiki/Daily-Workflow.md` and `docs/progress/phase-1.md` are corrected.

**A further, still-open pin class.** After fixing the source hashes, the gate at 61 patches stopped
in `msl-new-partition` with `complete producer section/symbol/relocation/payload drift`. The newly
built `to91_msl_scalar_new.compiler-full.o` differs from the pinned shape in two payload hashes, a
section length (238 vs 226) and a symbol size (48 vs 36) — all in debug/line-table sections. Adding
lines under `#ifdef SDK_PORT_3DS` shifts source line numbers, so MWCC's debug info changes even
though the code does not. **Next:** regenerate `scalar-shape.json`/`array-shape.json` with the tool's
own normalizer (do not hand-edit), as another 16xx re-pin patch, then rerun. Until then, the gate
cannot pass by construction.

**Other gate facts.**

- The gate needs `make -C tools/mwasmarm_patcher` first (git-ignored binary) and
  `make patch_mwasmarm MWCCVER=2.0/sp2p3`.
- The last full pass was at **10 patches**. Everything since is guarded and identity-proven, but the
  ROM SHA1 is the only strong proof.
- `dwc-iterator-advance` pins `lib/NitroDWC/src/to91_ov45_iterator_advance108.cpp`, which does not
  exist at the pinned commit; that tool is not reached by this build. Do not "fix" the pin without
  understanding why.

## 8. Harness/process traps found this session

- **A test mutated the real shim.** `tests/test_hgss_game.py` called `make_shim()` with a fake `/up`
  root; that re-pointed `build/hgss-sdk` at `/up`. A harness compiling concurrently then failed in
  763 files with `nnsys/snd/sndarc.h` incomplete-type errors. The tree was fine; the shim was not.
  The test now redirects `hgss_nitro.BUILD` to a temp dir. **Never run `make test` and the harness at
  the same time until this class of shared-state mutation is ruled out.**
- Running `make test` *after* a harness can also repair the shims to `build/upstream` (make_shim in
  the test used `/up` before the fix; after it, the test is isolated).
- `bootstrap --clean` deletes `build/upstream`; never run it while any build reads the tree. It broke
  one harness run this session (`/work/main-g58` was started before a bootstrap).
- `build/` is a volume: Mac and container see different trees. Verify patches in the container.
- Instrumented copies under `/tmp` remain the only practical way to locate a crash (gdb cannot attach
  under qemu-user; cores are off).
- The harness's `--out` is not fully private for the vendored NNS build until the `NNS_INLINE_SYMS`
  override added this session; it is now.
- `link failed (0 unresolved symbols)` is not proof of a clean link if `ld` aborted early on an
  unrelated error (see §5).

## 9. 3DS target: what still blocks it

- `hgss/std_sprintf.c`: `invalid operands to binary + (va_list and int)` under devkitARM; this stops
  the 3DS `libnitro.a`.
- `hgss/compat/src/ds_sound_tables.c`: the vendored ARM7 rule feeds a C file to the assembler path
  (`junk at end of line`, `unrecognized symbol type ""`). Needs a rule-side `.S`/`.c` distinction or
  a rename/build-as-C override in `tools/hgss_nitro.py`.
- ~45 devkitARM-only game compile failures remain (mostly `-Wincompatible-pointer-types` in
  Pokeathlon course helpers, Swalot Plop, dress-up, map-object/touch helpers, DWC error, and
  `ball_capsule_editor.c:3165`). Last full measurement at 56 patches: 43 failed / 150 errors.
- With `main_3ds.c` present, the 3DS static link no longer misses `main`/libctru symbols; the earlier
  1556-unresolved count was measured without the shell and without the 3DS port archive.

## 10. Facts worth remembering

- `test_arm7`'s pass count is 676; `test_nitrosystem` is 17,122; `make port-tests` is the only target
  that runs all three subprojects.
- The harness now reports `129 overlays (50 with data, 2 with static initializers), 828 objects
  (85 not in this build)` and 273 Wi-Fi stand-ins.
- The two duplicate globals the harness weakens (`FrontierScript_ReadByte`, `sListMenuTemplate`) are
  still a loose end: the overlay check can pass for the wrong reason until they get a real
  per-overlay/local fix.
- Save layout was never touched; no game content entered the repo; no test was weakened; no blanket
  game-symbol stub was added.
