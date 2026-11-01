# Session 6 (2026-10-31): Phase 2 links on PC, Phase 3 lands, 3DS compiles

Container `/workspaces/pokeheartgold-3ds`, start commit `42e2224` (61 patches), end working tree
**72 patches** (`NitroSDK` 15, `NitroSystem` 3, `pokeheartgold` 54). This note is the audit trail for
the session: what was broken, the root cause, the fix and the measurement. Wave A ran four agents on
disjoint worktrees; the lead ran the DS gate.

Headline results:

| Check | Start | End |
| --- | --- | --- |
| host32 compile | 1092/1092 | 1092/1092 |
| host32 link | 34 unresolved, no binary | **0 unresolved, `pokeheartgold-pc` links** |
| `--check-overlays` | never on a real binary | **129 overlays, 0 failed** |
| `symbol_crosscheck.py` | never on a real binary | **match 21,407 · allowlisted 3,328 · missing 0** |
| 3DS archives | `std_sprintf` + ARM7 rule fail | **`libnitro.a` + `libnitrosystem.a` build, 0 errors** |
| 3DS game compile | 1051/1092, 41 fail | **1092/1092, 0 fail** |
| 3DS link | none | **0 unresolved, `pokeheartgold.3dsx` (9,001,956 bytes) packaged** |
| Python tests | 204 | **277** |
| DS gate | not passed at 61 | see the gate section |

---

## 1. The last 34 host symbols, closed in four classes

### 1.1 Pokéwalker aliases: a missing build stage (5)

`tools/hgss_game.py` now runs the decomp's `build/upstream/tools/object_aliases.py` after every
C/C++ compile and every `.s` assemble for a source with a sibling `.alias-map.json`, exactly as
`common.mk` does. Two adaptations were needed for the port's `-fdata-sections`/`-ffunction-sections`
layout: the map's `.bss`/`.text` names are translated to the actual `.bss.<name>`/`.text.<name>`
sections with the offset delta, and a FUNC owner's size comes from GCC's compiled object. OBJECT/BSS
sizes stay as the map has them, so a real layout drift still fails. `cpp_runtime.c` supplies the two
lobby destructors the maps would otherwise add, so those two aliases are filtered by name.

### 1.2 MSL runtime: an ABI, not a header (11)

`hgss/compat/src/msl_runtime.c` implements the bit-pattern entry points (`_fadd`, `_fdiv`, `_ffix`,
`_fflt`, `_f_lltof`, `_f_ulltof`, `_ll_mul`, `_ll_udiv`, `_s32_div_f`), the two-argument MSL
`strnicmp` (a full case-insensitive compare, not `strncasecmp`) and the 256-byte `__lower_mapC`.
Conversions go through `memcpy`, never a punning cast. The object is linked into the program only.

Three TUs declared the native float form and would have misread the bit-pattern helper on x86/ARM
hard-float: `pokeathlon_relay_run.c` (`f32 _fflt(int)`), `pokewalker_ir.c` (`float _f_ulltof`,
`float _fdiv`), `plaza_game_mime_jr_top.c` (`f32 _f_lltof`). Patch **1760** switches those files to
the typed helpers in `hgss/compat/include/hgss_port_msl.h` under `SDK_PORT_3DS`; the `#else` text is
byte-identical under `gcc-10 -E -P` (387,972 / 186,707 / 533,197 bytes).

### 1.3 C++ runtime: six exact MWCC-mangled names (6)

`hgss/game/app/cpp_runtime.c` defines `_ZN5BytesD1Ev`, `_ZN5DequeD1Ev`, `_ZnamRKSt9nothrow_t`,
`_ZdaPv`, `_ZNSt15__vector_commonILb1EE18throw_length_errorEv` and `_ZSt7nothrow` with their real
semantics: the destructors call the lobby's own C destroy routines (`LobbyCharVector_Destroy`,
`LobbyEventDeque_Destroy2`), `new[]` is `malloc` with a NULL result, `delete[]` is `free`, the vector
throw helper aborts. No libstdc++ is linked; the port keeps `-fno-exceptions -fno-rtti`.

