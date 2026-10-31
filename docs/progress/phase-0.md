# Phase 0 progress

Status: **complete except items marked NOT DONE below.** Both matching builds verified; Platinum sanity build passes.

## What was done (local repo `pokeheartgold-3ds`, no remote)

`git log --oneline` has the hashes; main items:
- Repo, GPL-3.0, NOTICE/CONTRIBUTING/AI_DISCLOSURE, `upstream.lock`, `platinum.lock`, `docs/BASE.md`
- `.devcontainer/` (Dockerfile, devcontainer.json, onCreate.sh, verify-devkitpro.sh), `docs/devcontainer.md`
- `tools/install-ds-toolchain.sh`
- `.github/workflows/matching.yml` (disabled) + `docs/ci.md`
- `tools/golden/` (README, `capture.lua`, `compare_runs.py`) + `tests/inputs/*.txt`
- `tools/savetool.py` + `tests/test_savetool.py` (15 tests pass)
- `docs/outreach/platinum-maintainer.md` (not sent)
- Clones in the container volume: `/work/pokeheartgold` (full history, 5,925 commits, `pret` remote added), `/work/pokeplatinum-3ds`

## Runtime findings

| Item | Result |
| --- | --- |
| OrbStack, Rosetta on | `wineboot`, MWCC and WineHQ 10.0 `makelcf.exe` run (noisy `rosetta error: invalid gdt selector` lines, harmless so far) |
| OrbStack, Rosetta off (QEMU) | `wineboot` and MWCC run; SDK tools failed with distro Wine 9.0 for the reason in `docs/decisions.md`. **WineHQ 10.0 was only verified under Rosetta**, and the full build ran on the container as left (Rosetta on). |
| HG compile time | first full `make -j10` (QEMU, Wine 9): 13m22s wall to the link step; relink after fixes ~9.5m |

## Matching build

- All C/asm compiles with MWCC under Wine; ARM7 sub binary matches its SHA1 (`sub/ichneumon_sub.sha1`).
- `main.sbin` and all 118 overlay `.sbin` checks **FAIL** (119 mismatches), so **no ROM, no SHA1 match**. `COMPARE` verification stops the build.
- Steps needed to get this far that the decomp's docs don't mention: `make patch_mwasmarm MWCCVER=2.0/sp2p3` (alarm-partition pins the patched assembler), transcoding the cp932 templates.
- Not done: SoulSilver build, `build/reference/` artifacts (a non-matching ELF would be a bad reference).

## Matching build: resolved

Both ROMs now match, verified inside the container (OrbStack, Rosetta on, WineHQ 10.0, SDK 3.2, MWCC from `mwccarm.zip` SHA256 `dc386e37…`):

| Version | SHA1 | Wall time (`-j10`) |
| --- | --- | --- |
| HeartGold US | `4fcded0e2713dc03929845de631d0932ea2b5a37` ✅ equals your retail dump (`tools/rom_compare.py`: 0 differing bytes in every region) | 2m55s relink; first full build 18m12s |
| SoulSilver US | `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` ✅ | 11m08s |

(Only Rosetta was timed for the final builds. The earlier QEMU+Wine 9 run took 13m22s to reach the link step; it is not comparable.)

**Root cause of the earlier 119 mismatches** (found by diffing against the retail ROM): the linker dead-strips five unreferenced assembly-data sections that the original keeps: `.rodata.unified_00`, `.rodata.unified_01`, `.rodata.snd_decibel` (0x100+0x600+0x100 = 0x800 of `.rodata`) and `.bss.unified_00`, `.bss.unified_04` (0x20 of `.bss`, which moves the always-loaded overlay base 0x021E5900 -> 0x021E58E0). `tools/matching-build.sh` injects `KEEP_SECTION` blocks into the generated `build/<ver>/main.lcf` (a build artifact; the decomp checkout is untouched) and rebuilds. With them the decomp's own `COMPARE=1` checks pass (0 FAILED).

**Not established:** why the decomp authors' setup keeps those sections without this. The 4.2-vs-3.2 SDK was *not* the cause (3.2 alone still failed). Needs a native macOS run of the unmodified decomp to tell whether it is Wine/Rosetta, mwldarm behavior, or a fork issue. **Rule 3 caveat:** "matching build stays byte-identical" holds, but only with this LCF step; the `matching.yml` job must call `tools/matching-build.sh` instead of plain `make`.

Other steps the decomp's docs omit (all in `tools/`): `make patch_mwasmarm MWCCVER=2.0/sp2p3` (the alarm-partition tool pins the patched assembler; `matching-build.sh` relies on the first `make` having patched sp2p2, so run it once if the pins complain), the cp932->UTF-8 transcode of the SDK-4.2 templates is no longer needed with 3.2 (script is a no-op on ASCII).

Reference artifacts (git-ignored): `build/reference/{heartgold.us,soulsilver.us}/` with `main.elf`, `main.elf.xMAP`, `main.nm`, `COMMIT`, `rom.sha1`.

## Platinum sanity build (in the container, `CC` unset)

| Step | Result |
| --- | --- |
| `bootstrap.py --clean` + `make -j10 test` (host) | PASS, 8m15s: 122 overlays, 588 checks, 0 failed; Python tests 130 OK (1 skipped) + 48 OK |
| `make -j10 game` | PASS, 7m24s; produced `pokeplatinum.3dsx` (stays local, contains game code) |
| `make -C 3ds_port/nitro TARGET=host32` (builds **and runs** the nitro tests) | PASS, exit 0, 3m10s. 32-bit binaries execute under Rosetta here, so no QEMU switch is needed. The host32 *game/nitrosystem/pak* suites were not run separately. |

## Open questions for you

1. Does the *unmodified* pinned decomp match for you natively (macOS, `scripts/setup-macos.sh`)? That tells us whether the keep-section step is only needed in the container.
2. `~/hgss-private/heartgold-retail.nds` is at the top level; the plan's layout wants `roms/pokeheartgold.us.nds`. I left it. Move it if you like; nothing in the repo hard-codes it yet. No SS ROM, no saves yet.
3. Golden captures can't be produced on this Mac (BizHawk). Inputs are uncalibrated.

## Exit checklist

| Item | Result |
| --- | --- |
| Dev container builds and starts; `onCreate.sh` passes with `/private` | PASS (Ubuntu 24.04, WineHQ 10.0, devkitARM 16.1) |
| Matching SHA1s for HG and SS from inside the container | PASS (needs `tools/matching-build.sh`; see caveat) |
| Reference artifacts archived | PASS (git-ignored `build/reference/`) |
| Capture scripts and inputs committed | PASS as files; **NOT DONE:** never run (no BizHawk on macOS), inputs uncalibrated, touch names unverified, `compare_runs.py` untested |
| savetool with tests | PASS (15 tests) |
| Platinum `make test` / `make game` pass; `host32` recorded | PASS / PASS / PASS (host32 nitro tests) |
| Outreach draft written | PASS (unsent) |
| `docs/BASE.md`, `docs/devcontainer.md` complete | PARTIAL: `devcontainer.md` still describes the earlier distro-Wine/22.04 assumptions in places; needs a pass |
| `matching.yml` | written, disabled; **must be updated to call `matching-build.sh`** |
