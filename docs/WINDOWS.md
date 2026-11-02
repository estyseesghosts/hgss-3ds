# Windows: a hypothetical setup

> **Status: hypothetical, not tested.** This page describes how a Windows
> user *would* build and play pokeheartgold-3ds today. Nobody has run these
> exact steps on Windows yet, and `scripts/setup_toolchain.py` deliberately
> refuses to run there. The supported player instructions are
> [USER-GUIDE.md](USER-GUIDE.md) (Linux and macOS). Treat this page as a plan
> to try and report back on, not as a promise.

## The short answer: use WSL2

The build is a normal Linux toolchain (devkitARM, `make`, a Python harness).
The least surprising way to get it on Windows is **WSL2** (Windows Subsystem
for Linux), which runs a real Ubuntu beside Windows. The three player scripts
then run unchanged, and only the final copy to the SD card crosses back to
Windows.

Native Windows (MSYS2 + devkitPro's Windows packages) is *possible* in
principle — devkitPro ships Windows builds — but this project has not tested
it, the scripts' tool discovery assumes `/opt/devkitpro`, and the build uses
`make` and POSIX shell snippets. Do not start there.

## What you need

- Windows 10 (version 2004 or newer) or Windows 11, 64-bit
- virtualization enabled in the BIOS/UEFI (WSL2 needs it)
- at least 8 GB of RAM (the compiler runs several jobs at once)
- a New 3DS/New 2DS with the Homebrew Launcher
- your own US HeartGold or SoulSilver `.nds` dump
- an SD card reader

## 1. Install WSL2 and Ubuntu

Open **PowerShell as Administrator**:

```powershell
wsl --install -d Ubuntu-24.04
```

Reboot when Windows asks. On first start, Ubuntu asks for a Linux username
and password; they are separate from your Windows account. Check it worked:

```powershell
wsl --status
wsl --list --verbose
```

`VERSION` should be `2`. If WSL is already installed but old, run
`wsl --update`. If `wsl --install` reports that virtualization is disabled,
enable Intel VT-x/AMD-V in the firmware settings.

From now on, run the commands below **inside Ubuntu** (open “Ubuntu” from the
Start menu, or type `wsl` in PowerShell). The prompt looks like
`you@PC:~$`.

## 2. Work inside the Linux filesystem

Do not build in `/mnt/c/...`. WSL is dramatically slower there, and Windows
permissions confuse git and the compiler. Keep the project in your Linux home
folder:

```sh
cd ~
git clone https://github.com/estyseesghosts/hgss-3ds.git
cd hgss-3ds
```

If you downloaded the project as a ZIP in Windows, copy it in once:

```sh
cp -r /mnt/c/Users/<YourName>/Downloads/hgss-3ds-main ~/hgss-3ds
```

## 3. Install the toolchain

```sh
cd ~/hgss-3ds
python3 scripts/setup_toolchain.py
```

Ubuntu takes the Debian path: the script adds devkitPro's apt repository,
installs `devkitpro-pacman`, and then `3ds-dev`, `general-tools` and
`picasso` (the compiler, libctru, citro3d, `3dsxtool`, `bin2s` and the
shader assembler). It asks for your **Linux** password for `sudo`.

Check it without changing anything:

```sh
python3 scripts/setup_toolchain.py --check
```

A fresh WSL Ubuntu normally has Python 3, `git` and `make` already, or the
script installs them with `apt`.

## 4. Bring your ROM into WSL

The ROM stays yours and stays local. Copy it from the Windows drive, quoting
paths that contain spaces:

```sh
cp "/mnt/c/Users/<YourName>/Downloads/pokemon-heartgold.nds" ~/game.nds
```

Use the file's real name and path. The `C:` drive appears as `/mnt/c`,
`D:` as `/mnt/d`, and so on.

## 5. Prepare, build and stage

Exactly as in the [player's guide](USER-GUIDE.md):

```sh
python3 scripts/prepare_game.py ~/game.nds
python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak
```

The staging folder is `~/hgss-3ds/dist/sd-card/`. The first build fetches the
decompilation and takes roughly ten minutes on a typical laptop.

## 6. Copy to the SD card

Plug the card into Windows. It appears as a drive letter, for example `E:`.
WSL sees that drive at `/mnt/e`. The simplest path is to stage directly onto
the card:

```sh
python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak --out /mnt/e
```

This merges into `E:\3ds\` and leaves everything else on the card alone. The
card then has:

```
E:\3ds\pokeheartgold.3dsx
E:\3ds\pokeheartgold\heartgold3ds.pak
E:\3ds\pokeheartgold\manifest.json
```

If you prefer Explorer, the staging folder is reachable from Windows at
`\\wsl$\Ubuntu\home\<your-linux-name>\hgss-3ds\dist\sd-card\`; drag its `3ds`
folder onto the card and choose **Merge**.

Eject the card properly, put it in the 3DS, and start **pokeheartgold** from
the Homebrew Launcher. The first start writes `heartgold.sav` and `log.txt`
into `E:\3ds\pokeheartgold\`.

## Sound, saves and updates

- **Sound** needs your own console's DSP dump at `E:\3ds\dspfirm.cdc`. Without
  it the port runs silent and says so in `log.txt`; that is expected.
- **Saves** are raw 512 KiB files in `E:\3ds\pokeheartgold\heartgold.sav`.
  Back one up before replacing the pack or the app.
- **Updating**: `git pull`, then run step 5 again. The save stays where it is.
  Copy the new `.3dsx` and (if the pack changed) the pack and manifest.

## Troubleshooting

- **`wsl --install` fails or WSL will not start.** Enable virtualization in
  the firmware, run `wsl --update`, and check `wsl --status`. On Windows 10,
  the “Virtual Machine Platform” and “Windows Subsystem for Linux” optional
  features must be on (`optionalfeatures.exe`).
- **The build is extremely slow.** You are probably in `/mnt/c`. Move the
  project to `~/` inside WSL as in step 2.
- **`Permission denied` or CRLF errors after copying from Windows.** Copy the
  project again inside WSL (`git clone` is best) rather than editing it
  through `/mnt/c`. If git complains about line endings:
  `git config --global core.autocrlf false`, then clone again.
- **The clock is wrong and downloads fail with certificate errors.** WSL's
  clock can drift after sleep; run `sudo hwclock -s` (or restart WSL with
  `wsl --shutdown` from PowerShell).
- **Out of memory during the build.** Use fewer jobs:
  `python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak -j 2`. You can
  also cap WSL's memory in `C:\Users\<You>\.wslconfig`:
  ```ini
  [wsl2]
  memory=8GB
  ```
  then `wsl --shutdown`.
- **A tool is missing after the install.** Open a new Ubuntu terminal and run
  `python3 scripts/setup_toolchain.py --check`. The install writes to
  `/opt/devkitpro`; the scripts add it to `PATH` themselves.
- **Antivirus scans every compiled file.** Exclude the WSL virtual disk or
  your project folder from real-time scanning if builds are unusually slow.

## Why not native Windows?

devkitPro does publish Windows packages, so a native build is not impossible.
But this repository's toolchain setup, its `make` invocations and its shell
helpers were written and tested on Linux and macOS; the player scripts are
Python and would need their devkitPro discovery (`/opt/devkitpro`) taught
about `C:\devkitPro`. Until someone does that work and tests a full build,
WSL2 is the honest recommendation. If you try native Windows and it works (or
fails in a new way), a short report with the exact commands is welcome.