### 1.4 Multiboot: documented stand-ins (12)

`hgss/game/stubs/mb_stubs.c` answers the twelve `MB_*` calls the Wi-Fi setup code makes. Nothing in
the port has DS download-play: `MB_Init`/`MB_StartParentFromIdle` return failure, `MB_GetSegmentLength`
returns 0, `MB_ReadSegment`/`MB_RegisterFile` return false, the rest are no-ops. Each definition says
what the DS call did. This is the one approved stub class (the user decided); the cross-check already
classified `MB_*` as stubbed.

### 1.5 The two duplicate globals

`FrontierScript_ReadByte` and `sListMenuTemplate` were defined in two objects that never coexist on
the DS. Patch **1761** makes the voltorb-flip copy of `sListMenuTemplate` and the pokegear-map copy of
`FrontierScript_ReadByte` file-local under `SDK_PORT_3DS` (the header declares the shared one). The
harness' duplicate census is now **0 symbols in more than one object**, so the weaken pass does
nothing and the overlay check cannot pass for the wrong reason.

## 2. The gates on the real PC binary

```
python3 tools/hgss_game.py --target host32 --keep-going --out /work/wA1/pc -j4
  1092 compiled, 0 failed · 150 assembled, 0 failed · linked
./build/pokeheartgold-pc --check-overlays
  [check] 129 overlays (2 with static initializers), 0 failed          (exit 0)
python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc \
    --reference /work/pokeheartgold.ref/build/heartgold.us/main.elf
  matched: 21407 · allowlisted: 3328 · missing: 0
```

The allowlist is now explicit: no `*`/`?` rows. It was generated from the real reference ELF; every
row carries the reason it is absent. Audit of the game-source rows: all 48 owners are objects in
`main.lsf`'s set and were compiled — the symbols are unreferenced, so `--gc-sections` drops them (the
DS linker kept every static object). The 34 symbols this session implemented are explicitly forbidden
from appearing in the table (`tests/test_allowlist.py`); a new omission shows up as `missing`.

The reference ELF is `/work/pokeheartgold.ref/build/heartgold.us/main.elf` (the 10-patch pass), not
`/work/pokeheartgold/...`: the gate tree is rebuilt from scratch and has no ELF at that path until it
links.

## 3. Phase 3: the data pipeline

- `builder/verify_rom.py` — accepts exactly HG US `4fcded0e…` and SS US `f8dc38ea…`, with header and
  size checks before the hash so trimmed/overdumped/patched/other-region images get a specific reason.
- `builder/extract_rom.py` — FNT/FAT walk with cycle/duplicate/unsafe-name/extent checks, keeps the
  original FAT IDs, and writes `manifest.json` (`{id,path,size}` + `rom_sha1`).
- `builder/pack.py` — default `--build-id` is now `heartgold-nitrofs-1`; the format is unchanged.
- Boot check — `hgss/game/app/heartgold.c` maps `NitroPort_OpenPak`'s results to words; the new
  `tests/sdk/pak_boot` suite links the real `heartgold.c` + `port_fs.c` + `pak.c` and checks OK, wrong
  build ID, wrong version, truncated/corrupt, missing files, pack/manifest mismatch and ROM SHA-1.
- SD layout — `docs/wiki/SD-Card-Layout.md` (the plan named `3ds_port/game/README.md`, which is
  byte-identical by rule; recorded in `docs/decisions.md`).
- Tests — `tests/test_verify_rom.py`, `tests/test_extract_rom.py`, `tests/test_pack.py`,
  `tests/synthetic_rom.py`: synthetic NitroFS images only.

**HGSS file table finding.** The retail HG image has **513 FAT entries but only 384 FNT names**: the
129 ARM9 overlays are unnamed FAT entries 0-128, not an `overlay/` directory. The extractor names them
`overlay/overlay_NNNN.bin` from the header's overlay table and keeps every entry; the pak is ID-indexed,
so the names are informational.

