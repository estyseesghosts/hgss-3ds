# Phase 1: getting the vendored suites to actually run (issues and fixes)

Date: 2026-10-31, session 4. Container `/workspaces/pokeheartgold-3ds`, patch stack at **39 patches**
(commit `e242375`). This note is the debugging log for the step that took the vendored `3ds_port`
unit tests from "links, or crashes" to "702 + 120 + 68 checks green, one hang left". It exists
because nearly every failure here was **not** a compile error: the code built and then did the wrong
thing, or wrote through a DS address that is not mapped on the host.

Method, used for every crash below:

1. Run the failing binary directly, not through make (`make` hides the first line of output).
2. If it segfaults, `ulimit -c` is 0 and qemu-user writes no core, so gdb cannot attach
   (`PTRACE_GETREGS: Input/output error`). Copy the vendored test to `/tmp`, insert
   `fprintf(stderr, "TRACE <line>\n"); fflush(stderr);` after (or before) each statement, rebuild the
   copy against the same `libnitro.a`, and read the last line printed. The vendored file is never
   edited in the repo.
3. Fix at the lowest layer that explains it: a missing compat header, a `SDK_PORT_3DS` patch, or a
   port-layer C file. `3ds_port/` stays byte-identical.

## 1. `GXFifoStat` was only half guarded (patch 1203)

**Symptom:** `make -C 3ds_port/nitro lib` failed with `redeclaration of enumerator GX_FIFOSTAT_EMPTY`
and `conflicting types for G3X_GetCommandFifoStatus` in `mi/mi_dma_gxcommand.c`.

**Diagnosis:** patch 1202 guarded the file's local `GXFifoIntrCond` copy with
`#ifndef HGSS_COMPAT_GX_G3X_H`, but not the `GXFifoStat` enum or the getter below it. The compat
header (added later, for `test_threads`) supplies both, so the guarded branch was skipped while the
unguarded one was compiled.

**Trap that hid this:** the Mac's `build/upstream` and the container's are different trees. `build/`
is a named volume (`hgss-build`), so the Mac copy held hand edits that were never exported into the
patch. Reading the file on the Mac showed the guard; the container compiled a file without it.
**Rule: when a patch is suspect, read the file inside the container** (`devcontainer exec ... sed`),
not the Mac path.

**Fix:** `patches/pokeheartgold/1203`, wrapping the enum and the getter in the same guard. Identity
checked (`gcc -E -P` without the macro is byte-identical).

## 2. ARM7 sound: four missing symbols were BIOS/power-manager, not driver code

**Symptom:** `test_nitro` failed to link: `SVC_GetPitchTable`, `SVC_GetVolumeTable`,
`PMi_SetControl`, `PMi_ResetControl`.

**Diagnosis:** HGSS's ARM7 driver reaches the pitch/volume tables through BIOS SWIs and the PM
control register through the ARM7 SPI bus. The port has neither a BIOS nor an ARM7 SPI model, and
`sub/lib/src/power_manager_driver.c` (which owns `PMi_*`) is not part of the compiled set.

**Fix:** `hgss/compat/src/ds_sound_tables.c` (new, listed in `ARM7_COMPAT_FILES` so the Makefile's
`N7_*` renaming applies to it):
- `SVC_GetPitchTable(i) = round(65536 * (2^(i/768) - 1))`, matching the driver's `+ 0x10000`.
- `SVC_GetVolumeTable(i) = round(127 * 10^((i-723)/20))`, 723 = 0 dB, clamped.
- `PMi_SetControl/ResetControl` keep a shadow byte; only the sleep path reads it back, and the port
  mixer does not sleep. Documented as such in the file.

## 3. `assert` in vendored `stb_image.h` (port_wideart.c)

**Symptom:** undefined `assert` at link, "implicit declaration of function 'assert'".

**Diagnosis:** `third_party/stb_image.h` defines `STBI_ASSERT(x) assert(x)` and includes no
`<assert.h>`. A plain `-include assert.h` did not help: HGSS ships `include/assert.h` (which defines
only `GF_ASSERT`) earlier on the include path, so the forced header was the wrong one.

**Fix:** force-include HGSS's *MSL* header by absolute path
(`-include <root>/lib/include/assert.h`), which expands `assert` to `__msl_assertion_failed`, and
implement that function in `hgss/compat/src/sdk_standins.c` (prints and `abort()`s). The assertions
stay live; no no-op stub.

## 4. C99 math called without `<math.h>` (silent ABI bug, then a hard link error)

