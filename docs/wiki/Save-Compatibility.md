# Save compatibility

This page explains why the save format must never change, what the save image contains, how the game chooses between its two slots, why the struct layout has to match between compilers, and what the tools in the repo check. It also lists what is not done yet.

Related pages: [Architecture](Architecture.md), [Patch-Stack](Patch-Stack.md) (the `1001` patch), [Decisions-Explained](Decisions-Explained.md), [Glossary](Glossary.md).

## The rule

The save format never changes. The save is a 524,288-byte image with the same offsets, struct layouts and checksums as the cartridge's. A save copied from the cart must load on the port, and a save written by the port must load on the cart or in an emulator ([gameplan.md](../../gameplan.md), ground rule 3, and the "Save compatibility specification"). [CONTRIBUTING.md](../../CONTRIBUTING.md) repeats the rule as a hard rule for every change.

The reason is that players keep saves. A save is copied between the cart, the emulators and the port as a plain file, so the port cannot have its own format. The plan's decision table records the same reason: "Cart ↔ port ↔ emulator interchange" ([gameplan.md](../../gameplan.md), decisions log).

## What the save image is

The image is the cartridge's 4 Mbit flash chip, copied as bytes.

- **Size.** 524,288 bytes (512 KiB). The tool's docstring and the plan both state this. The decomp loads the two halves from offsets `0 * 0x40000` and `1 * 0x40000` (`FlashLoadChunk` calls in `src/save.c`).
- **Two mirrors.** Each `0x40000` half is a mirror, so the save is kept twice. The game writes every chunk at its sector and again 64 sectors later, and 64 sectors of `0x1000` bytes is `0x40000` (`src/save.c`, the `hdr->sector` and `hdr->sector + 64` writes, and `SAVE_SECTOR_SIZE` in `include/constants/save_arrays.h`). It reads both mirrors when it chooses a slot ([tools/savetool.py](../../tools/savetool.py), docstring).
- **Two slots per mirror.** Slot 0 (the tool calls it "main") starts at offset 0 of the mirror. Slot 1 ("storage") starts at the next `0x100` boundary after slot 0. The decomp's `SaveData_InitSlotSpecs` does this rounding (`src/save.c`).
- **Slot sizes are not fixed in the docs.** They depend on the compiled size of the save structs. The tool therefore finds the sizes from the footers, and has a `--layout` option to set them by hand ([docs/decisions.md](../decisions.md), 2026-10-30).
- **Footer.** Each slot ends with a 16-byte footer, `struct SaveChunkFooter` in `include/save.h`. It holds `count` (u32), `size` (u32), `magic` (u32, always `0x20060623`), `slot` (u16) and `crc` (u16). The `size` field is the slot size including the footer.
- **Checksum.** The `crc` is CRC-16/CCITT (`MATH_CalcCRC16CCITT` in `lib/NitroSDK/src/math/MATH_crc.c`): polynomial `0x1021`, initial value `0xFFFF`, most significant bit first, no final XOR. It covers the slot minus its last 16 bytes ([tools/savetool.py](../../tools/savetool.py), docstring).

A slot is valid when its footer has the right size, magic and slot number, and its CRC matches. This is what the tool's `check_slot` function tests, and the comment on it says it is a port of the game's `ValidateSaveSectorFooter`.

## How the game picks a slot