**Real-ROM smoke (not committed):** 513 files, 125,549,829 bytes extracted; 125,556,080-byte pack;
build ID `heartgold-nitrofs-1`; SHA-1 `4fcded0e…`; manifest IDs contiguous 0..512 and every size
matches the pack. SoulSilver is covered with synthetic images only (no ROM): recorded gap.

**The reader's allowlist was the last blocker.** `3ds_port/pak/src/pak.c` (byte-identical to
`platinum.lock`) hard-codes Platinum's two SHA-1s, so the real pack was rejected after every other
check passed. The harness now generates a build-private copy of the reader with the two HGSS digests
(`builder/verify_rom.py`'s `SUPPORTED`) prepended and compiles it as a program object, linked before
`libnitro.a`; the archive member is never loaded and `3ds_port/` stays untouched.

## 4. 3DS: `std_sprintf`, the ARM7 rule, and 41 devkitARM-only files

### 4.1 `std_sprintf.c` (patch 1701)

The file is written for MWCC, where `va_list` is a byte pointer (`va += 4; x = *(T *)(va - 4)`). On
ARM/newlib `va_list` is a struct and cannot be cast to a pointer. Under `SDK_PORT_3DS` every read is
now `va_arg` with the promoted type (`int`, `unsigned int`, `unsigned long long`, `const char *`,
pointer types for `%n`); that is correct on both GCC targets and keeps the h/hh truncation. `wm_core.c`
(patch 1702) had the same pattern. `gx/g3imm.c` + its header (patch 1703) take `uint32_t` under the
port to match the vendored declaration.

### 4.2 The ARM7 rule (`snd_arm7_data.c`)

Not `ds_sound_tables.c`. `snd_arm7_data.c` embeds x86-GAS directives in its `__asm__` string:
`.section .rodata,"a",@progbits` and `.type sym,@object`. On ARM GAS `@` starts a comment, hence
`junk at end of line` and `unrecognized symbol type ""`. The compat files now choose `%progbits` /
`%object` under `#if defined(__arm__)` and keep `@` for the host; `nns_data.c`,
`nns_tables_gfd.c` and `nns_tables_nns.c` were audited for the same class. Host32 objects are
byte-identical after `--strip-debug` (line numbers move with the added macro lines); the raw ARM7
`snd_arm7_data.o` and its renamed copy are sha256-identical before and after.

### 4.3 The 41 files (patches 1710-1712)

All `-Wincompatible-pointer-types`, all the same root: `s32`/`u32` are `signed long`/`unsigned long`
and the game mixes them with `int *` locals and tables; GCC 14 promotes the warning to an error.
Patches retype the locals/fields at the call sites (or the file-local prototype where the header was
wrong) under `SDK_PORT_3DS`, keeping the MWCC text under `#else`. 43 changed files, all byte-identical
under `gcc-10 -E -P` without the macro. No header prototype was changed, no warning suppressed.
`tests/test_game_3ds_types.py` proves the pre-fix call is rejected and both fix patterns compile.

### 4.4 Packaging the `.3dsx`

The static link was clean, but `3dsxtool` rejected the ELF twice. Both causes
are properties of the HGSS image, not of the link:

1. **Segment size not word-aligned.** `asm/middleware.s` keeps the middleware
   version strings (`_version_NINTENDO_DWC`, `_version_UBIQUITOUS_CPS`, ...) in
   a `.version` section, 0xdd bytes in HG. The default 3dsx linker script
   places that orphan last in the read-only LOAD segment, so `p_memsz` ended
   in 1. The harness renames the assembled object's section to
   `.rodata.version`; the script's `*(.rodata.*)` owns it and its trailing
   `ALIGN(4)` pads the segment. MWAS's section is untouched for the DS build.
2. **Absolute relocations with a value below the image base.** `FS_OVERLAY_ID`
   in C and `FS_EXTERN_OVERLAY` in `.s` refer to `SDK_OVERLAY_<name>_ID`,
   which `overlay_ids.ld` defines as the small overlay number. With
   `--emit-relocs` those became `R_ARM_ABS32` sites of value 0-128; 3dsxtool
   would add the load address to an overlay index. For `.s`, `s_to_gas.py`
   now substitutes the number at assembly time (`--overlay-ids`, the same
   generated script); for C, the generated header defines
   `SDK_PORT_OVERLAY_IDS_H`, which `lib/include/nitro/fs/overlay.h` already
   checks to use the enum constant instead of the symbol address (the port
   layer had the definition under a different guard name, so every C reference
   still took the address path).

After both, `3dsxtool` writes `pokeheartgold.3dsx` next to the ELF; the
harness packages it automatically after a successful link and reports a
missing tool instead of failing the build.

## 5. The DS gate

The five pinning tools were already re-pinned (1601/1602). Three stale pin
classes remained, and each one was found by running the gate, not by reading:

1. **Debug-info shape (1603).** `tools/msl-new-partition/*-shape.json` pinned
   the raw compiler product from before patch 1519. Regenerating both fixtures
   with the tool's own `shape.inventory()` shows differences in exactly
   `.debug_info` (payload hash), `.debug_line` (record/payload hash) and one
   `.dwarf_line.*` symbol (36 → 48 bytes; section length 226 → 238). Every
   allocated/executable section is identical, and the producer profile still
   asserts the code sizes and payloads.
   The first attempt still failed with `('tool drift', 'scalar-shape.json')`:
   `profile.json` pins the two fixtures as *modules*, so the patch must
   re-pin their hashes too. 1603 was amended to do that.
2. **Header closure (1762).** Patch 1703 adds a guarded branch to
   `lib/include/nitro/gx/g3imm.h`; the preprocessed text without the macro is
   identical, but the raw file hash changed and `tools/alarm-partition` pins
   it in `closure` → `production closure drift`.
3. **Producer source (1763).** `tools/msl-new-partition/select-{scalar,array}.py`
   assert the producer source bytes and still carried the *upstream* SHA-256s.
   Patches 1505/1519 changed `src/to91_msl_{scalar,array}_new.cpp` under the
   macro, so the gate stopped with `source bytes drift`. 1602 re-pinned
   `producers.*.source_sha256` but not the selectors, which are themselves
   pinned modules.

After 1763 the HEARTGOLD matching build at **72 patches** completed:

```
sha1sum -c heartgold.us/rom.sha1
build/heartgold.us/pokeheartgold.us.nds: OK
4fcded0e2713dc03929845de631d0932ea2b5a37   (retail)
```

SOULSILVER was run next at the same 72 patches:

```
build/soulsilver.us/pokesoulsilver.us.nds: OK
f8dc38ea20c17541a43b58c5e6d18c1732c7e582   (retail)
```

**Lesson.** The gate pins three different things in the same tree: raw header
bytes (`closure`), raw producer source bytes (selectors and `producers`), and
the retained compiler product's debug shape (`*-shape.json`). A guarded patch
that is byte-identical when preprocessed can still change any of the first
two; re-pin all three families together.

## 6. Process notes

- Four worktrees at `main`, one per agent; each bootstrapped its own `build/`, so the shim hazard was
  isolated. The lead's DS gate used its own clone at `/work/pokeheartgold`.
- Patch numbers were partitioned in advance (1701-1709 archives, 1710-1759 game compile, 1760-1779 PC
  runtime) so the merge had no patch-file collisions.
- The gate tree must be re-cloned after any patch change; `build/upstream` and `/work/pokeheartgold`
  are separate copies.
- `matching-build.sh` runs `make -j$(nproc)`; on a saturated container the qemu-i386 Wine compiler
  needs ~40-60 minutes for HG. A fake `nproc` returning a smaller number keeps the machine usable.
