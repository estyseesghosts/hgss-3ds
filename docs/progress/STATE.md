# Current state

Last verified: **2026-10-31**, inside the dev container (`/workspaces/pokeheartgold-3ds`), with the code at
commit `0abf623`. Everything below that says "verified" was run on that tree on that date. If you change the
patch stack, rerun the commands and update this page; do not trust it blindly.

**One line:** Phase 0 is done (with open items); Phase 1 is in progress and the next unfinished exit
criterion is the vendored port's unit tests. No game code is compiled yet. `make test` is green.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **In progress** | 15 patches apply from scratch; SDK survey 270/284; layout probe 0/804 differences; 11 host suites green; DS gate last passed with 10 of the patches. Port unit tests are not built yet. [phase-1.md](phase-1.md) |
| 2 Game code under GCC | **Not started** | No game object has been compiled or linked. |
| 3 Data pipeline | **Not started** | No `builder/`. |
| 4-11 | **Not started** | See [../wiki/Architecture.md](../wiki/Architecture.md). |

## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | `Done: build/upstream (15 patches applied)` |
| Host tests | `make test` | **exit 0**; 19 Python tests; 11 SDK suites; last suite `sdk small: all ok` |
| SDK parses (gcc 13, default) | `make sdk-survey` | **270/284** parse, 14 fail (all port-replaced) |
| SDK parses (gcc 10, the port's compiler) | `python3 tools/sdk_compile_survey.py --m32 --gcc gcc-10` | **269/284**; the extra failure is `ctrdg/CTRDG_common.c` |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | **29/46** compile; 17 fail for 15 distinct causes ([../port-wiring.md](../port-wiring.md)) |
| Vendored port compiles (host32) | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/state lib -k -j8` | **fails**: 44 `error:` lines in 9 objects (below) |
| Save layout | `make layout` | 0 differences in 804 values; needs `/work/pokeheartgold` built once. Not rerun today (no rebuild); last run in Phase 1. |
| DS-side gate | see [wiki/Daily-Workflow.md](../wiki/Daily-Workflow.md#the-ds-side-gate) | **not rerun** with the 5 newest patches. Last pass: 10 patches, HG SHA1 `4fcded0e…`, 18m43s. |

### The 11 host suites that pass

`card_spi_wait` 19 checks · `cp_context` (two programs) · `fx` 428 cases · `gcc_fixes` 4 groups ·
`irq_handler` 10 checks · `math_qsort` 11,202 checks · `memory_map` 35 checks · `nns_clz` ·
`os_system` 29 checks · `port_hooks` 10+60+10 checks · `small`.

### What still fails in the port build (44 errors, 9 objects)

Grouped, not file by file:

| Cause | Objects |
| --- | --- |
| Missing compat headers: `nitro/spi/common/type.h`, `nitro/rtc/common/fifo.h`, `nitro/snd/common/command.h`, `nitro/dgt/dgt.h`, `nitro/ioreg.h`, `nitro/hw/ARM9/mmap_global.h`, `card_common.h` | port_pm, port_rtc, port_snd, dgt, port_cp, port_ioreg, port_card |
| `MIUnpackBitsParam`, `MIReadStreamCallbacks`, `MIi_Cpu*` prototypes differ | svc, mi_uncompress, mi_memory |
| `MI_DMA_TIMING_{IMM,GXFIFO,DISP,DISP_MMEM,V_BLANK}`, `OS_IE_{FIFO_RECV,GXFIFO}`, `GX_FIFOINTR_COND_*` undeclared | port_dma, port_gx, port_os |
| `CTRDGBackupType`, `CTRDG_TASK_FUNC`, `PXIFifoEmtpyCallback`, PXI prototype conflicts | ctrdg, pxi |
| `HW_ROM_HEADER_BUF_END`; NVRAM `favoriteColor` vs HGSS `favouriteColour`, `NVRAM_CONFIG_*` | port_boot |

`hgss/compat/include/` exists but contains only `.gitkeep`: the compat headers from the W2 slice were
never written to the repo. That is the next concrete piece of work.

## Next actions, in order

1. **Compat headers (W2).** Write `hgss/compat/include/{spi/common/type.h, rtc/common/{fifo,type}.h,
   snd/common/{command,main}.h, dgt/dgt.h, ioreg.h, cp/{divider,sqrt}.h, card_common.h, ...}`. Each is
   either a thin include of the HGSS header or the missing names. `tools/hgss_nitro.py` already puts
   the directory on the include path. Measure with `port_compile_survey.py` and the `host32` build.
2. **HGSS PXI decoders.** RTC/touch/power requests from HGSS are literal command words; the port's
   virtual ARM7 decodes Platinum's named constants. New files (e.g. `port_pm_hgss.c`), not edits to
   the vendored ones. Payload formats are still uncompared.
3. **`MIi_Cpu*` signatures and the missing `MI_Copy*B`.** Decide where they live (port `mi_memory.c`
   vs a compat header); do not weaken the signature checks.
4. **Vendored port tests.** Wire `3ds_port/nitro` (then `nitrosystem`, `pak`) to the HGSS source list
   and make `make port-tests` real. This is the Phase 1 exit criterion that is still open.
5. **DS-side gate with all 15 patches**, then update [phase-1.md](phase-1.md) and this page.
6. Then the rest of Phase 1: `docs/sdk-diff.md` completion, timers/overlay/3D/ARM7-sound patch
   equivalents, `port.yml` on GitHub, final report.

## Open decisions

- Continue Phase 1 to green port tests, or close it with that criterion explicitly deferred?
- `g2d_CellTransferManager.h:24` is fixed by patch 1101. Keep the patch (chosen) or pin devkitARM?
- Send port-side fixes to the Platinum maintainer as upstream PRs, or keep them HGSS-only?
- The survey defaults to gcc 13 but the port builds with gcc 10. W3 suggests defaulting the survey to
  gcc 10; today both numbers are recorded instead.
- Commit trailer: the repo standard is `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`
  (see phase-1.md); agent notes written earlier sometimes say Haiku 5.5.

## Resume in five minutes

```bash
cd <repo>
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
# inside the container:
cd /workspaces/pokeheartgold-3ds
python3 tools/bootstrap.py            # "already at ... with 15 patches" if in sync
make test                            # exit 0
python3 tools/port_compile_survey.py # 29/46 today
python3 tools/hgss_nitro.py make --target host32 --out /work/out/try lib -k -j8
```

If `bootstrap.py` reports a patch-count mismatch, run `--clean`. If `build/` looks empty on the Mac,
that is expected: it is a volume (`hgss-build`) inside the container.

## Environment facts (measured in this container)

| Tool | Version | Used for |
| --- | --- | --- |
| Ubuntu | 24.04, amd64 | the container |
| Wine | WineHQ 10.0 | MWCC, `makelcf.exe`; pinned, do not bump |
| MWCC | 2.0/sp2p2 (patched sp2p3 for the assembler) | the DS-side matching build only |
| devkitARM | 16.1.0 | layout probe ARM variant; future 3DS builds |
| host gcc | 13.3.0 (`gcc`) | surveys by default, most SDK suites |
| host gcc-10 | 10.5.0 (`gcc-10`), also `CC` in the image | the port's `host32` build (its make log shows gcc-10) |
| Python | 3.12.3 | all tools; `pyelftools`, `unicorn` when noted |
| Make | 4.3 | |
| devkitPro tools | `3dsxtool`, `smdhtool` present | Phase 5, not used yet |

The full toolchain/build reference is [wiki/Build-Reference.md](../wiki/Build-Reference.md).

## Log

Newest first. Each entry: what changed, what was run, what it proved.

### 2026-10-31 (later session) — W3 landed; 1302; host suites back on the port arrays; docs

- Landed the interrupted W3 slice as commit `7631efa`: patch **1201** (GCC-10 fixes: unnamed
  `SND_command.c` parameter, duplicate CRC header definitions, `OS_IrqHandler(void)`,
  `PXI_FIFO_TAG_CTRDG_PHI`), `tests/sdk/gcc_fixes`, the `irq_handler` call-syntax update, the W3
  agent note. Trailer normalised to Sonnet 5.5. Verified `git am` on a fresh clone; both suites pass.
- Rebootstrapped from scratch: **15 patches**. `make test` then failed: patch 1301 moved the
  registers into `NitroPort_IoReg`, and six suites still linked against the old fixed addresses.
- Added `tests/sdk/port_memory.c` (the eleven arrays), linked it into the suites, made
  `card_spi_wait` guard the whole I/O array with `mprotect`, and pointed `small` at the arrays.
  No assertion changed.
- Found a real gap in 1301: `REG_ADDR_DMA0CNT` and `REG_ADDR_DMA0_CLR_DATA` are absolute literals in
  `nitro/mi/dma.h` and the flat `MI_dma.h`, so they did not follow `HW_REG_BASE`; the DMA hook test
  segfaulted. Patch **1302** derives both from `HW_REG_BASE` under `SDK_PORT_3DS`; `memory_map`
  gains three checks (35 total). Commit `0abf623`.
- Measurements: `make test` exit 0 (11 suites + 19 Python tests); SDK survey 270/284 (gcc 13) and
  269/284 (gcc 10); port survey 29/46 (was 26); `host32` port build 44 errors in 9 objects (was 62
  in 10 before 1201/1301/1302).
- Not done: DS-side gate with the new patches; compat headers; port unit tests. Those are the next
  actions above.

### 2026-10-30 — Phase 0 closed, Phase 1 mostly built

- Phase 0: container, matching HG/SS builds, reference artifacts, savetool, Platinum sanity build.
  Open: golden captures, saves, determinism, native-macOS control. [phase-0.md](phase-0.md)
- Phase 1: patches 0101-0901, NNS 0301, 1001, 1101; survey 270/284; layout probe 0 differences;
  DS gate passed with 10 patches; 9 SDK suites green. Port wiring started: `hgss_nitro.py` shim,
  `port_compile_survey.py`, 1301 memory map. [phase-1.md](phase-1.md)
- W2 (compat headers) designed and measured (94 errors → 62 with the early fixes) but its headers
  were never committed. W3 (patch 1201) was authored and left uncommitted; this session finished it.