**Symptom:** first warnings (`fabsf`, `floorf`, … declared implicitly), then
`undefined reference to 'isfinite'` in `port_gx3d_prune.c`.

**Diagnosis:** several vendored files call C99 math without including `<math.h>`. An implicit
declaration makes GCC assume `int` return, so a `double` result comes back through the wrong register
class — silently wrong values, not just a warning. `isfinite` is a macro over a builtin, so an
undeclared call instead leaves a real symbol that libm does not export. The system `<math.h>` is
shadowed by HGSS's flat `lib/NitroSDK/include/math.h`.

**Fix:** `hgss/compat/include/hgss_port_math.h` declares exactly the surface the port uses (with the
standard prototypes) and defines `isfinite/isnan/isinf/signbit` over `__builtin_*`. It is
force-included by `hgss_nitro.py` for every port compile, so TUs that never include anything still get
it.

## 5. `test_gx3d` pulled the VRAM transfer table and its 36 references

**Symptom:** after exporting `gVramTransferFuncTable`, `test_gx3d` failed with 36 undefined
`DoTransfer2d*` symbols.

**Diagnosis:** the table lives in `hgss/compat/src/nns_tables_gfd.c` so that `test_vram_transfer`
(which compiles `gfd_vram_transfer_manager.c` directly) can link it. Making it global let *any* link
that pulls the archive member require the 36 transfer functions, which only that one test compiles.

**Fix, in two steps:**
1. Declare all 36 entries `.weak`, so a link that only needs the table gets null slots instead of
   undefined symbols; the test that provides the real functions overrides them.
2. `NNS_GfdGetVramTransferTaskTotalSize` (ntrtwl API HGSS lacks) reads the queue, and the queue's
   real definition is in the manager file, so the same object now carries a **weak** zeroed
   `gVramTransferTaskQueue` as well. `test_vram_transfer`'s strong definition wins there.

## 6. `GX_LoadBGPltt` wrote to `0x05000000` — the first real "wrong address" crash

**Symptom:** `test_nitro` segfaulted in `test_dma`, at `GX_LoadBGPltt`.

**Diagnosis:** `gx_load2d.c` loads palettes with `(void *)(0x5000000 + offset)` and OAM with
`(void *)(0x7000000 + offset)` — raw DS addresses. Patches 1301/1302 remap the `HW_*` macros, but these
literals never went through them, so on the port they wrote into unmapped host memory.

**Fix:** `patches/pokeheartgold/1303` turns each literal into a named macro defined twice: the
original literal for MWCC (so `gcc -E -P` is byte-identical) and the corresponding `HW_*` area for the
port. Covers the eight palette/OAM loaders, the two extended-palette pointers, and the five LCDC block
addresses. `gx_load3d.c` is deliberately untouched: its `0x06800000 >> 12` entries are VRAM bank
numbers written to `GX_VRAMCNT`, not host pointers.

## 7. `MATH_CountPopulation` returned a byte, callers read a word (patch 0303)

**Symptom:** one check failed: `MATH_CountPopulation(0xF0F0F0F1) = 67636241 (0x4080C11)`, expected 17.
Note `0x4080C11 & 0xFF == 17`.

**Diagnosis:** HGSS defines it as `u8` but declares it as `int` in `nitro/wm/wm_internal.h:155` and
`vct/vct_audio_internal.h:155`. On the ARM EABI the callee only promises the low byte of r0; GCC
leaves the rest as the last intermediate value. MWCC happens to leave the register zero-extended for
this body, which is why the DS and the game work.

**Fix:** under `SDK_PORT_3DS` the declaration and definition return `u32` (same value, zero-extended).
MWCC keeps its two original lines.

## 8. `CARD_GetBackupPageSize` read the wrong slot

**Symptom:** `test_arm7`: `CARD_GetBackupPageSize() == 256` failed; the value was 0. Total size and
sector size were correct.

**Diagnosis:** HGSS's `CARDi_IdentifyBackupCore` fills a *private* `Cmd` struct whose fields are
shifted one slot against ntrtwl's public `CARDiCommandArg.spec`: the flash page size (256) lands in the
slot the public header calls `addr_width`, while `spec.page_size` stays 0. HGSS has no
`CARD_GetBackupPageSize`, so nothing on the DS ever read it.

**Fix:** the compat getter returns `spec.page_size` if non-zero, else `spec.addr_width`, with the
layout explanation in the comment. Works for EEPROM/FLASH/FRAM because HGSS stores the page size in
that slot for all three.

## 9. `test_arm7` segfault chain: three more shared-memory literal classes

