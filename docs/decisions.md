# Decisions log

Choices made during implementation that `gameplan.md` did not specify. The
plan's own decisions table is authoritative for everything it covers.

| Date | Decision | Why |
| --- | --- | --- |
| 2026-10-30 | Private folder is `~/hgss-private` (`mwccarm/`, `NitroSDK/`, `roms/`, `saves/`), created by moving/extracting the archives the user left in the project root. | User instruction. |
| 2026-10-30 | The supplied Nitro SDK is `NitroSDK-4_2-071210-jp` (the same archive pmd-sky uses), not the `NitroSDK-3_2-060901` named in the decomp's `scripts/setup-macos.sh`. We use it and let the matching build be the test. | Only archive available; the decomp's `INSTALL.md` says "a compatible Nitro SDK". Risk tracked in `docs/progress/phase-0.md`. |
| 2026-10-30 | `install-ds-toolchain.sh` copies the whole `mwccarm/` tree plus `license.dat`, not only `2.0/sp2p2`. | `common.mk` also uses `2.0/sp2p3` (lib/NitroSDK, lib/MSL_C), `1.2/sp2p3`, `2.0/base`, `sp1`, `sp1p5`, `sp2`; license is found via `LM_LICENSE_FILE=tools/mwccarm/license.dat`. |
| 2026-10-30 | LCF/response templates are copied from the SDK's `include/nitro/specfiles/`, not from a "templates" folder in `/private`. | That is where the SDK (and the decomp's setup script) has them. |
| 2026-10-30 | Container runtime is OrbStack (Apple silicon, amd64 via Rosetta); Dev Containers CLI from npm. | User approved installing OrbStack with brew. |
| 2026-10-30 | `savetool` discovers slot sizes from footers instead of hard-coding them (`--layout` overrides). | Slot sizes depend on compiled struct sizes (`gSaveChunkHeaders` size funcs); hard-coding would be a guess. |
| 2026-10-30 | Container base is Ubuntu **24.04**, not the decomp's 22.04. | devkitARM (devkitPro pacman, r68) is linked against glibc 2.36; on 22.04 `arm-none-eabi-gcc` fails with `GLIBC_2.36 not found`. The `COPY --from=devkitpro/devkitarm` route has the same problem (Debian bookworm binaries). |
| 2026-10-30 | devkitPro installed with its apt repo + `dkp-pacman -S 3ds-dev`, key fetched with `wget -U "dkp apt"`. | `apt.devkitpro.org` returns a Cloudflare 403 to other User-Agents; the official installer script uses `dkp apt`. |
| 2026-10-30 | Wine is **WineHQ stable 10.0**, pinned. | 11.0 aborts in `ntdll/unix/virtual.c` (`anon_mmap_fixed`) under emulation; distro Wine 9.0 runs MWCC but cannot load the Nitro SDK tools (fixed base 0x400000, no relocs: `status c0000018`). |
| 2026-10-30 | `install-ds-toolchain.sh` transcodes the two LCF templates from cp932 to UTF-8 (installed copies only). | SDK 4.2-jp templates have Shift-JIS comments; the decomp's Python scripts read UTF-8. |
| 2026-10-30 | `CC` is unset when building the Platinum port (it honours `CC ?= cc`); the container's `CC=gcc-10` makes it fail on unnamed parameters. | Platinum CI uses the distro default gcc. |
