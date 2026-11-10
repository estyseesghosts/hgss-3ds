# Windows: build and run natively

> **Status: tested, native.** This page is the Windows build/run guide. It was
> worked through end to end on a Windows 11 machine in November 2026
> (devkitPro at `C:\devkitPro`, Azahar 2126.2, Python 3.15, Git for Windows):
> `tools/bootstrap.py` fetches the pinned decomp and applies all 87 patches,
> `tools/hgss_game.py --target 3ds` links `pokeheartgold.3dsx`, and the app
> boots its intro/title 3D in Azahar in Old-3DS mode. No WSL, no container,
> no MWCC/Nitro SDK. The matching DS build (Phase 0) is still Linux/macOS
> only. An untested WSL2 fallback is sketched at the bottom.

## What you need

- Windows 10 (version 2004 or newer) or Windows 11, 64-bit.
- devkitPro's Windows install at `C:\devkitPro`, with the **`3ds-dev`**
  package group (devkitARM, libctru, citro3d/citro2d, `3dsxtool`,
  `smdhtool`, `3dsxdump`), plus **`general-tools`** (`bin2s`) and
  **`picasso`** (the shader assembler). The devkitPro graphical updater
  installs all of these; `tex3ds` comes along with the group.
- devkitPro's bundled MSYS2 (at `C:\devkitPro\msys2`), plus two extra
  packages for the build's POSIX layer (host `gcc`, host `python3`):
  ```powershell
  C:\devkitPro\msys2\usr\bin\pacman.exe -S --noconfirm --needed gcc python
  ```
  (`make` 4.4.1 ships with MSYS2 already. The post-transaction `info`
  hook may report an error; `gcc --version`/`python3 --version` still work.)
- Git for Windows and a Windows Python 3.9 or newer
  (tested: 3.15.2 at `C:\Program Files\Python315\python.exe`).
- Your own US HeartGold or SoulSilver `.nds` dump. The scripts only read it.
- Azahar (tested: 2126.2 at `C:\Program Files\Azahar`).
- Optional: `dspfirm.cdc` from your own 3DS for sound. Without it the port
  runs silent and says so in `log.txt`; that is expected, not an error.

`scripts/setup_toolchain.py` does **not** run on Windows (it says so and
stops). Install the toolchain once as above instead; everything else is the
same three player scripts as Linux/macOS.

## 0. Shell setup (every PowerShell window)

The scripts default to `/opt/devkitpro`, which does not exist on Windows,
so export the install location first. Do this in **each** PowerShell window
you build from:

```powershell
$env:DEVKITPRO = 'C:\devkitPro'
$env:DEVKITARM = 'C:\devkitPro\devkitARM'
$env:PATH = 'C:\Program Files\Git\cmd;C:\devkitPro\msys2\usr\bin;C:\devkitPro\tools\bin;C:\devkitPro\devkitARM\bin;' + $env:PATH
```

Order matters: Git's `cmd` before MSYS2 (so both gits agree on line
endings), then the devkitPro bins (so `make`, host `gcc`/`python3`,
`3dsxtool` and `arm-none-eabi-gcc` resolve).

> **Never run bare `py`.** Installing MSYS2's `python` adds a `py` that
> shadows `C:\Windows\py.exe`, and the MSYS Python mangles Windows script
> paths. Always invoke the full interpreter:
> `& 'C:\Program Files\Python315\python.exe' <script> ...`
> (adjust the path to your install). Inside MSYS2 make recipes `python3`
> correctly resolves to the MSYS build.

To make the variables permanent instead, use `setx` once (new windows only):

```powershell
setx DEVKITPRO 'C:\devkitPro'
setx DEVKITARM 'C:\devkitPro\devkitARM'
```

## 1. Fetch and patch the decompilation

```powershell
cd C:\Users\<You>\workdir\hgss-3ds   # wherever your checkout is
& 'C:\Program Files\Python315\python.exe' tools\bootstrap.py
```

Expect `upstream: already at 8cbb4a19ab78 with 87 patches` on repeat runs.
Two Windows notes:

- The build uses directory junctions/hard links where POSIX uses symlinks
  (no elevated privileges needed); `tools/` handles that itself.
- After the **first** fetch, align the two gits on line endings, or the
  next run reports `build/upstream has local changes` while everything
  looks clean:
  ```powershell
  git -C build\upstream config core.autocrlf true
  ```
  Re-apply this after `tools/bootstrap.py --clean`, which deletes the
  setting along with the checkout.

## 2. Prepare the game data from your ROM

```powershell
& 'C:\Program Files\Python315\python.exe' scripts\prepare_game.py C:\path\to\heartgold.nds
```

This verifies the dump (clean US ROM only), extracts it to
`build/game-data/`, and packs `build/heartgold3ds.pak` (about 120 MiB).
SoulSilver works the same way and produces `build/soulsilver3ds.pak`.

