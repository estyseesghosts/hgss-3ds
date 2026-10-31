# Phase 0 progress

Status: **incomplete.** Blocked on the matching build (see "Open questions").

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

## Open questions for you

1. **The decomp's setup script expects `NitroSDK-3_2-060901.7z` (SHA256 `0ee935e3…`). I only have `4_2-071210-jp`.** Evidence it is the wrong SDK: its `makelcf`/template needed transcoding, and every link output mismatches. I can't confirm the cause without the 3.2 archive. Can you put it in `~/hgss-private/` (or tell me where it is)?
2. Should I keep Rosetta on (current) or QEMU?

## Exit checklist

| Item | Result |
| --- | --- |
| Dev container builds and starts; `onCreate.sh` passes with `/private` | PASS (Ubuntu 24.04, WineHQ 10.0, devkitARM 16.1) |
| Matching SHA1s for HG and SS | **FAIL** (HG compiles+links, 119 checks mismatch; SS not run) |
| Reference artifacts archived | **FAIL** (not meaningful until matching) |
| Capture scripts and inputs committed | PASS, but inputs are uncalibrated and the BizHawk touch names are unverified; `compare_runs.py` untested (Pillow only in container) |
| savetool with tests | PASS |
| Platinum `make test` and `make game` | **NOT YET**: `make test` running in the container (`/work/plat-test.log`); `make game` and `host32` not attempted |
| Outreach draft | PASS (unsent) |
| `docs/BASE.md`, `docs/devcontainer.md` complete | PASS, except `devcontainer.md` still says distro-Wine details are in progress; QEMU-vs-Rosetta table above is the source of truth |
