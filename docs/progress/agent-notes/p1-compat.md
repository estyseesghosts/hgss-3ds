# P1 compat headers (p1-compat slice) — agent notes

## Result

`hgss/compat/include/` written (20 headers), `hgss/compat/README.md` written,
`tools/port_compile_survey.py` include flags reordered to match
`tools/hgss_nitro.py`. No `patches/`, `build/upstream`, or `3ds_port/`
changes. `.gitkeep` removed (directory now has files).

| Check | Before | After |
| --- | --- | --- |
| `python3 tools/port_compile_survey.py` | 29/46 | **41/46** |
| `hgss_nitro.py make --target host32 --out /work/out/p1_compat lib -k -j8` | 44 `error:` lines, 17 objects | **27 lines, 6 objects** |
| `make test` | exit 0 (at session start, clean tree) | fails only in `gcc_fixes` (e)–(i) positive checks — sibling 1202 work in progress, independent (below) |

Full logs: `/work/out/p1_compat_baseline.log` (before),
`/work/out/p1_compat.log` (after). Survey baseline error list was the 15
distinct causes in `docs/port-wiring.md`.

## What the remaining errors are

Survey 41/46 — the 5 failures:

- 4 are patch 1202's conflicts (sibling agent, patch exists untracked but
  unapplied): `MIi_CpuClear16` (`mi_memory.c`), `favoriteColor`
  (`port_boot.c`), `DC_StoreRange` (`port_os.c`), `PXI_SetFifoRecvCallback`
  (`pxi.c`).
- 1 is new, same family, missed by W2 (masked behind the missing header):
  `CARDiCommandArg` has no member `dst` (`port_card.c`, 10 error lines in
  the build, one root cause). ntrtwl's member is `dst`; HGSS renamed it
  `dest` (same offset). **Decision needed** (no compat-only fix exists —
  a `#define dst dest` would also rewrite `cardi_common.dst`, and copying
  the shared struct risks silent layout drift).

Build 27 lines / 6 objects: the above (13 lines 1202 + 10 lines dst) plus
`mi_dma_gxcommand.c` (4 lines): that SDK file defines its own
`GX_FIFOINTR_COND_*` enum + `OS_IE_GXFIFO` locally, colliding with the
compat `nitro/gx/g3x.h` wrapper every `nitro.h` TU now sees. Needs an
SDK-side patch dropping its local copies (same kind as the 1202 set).
No compat-side fix exists (the two contexts are indistinguishable).

## Traps found while writing (see README for detail)

- `card_common.h` vs flat `CARD_common.h` differ by case only. On a
  case-insensitive mount the SDK's quoted `"CARD_common.h"` resolves here
  and once dragged the whole public chain into flat TUs (359 build
  errors). The header now branches on `NITRO_H_` (public branch only for
  `port_card.c`; flat-forward otherwise). Verified no SDK TU mixes both.
- Compat `nitro/mi/stream.h` must be chained from the uncompress wrapper:
  ntrtwl's `mi.h` includes it, HGSS's does not, and `svc.c` relies on that.
- Compat `nitro/ioreg.h` must also pull `<nitro/hw/mmap.h>`: HGSS's io_reg
  chain never defines `HW_REG_BASE` (ntrtwl's does via `mmap_global.h`).
- `nitro/cp/divider.h` adds `<stdint.h>` for `INT64_MIN` (Platinum got it
  from its patched `types.h`; HGSS's has none).
- `nitro/dgt/dgt.h` deliberately does NOT include flat `dgt.h`: the flat
  Hash2 struct is the SAFE variant (port needs ntrtwl fields) and its
  prototypes conflict with `dgt.c`. Hash1 layouts match, so `MATH_CalcMD5`
  interoperates; Hash2 callers are a runtime follow-up.
- Flat `SND_work_shared.h` / `CTRDG_common.h` / `MI_exMemory.h` cannot be
  included in `nitro.h` TUs (redefine public structs), so `main.h` keeps an
  incomplete `SNDSharedWork` and `ctrdg.h` spells out `CTRDGBackupType`.
- `PM_PAD_KEY_MASK`, `RTC_STATUS2_*`: in neither ntrtwl nor the port;
  intentionally absent.

## Cross-checks

- Reference values verified against the pinned ntrtwl tree at
  `/work/pokeplatinum-3ds/build/NitroSDK/include/` (same revision
  pokeplatinum pins), not just memory.
- `HW_ROM_HEADER_BUF_END (HW_MAIN_MEM + 0x007fff60)` confirmed in ntrtwl
  `hw/common/mmap_shared.h` and consistent with HGSS `BUF + 0x160`.
- `make test`: all pre-existing suites pass; the 5 `gcc_fixes` positive
  1202 checks fail because the sibling's patch is untracked and unapplied
  (`compat/` is not on that test's include path — verified zero `compat`
  entries — so this slice cannot affect it). Do not "fix" by touching
  their files.
- 3DS target (`--target 3ds`, devkitARM present): 92 errors, all either
  the two items above or pre-existing toolchain gaps unrelated to compat
  (`va_list` arithmetic in SDK `std_sprintf.c`, implicit math decls in
  port shadow/gx3d files). No baseline existed for this target.

## For the lead

1. Decide `CARDiCommandArg.dst`: port-side rename (3ds_port, breaks
   Platinum unless guarded) vs SDK-side alias (patches/, layout-neutral).
2. Route `mi_dma_gxcommand.c`'s local enum/`OS_IE_GXFIFO` to whoever owns
   the next SDK patch (delete locals, use compat names).
3. Suggested upstream PRs to pokeplatinum-3ds: none from this slice (all
   files are HGSS-only by design).