Every valid slot has a `count`. The game picks the slot with the newer count when both are valid. The counts are u32 values and can wrap, so the comparison treats `0xFFFFFFFF` as older than `0` (`SaveCounterCompare` in `src/save.c`; the tool's `counter_compare` does the same).

The game checks both mirrors and reduces the results to one status. The status names and numbers are the game's (`include/save.h`):

| Status | Number | What the game does |
| --- | --- | --- |
| `NOT_EXIST` | 0 | No valid slot. The game initializes an empty save region (`Save_InitDynamicRegion`). The plan expects this to lead to New Game. |
| `IS_GOOD` | 1 | The slots pass their checks and agree. The game loads the newer one. |
| `SLOT_FAIL` | 2 | One slot fails its check and the other passes. The game loads the one that passes and sets a flag in `statusFlags`. |
| `TOTAL_FAIL` | 3 | The save cannot be used. The game sets a flag in `statusFlags` and initializes an empty save region. |

These branches are in the `switch` on `status` in `src/save.c`, after the status is computed. The tool's `load_status` is documented as a port of that function ([tools/savetool.py](../../tools/savetool.py), `load_status`). The exact rules for each case are in the tool's code, not restated here.

Erased flash reads as `0xFF` bytes. An erased chip has no footer magic, so it reads as `NOT_EXIST`. That is how a missing `.sav` file gives New Game ([gameplan.md](../../gameplan.md), save rule 2).

## The tool: savetool

`tools/savetool.py` normalizes, validates and compares save images. It is a standard-library Python program, and its three commands are:

| Command | What it does | Exit status |
| --- | --- | --- |
| `normalize IN [-o OUT]` | Strips a DeSmuME text footer, if present, and checks that the result is 524,288 bytes. | 0 on success, 2 on error. |
| `validate FILE [--layout SIZE0,SIZE1]` | Discovers the slot sizes (or takes them from `--layout`), checks each slot in both mirrors, and prints the status and the newer mirror. | 0 only for `IS_GOOD`, otherwise 1. |
| `diff A B [--block N]` | Lists the byte ranges that differ, in blocks of `N` bytes (default `0x1000`). | 0 if identical, 1 if not. |

The tool never edits its input. `normalize` writes a separate file if you give it `-o`. Its tests use synthetic images, as the test file's header says (`tests/test_savetool.py`, which has 15 test functions). Phase 0 records 15 tests passing ([docs/progress/phase-0.md](../progress/phase-0.md)).

Use it on any image before comparing saves. `validate` is the quickest way to see whether a save is intact and which slot the game would load.

## Why the struct layout must match

The game writes its save structs to the image as raw memory. A field's offset and the struct's size decide where its bytes go. If MWCC and GCC lay out a struct differently, the port writes data at other offsets than the cart does. The plan calls any such difference a release blocker ([gameplan.md](../../gameplan.md), Phase 1, step 7).

There are three ways GCC and MWCC can disagree here, and the plan names each one:

- **Enum size.** Both must be 4 bytes. The build uses `-fno-short-enums` to keep them 4 bytes.
- **64-bit integers.** This is the one that was measured. See the next section.
- **Bitfields, trailing padding and `wchar_t`/`u16` string arrays.** The plan lists these as things to check. The probe below skips bitfields, so bitfields are not covered by it.

## The 64-bit integer finding

MWCC aligns a `u64` or `s64` inside a struct to 4 bytes. The GCC ARM EABI, and GCC with `-malign-double`, align it to 8. The extra padding moves the fields after each 64-bit value.

The probe found the difference in three save structs. The patch message for `1001` gives the numbers: `POKEWALKER` (`irClock` at 252 against 256), `PCStorage` (74492 against 74496) and `BattleRecord` (7504 against 7512). The smaller figure in each pair is the 4-byte layout that MWCC produces. Each of the three structs holds a 64-bit field ([patches/pokeheartgold/1001](../../patches/pokeheartgold/) message; [docs/progress/phase-1.md](../progress/phase-1.md)).

The fix is patch `1001`. Under `SDK_PORT_3DS` only, `u64` and `s64` get `__attribute__((aligned(4)))`, and the DS build keeps its own typedef unchanged ([Patch-Stack](Patch-Stack.md)). After this patch the probe shows no differences for the host `-m32` build, the `-m32 -malign-double` build and the devkitARM build.

Because of this, the port's host `-malign-double` flag is safe only with patch `1001` in place ([docs/progress/phase-1.md](../progress/phase-1.md)).

## The layout probe in plain words

The probe compares how two compilers lay out the save structs. Here is what it does, in order ([tools/layout_probe/run_probe.py](../../tools/layout_probe/run_probe.py), docstring):

1. It finds the 45 save-chunk root types. These are the types whose sizes the save code uses, listed in the decomp's `src/save_arrays.c`.
2. It walks the debug information of those types, through every member, and writes a C file. The file holds one array of numbers: `sizeof(T)` for each type and `offsetof(T, member)` for each member.
3. It compiles that file with GCC in three configurations: `-m32`, `-m32 -malign-double`, and devkitARM with the port's ARM flags.
4. It compares each GCC array with the committed MWCC array in `tools/layout_probe/mwcc.txt`. Any difference makes the run fail.

The committed file has 804 lines, so the probe compares 804 numbers. Each line is a label and a value. The labels are struct and member names taken from the decomp's headers, and the values are numbers. The file holds no game data ([tools/layout_probe/compare.py](../../tools/layout_probe/compare.py), docstring).

The probe does not cover bitfields, because `offsetof` cannot be used on them. It also skips members of anonymous structs and unions, though their sizes still appear in the parent's numbers ([tools/layout_probe/gen_probe.py](../../tools/layout_probe/gen_probe.py), docstring).

The results recorded in the repo are:

- Before patch `1001`: 55 values differ, in the three structs above. The phase-1 record reports this for the devkitARM build and the `-malign-double` build. The host `-m32` build matched.
- After patch `1001`: 0 values differ, for all three configurations ([docs/progress/phase-1.md](../progress/phase-1.md)).

The `1001` patch message gives the number of values as 702. The committed `mwcc.txt` has 804 lines, and the phase-1 record says 804. This page uses 804, which is the count in the file the check reads.

## How to run it

The layout check is `make layout`, which runs `tests/layout_probe/run.sh`. From the repo root:

```sh
python3 tools/bootstrap.py            # build/upstream must exist
make layout                           # or: PY=/path/to/python bash tests/layout_probe/run.sh
```

What the run needs, from the script and its comments:

- A Python with `pyelftools`. Point `PY` at a virtual environment that has it.
- The decomp's generated `.naix` headers. The script reads them from `FILES`, and by default from `$DECOMP/files`, with `DECOMP` set to `/work/pokeheartgold` (the dev container's checkout). Set `DECOMP` or `FILES` to your own checkout.
- A `build/upstream` tree with the patches applied.
- The script says it is run inside the dev container, which has the GCC and devkitARM toolchains.