## 3. Build the app and the SD-card folder

One command does bootstrap (fast when done), the 3DS compile/link/package,
and the SD-card staging:

```powershell
& 'C:\Program Files\Python315\python.exe' scripts\build_3dsx.py --pak build\heartgold3ds.pak
```

The first build takes about ten minutes (1,092 game files, the vendored
`libnitro`/`libnitrosystem` through MSYS2 `make`, then the link); later
builds are incremental. It stages into `dist\sd-card\`:

```
dist\sd-card\
└── 3ds/
    ├── pokeheartgold.3dsx
    └── pokeheartgold/
        ├── heartgold3ds.pak
        └── manifest.json
```

A successful run ends with `packaged ... pokeheartgold.3dsx` and
`LASTEXITCODE 0`; the reference HeartGold build is 5,834,596 bytes. To
redo only the compile exactly as the script runs it:

```powershell
& 'C:\Program Files\Python315\python.exe' C:\...\hgss-3ds\tools\hgss_game.py --game heartgold --target 3ds --keep-going --out C:\...\hgss-3ds\build\game-3ds -j 8
```

## 4. Run it in Azahar (Old-3DS mode)

Azahar emulates its SD card at `%APPDATA%\Azahar\sdmc\` (that trailing
`sdmc\`). Copy the staged `3ds` folder onto it, or point the script at it:

```powershell
Copy-Item dist\sd-card\3ds $env:APPDATA\Azahar\sdmc\ -Recurse -Force
```

The card must end up with:

```
%APPDATA%\Azahar\sdmc\3ds\pokeheartgold.3dsx
%APPDATA%\Azahar\sdmc\3ds\pokeheartgold\heartgold3ds.pak
%APPDATA%\Azahar\sdmc\3ds\pokeheartgold\manifest.json
```

Then pin Azahar to the Old 3DS and start the game:

1. Close Azahar if it is running.
2. In `%APPDATA%\Azahar\config\qt-config.ini`, under `[System]`, set
   **both** lines (the `\default=false` marks the value as user-set; the
   value alone is ignored and Azahar resets it to New 3DS):
   ```ini
   is_new_3ds\default=false
   is_new_3ds=false
   ```
3. Optional but recommended: `attrib +R` the ini file so Azahar cannot
   flip the console back on exit. (UI setting changes then don't persist;
   `attrib -R` to unpin.)
4. Start Azahar opening the app:
   ```powershell
   Start-Process 'C:\Program Files\Azahar\azahar.exe' -ArgumentList "$env:APPDATA\Azahar\sdmc\3ds\pokeheartgold.3dsx"
   ```

What to check:

- `%APPDATA%\Azahar\log\azahar_log.txt` shows
  `System_IsNew3ds: false` near the top.
- The port writes `%APPDATA%\Azahar\sdmc\3ds\pokeheartgold\log.txt`.
  In O3DS mode its perf lines read `drawer 0 (core 0, both screens)` and
  `smooth 0` (New-3DS runs show a `core 2` drawer and nonzero smooth).
- Without the pack/manifest the app shows its "cannot read the data pack"
  error screen — that still proves the `.3dsx` runs. With them, the boot
  reaches 3D scenes within a minute (frame counter and `gx3d` lines
  advance; a `heartgold.sav` appears once the game first saves).
- Without `sdmc\3ds\dspfirm.cdc` there is no audio; the log says so.

## Troubleshooting

- **`build/upstream has local changes` right after a clean fetch.** The
  MSYS2 and Windows gits disagree on CRLF. Apply the `core.autocrlf true`
  step from section 1.
- **`FileNotFoundError` naming `python3`, `picasso`, `smdhtool`, ...**
  The PATH order in section 0 is wrong, or a bare `py` picked up MSYS
  Python. Re-export PATH exactly as shown and use the full interpreter
  path.
- **`WinError 206 (filename too long)`.** Fixed in `tools/` (response
  files); update your checkout if you see it.
- **Azahar boots as New 3DS (`System_IsNew3ds: true`).** Both ini lines
  from step 4.2 are required, and Azahar must have been closed when you
  edited them.
- **The emulator shows nothing / exits.** Check the port `log.txt` first:
  a missing pack or manifest stops the boot with an on-screen message and
  names the file in the log.
- **Slow emulation.** Expected on first boot (shader/pipeline warmup) and
  generally slower than a New 3DS; measure frame rate on hardware, not here.

## WSL2 fallback (untested)

If the native path fails somewhere new, the original WSL2 plan still
applies: install WSL2 + Ubuntu, keep the checkout in `~/` (never in
`/mnt/c`), run the three player scripts unchanged inside Ubuntu, and copy
`dist/sd-card/3ds` onto the SD card or Azahar's `sdmc`. Details were never
verified; report back if you go this way.