One crash at a time, each fixed at the same layer:

| Crash | Literal | Real location | Fix |
| --- | --- | --- | --- |
| `test_sound` first | `SNDi_SharedWork` NULL when the finished-command tag is bumped | the pointer only arrives with the setup command | 1403: skip the bump while NULL |
| `test_rtc` | `0x027FFDE8/EC/EA` (raw RTC words) | `OSSystemWork.real_time_clock +0/+4/+2` | 1304 |
| `test_touch` | `0x027FFFAA` (`TP_RAW_POINT`), `0x027FFC80` (`TP_USER_SETTINGS`) | `touch_panel`, `nvramUserInfo` | 1305 |
| (`test_power`, same patch) | `0x027FFC40` | `wmBootBuf` (first u16) | 1305 |
| (MIC, same patch) | `0x027FFF90/94` | `mic_last_address`, `mic_sampling_data` | 1305 |

All five are the same root cause as §6: patches 1301/1302 moved main/shared memory into the port's
arrays, and the SDK still addressed it by literal DS address. The fixes read the same words through
`OS_GetSystemWork()`, using the shared struct's own fields, which sit at exactly those offsets
(`nitro/os/common/systemWork_shared.h` documents the offsets in comments).

**How to find the next one:** if a DS-side file contains `0x02...` or `0x05...`/`0x07...` as a
pointer, it is suspect. Grep the compiled set for literals in those ranges before assuming a logic bug.

## 10. Patch export paths and the DS-side gate pins

- **`--relative` matters.** A patch exported from a clone of `build/upstream` without
  `--relative=lib/NitroSDK` contains `lib/NitroSDK/src/...`, but bootstrap applies the NitroSDK stack
  with `--directory=lib/NitroSDK`, producing `lib/NitroSDK/lib/NitroSDK/src/...`. Symptom: `git am`
  fails with "does not exist in index". Re-export with the right `--relative`.
- **The gate needs `mwasmarm_patcher` built.** `make patch_mwasmarm` fails with
  `tools/mwasmarm_patcher/mwasmarm_patcher: No such file or directory` on a fresh clone, because the
  binary is git-ignored. Run `make -C tools/mwasmarm_patcher` first. The old docs omitted this step;
  `Daily-Workflow.md` now has it.
- **Two build tools pin header hashes.** `tools/arm7-alarm-partition/build.py` ("physical input
  drift") and `tools/alarm-partition/build.py` ("production closure drift") refuse to build when a
  header they read has changed. Eleven headers are touched by the patch stack (all inside
  `#ifdef SDK_PORT_3DS`), so `patches/pokeheartgold/1601` re-pins 26 hashes across the two
  `profile.json` files. The pin is provenance, not ROM content; the ROM gate is the proof.
  **If you add a patch that touches a pinned header, the gate will name it; update the pin.**

## 11. Concurrency: do not bootstrap while a build is running

`bootstrap.py --clean` deletes `build/upstream`. A build that reads it concurrently fails in random
files (`fatal error: nitro/hw/mmap.h: No such file or directory` was one). Symptom: a single missing
header that certainly exists. Check `git -C build/upstream rev-list --count` before believing such an
error, and serialise bootstrap against builds.

## Open: `test_arm7` `test_sound` spins

After the five fixes above, `test_arm7` runs through backup, RTC, power and touch, then **hangs in
`test_sound`** (99.9 % CPU, no output). It is a spin, not a deadlock on a lock: the process is
runnable. The likely site is `SND_WaitForCommandProc`/`SND_IsFinishedCommandTag`: the genuine ARM7
sequencer is now linked in, but the test drives the ARM9 API directly, so whatever normally pumps
`N7_SND_CommandProc` (the port audio tick) is not running for it, and the finished-command tag never
advances.

Next step: decide whether the virtual ARM7 should process the queue synchronously when a command is
pushed (`NitroPort_HandleSound` → `N7_SND_CommandProc`) or whether `port_snd.c` must run its tick from
`OS_Halt`/the test's wait. Both are port-layer changes; the vendored test must not be edited.

## Facts worth remembering

- `build/` is a volume: the Mac and the container see different trees. Always verify inside the
  container.
- A green link proves nothing about addresses. The port writes through `HW_*` arrays now; any raw
  literal is a crash waiting for the first test that touches it.
- `-include` resolves through the project's include path first. Force-include absolute paths when you
  mean the system/MSL header.
- Instrumented copies in `/tmp` are the fastest way to locate a crash under qemu-user, where gdb
  cannot attach.
