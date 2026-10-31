# Phase 0 progress

Status: **incomplete.** Blocked on the matching build (see "Open questions"). Updated after the correct SDK arrived.

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

## Update: correct SDK installed, still not matching

`~/hgss-private/NitroSDK-3_2-20060901.7z` (SHA256 `0ee935e3…`, equals the hash in the decomp's `scripts/setup-macos.sh`) is now extracted to `~/hgss-private/NitroSDK`; the 4.2 copy is `NitroSDK-4_2`. After `make clean` and a full rebuild (18m12s wall, 99m CPU, Rosetta, WineHQ 10.0, `-j10`) **all 119 SHA1 checks still fail** (main + 118 overlays), so the 4.2 SDK was not the (only) cause. The ARM7 sub binary matches.

Reasoning so far: one wrong object in `main` shifts addresses that every overlay references, so "all fail" does not imply a systemic fault. Not yet distinguished: (a) a source regression at the pinned commit, (b) an emulation effect on MWCC (x87 float constant folding under Rosetta/QEMU), (c) a missing step. No reference binary exists to diff against.

## Open questions for you

1. Please put `pokeheartgold.us.nds` (SHA1 `4fcded0e…`) in `~/hgss-private/roms/`. Extracting its ARM9 binary lets me find the first differing byte, map it to a function and see whether it is a codegen difference (emulation) or a code difference (source). Nothing else can localize this.
2. If you know the fork's HEAD matched for you natively (macOS Wine 11.16 via `scripts/setup-macos.sh`), say so: that points at (b).

## Platinum sanity build (in the container)

`python3 tools/bootstrap.py --clean && make -j10 test` with `CC` unset: **passes**, 8m15s. Overlay check: 122 overlays, 588 checks, 0 failed; Python tests 130 OK (1 skipped) and 48 OK. `make game` and `TARGET=host32` not yet run.

## Exit checklist

| Item | Result |
| --- | --- |
| Dev container builds and starts; `onCreate.sh` passes with `/private` | PASS (Ubuntu 24.04, WineHQ 10.0, devkitARM 16.1) |
| Matching SHA1s for HG and SS | **FAIL** (HG compiles+links with the verified 3.2 SDK, 119 checks mismatch; SS not run) |
| Reference artifacts archived | **FAIL** (not meaningful until matching) |
| Capture scripts and inputs committed | PASS, but inputs are uncalibrated and the BizHawk touch names are unverified; `compare_runs.py` untested (Pillow only in container) |
| savetool with tests | PASS |
| Platinum `make test` and `make game` | `make test` PASS (container); `make game` and `host32` not run yet |
| Outreach draft | PASS (unsent) |
| `docs/BASE.md`, `docs/devcontainer.md` complete | PASS, except `devcontainer.md` still says distro-Wine details are in progress; QEMU-vs-Rosetta table above is the source of truth |
