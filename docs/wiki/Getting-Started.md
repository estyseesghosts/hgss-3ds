# Getting started

This page takes you from nothing to a green `make test`. Each step says what you should see when it worked. Troubleshooting for problems the repo has already recorded is at the end.

The documented setup is an Apple-silicon Mac with OrbStack. Everything builds inside an amd64 Ubuntu 24.04 container. The Linux path is not written up here yet. See the [README](../../README.md#requirements).

You need your own copies of the MWCC 2.0 archive and the Nitro SDK 3.2-060901 archive. You do not need a ROM for the steps below. Nothing in this repo or its container contains proprietary files. They come only from your private folder, mounted read-only.

Steps:

1. Install OrbStack
2. Install the Dev Containers CLI
3. Create `~/hgss-private`
4. Bring up the container
5. Clone the decomp into `/work`
6. Install the DS toolchain
7. Run the matching build
8. Run bootstrap
9. Run `make test`

Commands run on the Mac in your checkout unless they say `devcontainer exec`. Those run inside the container. The repo is at `/workspaces/pokeheartgold-3ds` in the container.

## 1. Install OrbStack

```
brew install orbstack
```

Open OrbStack once so its Docker engine starts. Keep Rosetta enabled. The Wine and SDK tools were verified only under Rosetta. See [Troubleshooting](#rosetta-and-qemu).

Docker Desktop is documented as an alternative in [docs/devcontainer.md](../devcontainer.md), but this repo has not been tested with it.

**You should see:** OrbStack running, with its engine started.

## 2. Install the Dev Containers CLI

```
npm i -g @devcontainers/cli
devcontainer --version
```

**You should see:** a version number. The last run printed `0.90.0`.

## 3. Create `~/hgss-private`

The container mounts this folder at `/private`, read-only. Keep it outside the repo. The layout comes from [docs/devcontainer.md](../devcontainer.md) and what `.devcontainer/onCreate.sh` checks:

```
~/hgss-private/
  mwccarm/                        license.dat, 1.2/, 2.0/{sp2p2,sp2p3,...}/
  NitroSDK/                       the extracted NitroSDK 3.2-060901 archive
    tools/bin/makelcf.exe
    include/nitro/specfiles/ARM7-TS.lcf.template
    include/nitro/specfiles/ARM9-TS.lcf.template
    include/nitro/specfiles/mwldarm.response.template
  roms/                           optional: pokeheartgold.us.nds, pokesoulsilver.us.nds
  saves/                          optional: *.sav
```

Extract the MWCC archive so that `mwccarm/2.0/sp2p2/mwccarm.exe`, `mwccarm/2.0/sp2p3/mwccarm.exe` and `mwccarm/license.dat` exist. The decomp's [INSTALL.md](https://github.com/ctrl-cheeb-del/pokeheartgold/blob/master/INSTALL.md) asks for the same MWCC files under its `tools/` folder.

Extract the Nitro SDK archive so that the tree above exists. The archive is a 7z file. Use any 7-Zip-compatible tool on the Mac. Extract on the Mac, not in the container: `/private` is mounted read-only there, so nothing can be extracted into it from inside.

The verified matching build used the 3.2-060901 archive ([docs/progress/phase-0.md](../progress/phase-0.md)). [docs/decisions.md](../decisions.md) records an earlier attempt with a 4.2-071210-jp archive. Use 3.2-060901.

Check the required files on the Mac:

```
ls ~/hgss-private/mwccarm/2.0/sp2p2/mwccarm.exe ~/hgss-private/mwccarm/2.0/sp2p3/mwccarm.exe ~/hgss-private/mwccarm/license.dat
ls ~/hgss-private/NitroSDK/tools/bin/makelcf.exe ~/hgss-private/NitroSDK/include/nitro/specfiles/ARM7-TS.lcf.template
ls ~/hgss-private/NitroSDK/include/nitro/specfiles/ARM9-TS.lcf.template ~/hgss-private/NitroSDK/include/nitro/specfiles/mwldarm.response.template
```

**You should see:** every path listed with no error.

The ROMs and saves are optional. Nothing in the repo reads `roms/` or `saves/` yet. The builder that would read them is Phase 3 and does not exist.

## 4. Bring up the container

From your checkout:

```
devcontainer up --workspace-folder .
```

The first run builds the image. That step downloads Ubuntu packages, WineHQ 10.0 and devkitPro's `3ds-dev` group, so it takes a while. When the container is created, `.devcontainer/onCreate.sh` runs once. It checks the private folder and fails with the list of anything missing, and it never downloads proprietary files. It then starts Wine once under a virtual X server.

Open a shell when you need one:

```
devcontainer exec --workspace-folder . bash
```

**You should see:** `devcontainer up` print a JSON line with `"outcome":"success"`. Then check the toolchain:

```
devcontainer exec --workspace-folder . bash -c 'wine --version; arm-none-eabi-gcc --version | head -1'
```

It should print `wine-10.0` and `arm-none-eabi-gcc (devkitARM) 16.1.0`.

If `onCreate.sh` reports missing files, fix `~/hgss-private`, then recreate the container so that `onCreate.sh` runs again. See [Troubleshooting](#private-files-missing).

## 5. Clone the decomp into `/work`

The DS toolchain step and the matching build use a full decomp checkout at `/work/pokeheartgold`. This is a separate copy from `build/upstream/`, which `bootstrap.py` fetches later. `/work` is a named Docker volume, so the clone survives container restarts.

```
devcontainer exec --workspace-folder . bash -c 'git clone https://github.com/ctrl-cheeb-del/pokeheartgold /work/pokeheartgold && git -C /work/pokeheartgold checkout "$(sed -n "s/^commit=//p" /workspaces/pokeheartgold-3ds/upstream.lock)"'
```

The clone uses full history, as Phase 0 did. The checkout puts it on the commit in `upstream.lock`. The matching build depends on that commit.

**You should see:** git print a detached-HEAD notice. Then check it:

```
devcontainer exec --workspace-folder . bash -c 'git -C /work/pokeheartgold rev-parse HEAD; sed -n "s/^commit=//p" /workspaces/pokeheartgold-3ds/upstream.lock'
```

The two lines should match.

## 6. Install the DS toolchain

```
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && tools/install-ds-toolchain.sh /work/pokeheartgold'
```

The script copies MWCC and the SDK files from `/private` into the decomp checkout, at paths git ignores. It converts the two LCF templates to UTF-8 if they are not already UTF-8. It then runs `mwccarm.exe` once under Wine.

**You should see:** these lines, in this order:

```
installed tools/mwccarm
installed tools/bin
installed sub/ARM7-TS.lcf.template
installed ARM9-TS.lcf.template
installed mwldarm.response.template
mwccarm under wine: ...
```

A `transcoded cp932 -> utf-8` line appears only if a template was not UTF-8. The last line may show compiler output or `<no output>`. The script refuses to copy into any path git does not ignore in the decomp, and it refuses if `/work/pokeheartgold` is not a git checkout.

## 7. Run the matching build

This is the check that the DS side still reproduces the retail bytes. It is slow the first time. Phase 0 measured about 18 minutes for the first HeartGold build and about 3 minutes for a relink, on Apple silicon under Rosetta with `-j10`. You can skip this step for `make test`, because `make test` does not use `/work`. Run it once before you rely on the DS build. Changes to SDK or decomp patches are checked with the gate in [Daily-Workflow](Daily-Workflow.md#the-ds-side-gate).

Two commands. The first is the decomp's own patch step. [docs/progress/phase-0.md](../progress/phase-0.md) says to run it once if the pins complain. Running it first avoids that:

```
devcontainer exec --workspace-folder . bash -c 'cd /work/pokeheartgold && make patch_mwasmarm MWCCVER=2.0/sp2p3'
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && tools/matching-build.sh /work/pokeheartgold HEARTGOLD'
```

`tools/matching-build.sh` is the build to use, not a plain `make`. It injects five `KEEP_SECTION` blocks into the generated linker script. Without them the ROM does not match. The root cause of that is not established ([docs/progress/phase-0.md](../progress/phase-0.md)).

Then check the SHA1 of the result:

```
devcontainer exec --workspace-folder . bash -c 'cd /work/pokeheartgold && echo "4fcded0e2713dc03929845de631d0932ea2b5a37  build/heartgold.us/pokeheartgold.us.nds" | sha1sum -c -'
```

**You should see:** the script print `injected 5 KEEP_SECTION blocks` (or `keep blocks already present` on a rerun), then the build finish. The check should print `build/heartgold.us/pokeheartgold.us.nds: OK`.

For SoulSilver, run the same script with `SOULSILVER` and check `f8dc38ea20c17541a43b58c5e6d18c1732c7e582  build/soulsilver.us/pokesoulsilver.us.nds`.

The ROM this produces contains game code. Keep it under `/work`. Never copy it into the repo or share it.

If the first `make patch_mwasmarm` step fails, see [Troubleshooting](#the-mwasmarm-step).

## 8. Run bootstrap

```
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && python3 tools/bootstrap.py'
```

This fetches the decomp at the commit in `upstream.lock` into `build/upstream/`, applies the **78** patch files from `patches/` as commits, and links `build/NitroSDK` and `build/NitroSystem` to the SDK sources. The fetch needs network access to GitHub.

**You should see:** a `Fetching https://github.com/ctrl-cheeb-del/pokeheartgold.git @ 8cbb4a19ab78 ...` line, seventy-eight `  applying ...` lines, and `Done: build/upstream (78 patches applied), build/NitroSDK, build/NitroSystem`.

Running it again prints `upstream: already at 8cbb4a19ab78 with 78 patches`.

## 9. Run `make test`

```
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && make test'
```

This runs the 330 Python tests and then the twenty SDK host suites. It is the same command CI runs on the synthetic-data image.

**You should see:**

- `Ran 330 tests`, then `OK`.
- One `== tests/sdk/<name>` header for each of the twenty suites (including `arm7_sound`, `div_sqrt_writers`, `fx`, `gcc_fixes`, `irq_handler`, `math_qsort`, `memory_map`, `nns_clz`, `os_system`, `port_hooks`, `small`).
- The `fx` suite's checks, and `sdk small: all ok` at the end.
- Exit status 0.

You are green. Next, see [Daily-Workflow](Daily-Workflow.md) for the loop, and the layout probe with `make layout` (it needs the decomp checkout from step 5 to have been built once, so run it after step 7).

## 10. Build the port layer

The vendored hardware layer (`3ds_port/`) is wired to HGSS and green:

```
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && python3 tools/port_compile_survey.py'
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && python3 tools/hgss_nitro.py make --target host32 --out /work/out/first lib -k -j8'
```

**You should see:** `46/46 port source files compile against the HGSS headers`, exit 0. The `make` run then builds `libnitro.a` and `libnitrosystem.a` with no errors. `make port-tests` runs the vendored suites: **all of them pass** (at 78 patches; nitrosystem 17,122 checks, gx3d 337+239). The next step is the game build, `make game-pc` / `make game-3ds`, and the DS gate in [../progress/STATE.md](../progress/STATE.md) and [../port-wiring.md](../port-wiring.md).

`hgss_nitro.py` explains the shim it builds in [Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired). The next work and the full error map are in [../progress/STATE.md](../progress/STATE.md) and [../port-wiring.md](../port-wiring.md).

## Troubleshooting

Each entry lists what happened, why, and the fix. Every entry comes from something the repo records.

### Private files missing

`onCreate.sh` prints `ERROR: required private files are missing:` and a list. The cause is that `~/hgss-private` is not laid out as in step 3, or the mount path in `.devcontainer/devcontainer.json` points elsewhere. Fix the folder or the mount line, then recreate the container so the check runs again. The script never downloads files.

### Wine fails at container setup

`onCreate.sh` prints `wineboot failed; wine is not usable`. The image pins WineHQ 10.0 on purpose. WineHQ 11.0 aborts in `ntdll/unix/virtual.c` (`anon_mmap_fixed`) under emulation. Ubuntu's distro Wine 9.0 runs MWCC but cannot load the Nitro SDK tools. See [docs/decisions.md](../decisions.md). Do not change the Wine version in `.devcontainer/Dockerfile`.

The decomp's own [INSTALL.md](https://github.com/ctrl-cheeb-del/pokeheartgold/blob/master/INSTALL.md) recommends Wine 11.16 from its setup helper. This repo does not use that helper, and the pin is 10.0.

### SDK tools will not load (`c0000018`)

Nitro SDK tools such as `makelcf.exe` fail with `failed to create main module, c0000018` when run under Ubuntu's distro Wine 9.0. The fix is the pinned WineHQ 10.0 in the image. Rebuild the image if you changed it.

### Download of devkitPro packages returns 403

`apt.devkitpro.org` returns a Cloudflare 403 to the default User-Agent. The Dockerfile fetches the key with `wget -U "dkp apt"` for that reason. If you edited that line, restore it.

### `GLIBC_2.36 not found` from devkitARM

devkitARM's binaries need glibc 2.36. Ubuntu 22.04 has 2.35. The image is therefore Ubuntu 24.04, and the `COPY --from=devkitpro/devkitarm` route does not work. See [docs/decisions.md](../decisions.md).

### The mwasmarm step

The decomp's alarm partition pins the patched assembler. Phase 0 found that the matching build relies on `make patch_mwasmarm MWCCVER=2.0/sp2p3` having run. The decomp's own `all` target runs `patch_mwasmarm` before the ROM, so a plain `make` does it too. If the pins complain, run that command in `/work/pokeheartgold` once. This comes from [docs/progress/phase-0.md](../progress/phase-0.md).

### The matching SHA1 does not match

A plain `make` in `/work/pokeheartgold` produced 119 mismatches in Phase 0. Those were caused by five unreferenced sections that the linker removed. `tools/matching-build.sh` puts them back, and the SHA1 then matches. Use the script, not plain `make`. The matching workflow also has to call the script: `.github/workflows/matching.yml` does, but [docs/ci.md](../ci.md) still lists plain `make`.

### `install-ds-toolchain.sh` refuses to copy

The script says `... is not git-ignored in ...; refusing to copy`. The decomp checkout must ignore the destination paths. Check that `/work/pokeheartgold` is the pinned decomp clone from step 5. Do not force the copy.

### `rosetta error: invalid gdt selector`

These lines appear while running Wine and the SDK tools under Rosetta. They are harmless. Phase 0 and [docs/devcontainer.md](../devcontainer.md) both record that.

### Rosetta and QEMU

Rosetta works for the pinned WineHQ 10.0, the matching builds and the 32-bit host tests. QEMU (`orbctl config set rosetta false`) was tried only with the failing Wine versions. Keep Rosetta on. Details are in [docs/devcontainer.md](../devcontainer.md).

### `bootstrap.py` says the checkout has the wrong number of patches

It prints `build/upstream has N patches applied but patches/ has M`, or `has local changes`, or `does not descend from ...`. Run `python3 tools/bootstrap.py --clean`. That deletes `build/upstream/` and fetches it again.

### `make test` says `Run first: python3 tools/bootstrap.py`

Step 8 has not run in this container, or `build/upstream` was removed. Run bootstrap, then `make test` again.

### A patch does not apply

Bootstrap prints `<name>.patch does not apply cleanly`, with the `git am` output, and stops. The patch was made against a different base. Fix the patch, then run `--clean`.

## Links

- [Home](Home.md): the wiki index.
- [Build-Reference](Build-Reference.md): toolchains, every build target, flags and cleanup.
- [Daily-Workflow](Daily-Workflow.md): the loop once you are set up.
- [Tools-Reference](Tools-Reference.md): every script and test runner.
- [README](../../README.md): what the project is and its current status.
- [docs/progress/STATE.md](../progress/STATE.md): what is green today.