MWCC is not needed for the check. The check compares GCC against the committed `mwcc.txt`. MWCC under Wine is needed only to regenerate that file with `--update-mwcc` ([tools/layout_probe/run_probe.py](../../tools/layout_probe/run_probe.py), docstring). The `Makefile` comment on `make layout` says it needs MWCC, which is true only for regeneration.

The probe's scratch files go to the directory given by `--work`, which defaults to `/tmp/layout_probe`.

The probe is local. The plan lists it in the public `port.yml` workflow, but the workflow in the repo runs `make test` only, and the phase-1 record says `port.yml` has not been run on GitHub ([gameplan.md](../../gameplan.md), "CI workflows"; [.github/workflows/port.yml](../../.github/workflows/port.yml); [docs/progress/phase-1.md](../progress/phase-1.md)).

## Status of the plan's save rules

The plan lists six rules for the save ([gameplan.md](../../gameplan.md), "Rules"). This is what the repo has for each.

| Rule | Status |
| --- | --- |
| 1. Size exactly 524,288 bytes; reject other sizes; strip known emulator footers | `savetool normalize` and `validate` do this. The game's own load path in the port is not written yet (Phase 4). |
| 2. Erased flash reads `0xFF`, which means "no save" | Stated in the plan. The HGSS path is not run yet (Phase 4). |
| 3. Write-through: each completed write goes to the file, then `fsync` or the archive commit | Not started for HGSS. `port_card.c`, which the port reuses from Platinum, persists the flash image as a raw `.sav` file ([3ds_port/nitro/src/port_card.c](../../3ds_port/nitro/src/port_card.c) header). The CIA commit is not written. |
| 4. No port data inside the save; port settings go in `port.ini` | Planned. No `port.ini` exists yet. |
| 5. Console-bound values (MAC address, RTC offset) come from `port.ini` and fill fields only | Planned. |
| 6. Layout equality is checked by the probe | Done locally, with 0 differences ([docs/progress/phase-1.md](../progress/phase-1.md)). Not yet in CI. |

The plan's test list for saves (load a real save, port to melonDS and back, byte-level diff, interrupted write, Pokéwalker and Chatot data, hardware round trip) is not implemented. Only the layout probe and the savetool tests exist ([gameplan.md](../../gameplan.md), "Tests").

Two pitfalls from the plan are still open. The CIA save archive must be committed after each write, or a save can be lost if the console powers off. Two versions, HeartGold and SoulSilver, must keep separate save folders, so one build never loads the other's save ([gameplan.md](../../gameplan.md), Phase 5 and "Pitfalls").
