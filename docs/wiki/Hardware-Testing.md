# Testing on hardware

Everything on this page is a **user run**: the container cannot attach a 3DS,
read an SD card or play a cartridge. Each section says what a passing run
looks like so the result can be recorded without guesswork.

## Before the first run

1. Build the artifacts in the container:

   ```bash
   python3 tools/hgss_game.py --target 3ds --keep-going --out build/game-3ds
   ls build/game-3ds/pokeheartgold.3dsx
   ```

2. Put the game's own files on the SD card (nothing below is committed):

   ```text
   sdmc:/3ds/pokeheartgold/
     heartgold3ds.pak        # builder/pack.py from your cartridge
     manifest.json
     heartgold.sav           # created by the game; a raw 512 KiB save
     log.txt                 # created by the game (rewritten each start)
     port.ini                # optional, see below
     script.txt              # optional: a tests/inputs scene
   ```

   The DSP firmware is **not** in that folder — libctru looks for it at a
   fixed path:

   ```text
   sdmc:/3ds/dspfirm.cdc     # optional: your own console's dump
   ```

   `port.ini` is `key=value` and lives in the data folder. Two keys today:
   `script` (the input script) and `fps` (the frame cap). Give `script` an
   absolute path or a name inside the data folder:

   ```ini
   script=sdmc:/3ds/pokeheartgold/script.txt
   fps=60
   ```

   Without `port.ini`, the game still picks up `script.txt` from the data
   folder on its own, so for a scripted run the file alone is enough.
   `fps` accepts `60` (the default) or `30`; `30` is a comparison mode that
   **halves the game's speed**, because the game clock advances once per
   frame. Any other value (or no key) uses 60 Hz.

3. `dspfirm.cdc` is the console's own DSP firmware dump. **Without it the
   game runs silent** and `log.txt` says so; that is expected, not a crash.

## Installing without 3dslink

The `.3dsx` is the **program**, not a ROM — the ROM-derived data is
`heartgold3ds.pak`. Copy it anywhere the Homebrew Launcher scans, normally
the top of `/3ds`:

```text
sdmc:/3ds/pokeheartgold.3dsx   # the payload (about 8.7 MiB)
```

The Homebrew Launcher lists it by file name, so `pokeheartgold.3dsx` reads
better than `rom.3dsx`. Only the `.3dsx` is needed: the SMDH metadata and
icon are already embedded in it by `3dsxtool --smdh=…`, so
`pokeheartgold-3ds.smdh` does **not** go on the card. Put the payload next
to the `pokeheartgold/` folder rather than inside it — the folder is
created by the game if missing.

The data pack is about 120 MiB, so leave room on a FAT32 card (the 3DS does
not read exFAT).

## Deploying with 3dslink

1. On the 3DS, open the Homebrew Launcher and press **Y** (3dslink
   listener). Note the IP address it shows.
2. From the container or the host:

   ```bash
   ../../tools/3dslink/3dslink -a <3ds-ip> build/game-3ds/pokeheartgold.3dsx
   ```

   (Any `3dslink` build works; `devkitPro` ships one, and the container can
   install it if needed.)
3. The app starts from `sdmc:/3ds/pokeheartgold/`. `log.txt` is rewritten on
   every start; copy it off after a run.

## Checking the artifact before it reaches the card

Two things go wrong silently on the host and only show on the console, so
check the file you are about to copy:

```bash
# the packaging step already ran this and failed the build if it found a
# problem (tools/hgss_game.py: validate_3dsx)
/opt/devkitpro/tools/bin/3dsxdump build/game-3ds/pokeheartgold.3dsx /tmp/out.bin
#   CODE / RODATA / DATA / BSS page counts and exit 0 is what you want
```

A rejected file prints `Cannot write segment data!`; the console then aborts
in its Loader with an undefined-instruction dump naming the process `loader`.
See [3DS-Pitfalls](3DS-Pitfalls.md#one-relocation-held-symbol---0x06800000).

The frame-time instrumentation is in: every 600 frames the shell writes a
`FRAMETIME` line, and `tools/frametime_report.py log.txt` turns a whole log
into the budget table (mean/min/p95/max per subsystem, the frame rate, and how
many frames exceeded 16.7 ms). `audio_us` is filled by the audio thread once
audio is wired; `draw_us` includes the flip wait, so `total_us` is the number
that decides whether it feels smooth.

## Logs over ftpd

If the console runs `ftpd` (Homebrew Launcher), from the host:

```bash
curl -s ftp://<3ds-ip>/3ds/pokeheartgold/log.txt -o log.txt
curl -s ftp://<3ds-ip>/3ds/pokeheartgold/heartgold.sav -o heartgold.sav
```

A crash log begins with `=== exception (type N) pc ...` and then lists r0-r12,
sp, pc, the last log lines and 16 stack words. Send that file; it is enough to
map the pc with `addr2line -e build/game-3ds/pokeheartgold-3ds.elf <pc>`.

## Rosalina GDB

1. In Rosalina (L+Down+Select), enable **Debugger** and note the port.
2. On the host/container:

   ```bash
   gdb-multiarch build/game-3ds/pokeheartgold-3ds.elf
   (gdb) target remote <3ds-ip>:4000
   (gdb) continue
   ```

   `gdb-multiarch` is not installed in the container by default; install it
   only if you want this path (it is a host tool, not a build dependency).
3. Break on `Heartgold_Run`, `NitroMain` or `HgssOnException` as needed.

## Reading `log.txt`

The first lines say what the build thinks it is running on and what it
connected:

```text
[perf] New 3DS: 804 MHz + L2 requested     (or "268 MHz, the speedup is New-3DS only")
[perf] frame cap 60 Hz
NDSP started
audio: 32768 Hz stereo, 4 blocks of 512 frames
```

Then the run. These lines mean specific things, and each one is there because
a failure used to be ambiguous:

| Line | Meaning |
| --- | --- |
| `[perf] frame cap 60 Hz` | The frame cap in use. `30 Hz` means `port.ini`'s `fps=30` comparison mode: the game clock is per frame, so the game runs at half speed. |
| `[perf] port.ini fps=… is not 60 or 30; using 60 Hz` | The `fps` value was ignored; the run uses the default. |
| `still starting: no frame after 10 s` | Stuck inside the game's init; nothing killed it. |
| `the HOME menu asked to close (frame N)` | APT delivered a close; the shell stops in order. |
| `the game's thread is returning (frame N)` | `NitroMain` returned by itself. |
| `threadJoin says the game's thread ended, but it is still running` | libctru's join lied; the shell's own flag keeps the run alive (expected on this port). |
| `[gx3d] PICA ready (classic path)` | The GPU worker initialised; the 3D path is live. |
| `[gx3d] display on core 2` | The GPU/display worker has the New 3DS's free core, so its render overlaps the software 2D composite. |
| `[gx3d] geometry on its own thread` | The geometry thread started; scenes are built off the game's thread. |
| `3D images: N (scenes drawn: M, camera-plane drops: D)` (every 600 frames) | `N` must **advance**: about 300 per 600 frames (30 a second, the scene rate) now the worker has a core, not 10. If it stays low, the 3D is updating slowly whatever the screen shows. `D` should be small; it is triangles near the camera plane that were removed to stop the centre-fan. |
| `[gx3d] 3D setup failed …` / `couldn't start the 3D thread` / `no geometry thread` | The safe fallback: the game still runs 2D-only. Send the line; it names the failing step. |
| `=== exception (type N) pc …` | A real fault in game code; map the pc with `addr2line`. |
| `audio: N dropped frames of M` (at exit) | `N` large relative to `M` means the DSP was starved. |
| `Can't start: …` | The pack or manifest is missing, damaged or from another build. |

Send `log.txt` whole; nothing on that page needs filtering.

The noise-channel fix (patch 1407) has no log line of its own: its pass is by
ear. Play a field and a battle and confirm no note sustains into the next one
and single sounds are not distorted. `audio: N dropped frames of M` near zero
is the other half of the audio result.

## The scripted New 3DS run

1. Copy `tests/inputs/newgame-buttons.txt` to `sdmc:/3ds/pokeheartgold/script.txt`.
   That is enough on its own; if you want to name it something else, add
   `script=sdmc:/3ds/pokeheartgold/<name>.txt` to `port.ini`.
2. Start the app and leave the console alone. The script presses START on the
   title, selects **NO INFO NEEDED**, advances Oak's speech and enters the
   bedroom, frame-for-frame as the PC run.
3. **Passing:** the top screen shows the same sequence as the PC screenshots
   (logo, movie, title, menu, Oak, gender, naming, bedroom); the bedroom's room
   and player sprite are visible behind the dialogue box, the intro movie's 3D
   renders, and nothing fans out black from the centre of the picture. `3D
   images:` rises by roughly 300 every 600 frames while 3D is up, and walking
   animates smoothly rather than once a second. The app stays at full speed (no
   visible stutter), and the game runs at its intended speed rather than fast:
   the log's mean `total_us` from `tools/frametime_report.py log.txt` is near
   **16,700** (60 Hz), not 11,500 (87 Hz). Audio is clean by ear — no note
   hangs into the next one, which is what patch 1407 fixes — and `log.txt` ends
   with a frame count near the script's `end` without an exception block.
   `audio: N dropped frames of M` should be near zero relative to `M`.
4. Save a copy of `log.txt` and note the frame count and any dropped frames.

## Frame-time budget

The shell logs a `frame N` line every 600 frames. Once the instrumentation
build is in, the log also carries per-subsystem milliseconds; the report
format and `tools/frametime_report.py` are described in
`docs/progress/phase-5.md`. Target: **60 Hz** in menus, intro and title. The
frame cap sets that pace: `total_us` should average near **16,700** (the
period is `SYSCLOCK_ARM11 / 60` = 4,468,530 ticks ≈ 16,667 us). A mean near
11,500 (87 Hz) means the cap is not in the artifact or not reached; a mean
near 33,400 means `fps=30` is set. `fps=30` reports as missing the 59.8 Hz
target by design. Record the New 3DS numbers and, separately, the Old 3DS
numbers.

## Old 3DS measurement (decision pending)

Run the same scripted scene on an Old 3DS (or 2DS). Record:

- whether it boots and reaches Oak's speech at all;
- the average frame time in the menu, the intro movie and the title;
- any visible slowdown or audio dropouts.

The support decision (supported / menus only / not supported) is the user's;
it is recorded in `docs/decisions.md` only after this measurement.

## Save round-trip

Both directions are required by the Phase 5 exit.

**Port save → melonDS Continue**

1. Finish Oak's speech and save in-game (Start/X menu → SAVE).
2. Copy `sdmc:/3ds/pokeheartgold/heartgold.sav` off the SD.
3. In melonDS, load the original HG ROM with that file as the `.sav`.
4. **Passing:** Continue works and shows the same trainer name, gender and
   play time.

**Cartridge save → port Continue**

1. Copy a real cartridge save to `sdmc:/3ds/pokeheartgold/heartgold.sav`.
2. Start the port build and choose Continue.
3. **Passing:** the trainer, party and location match the cartridge.

Record both results (with the save's SHA-1 and the trainer name) in
`docs/progress/phase-5.md`. Do not mark either direction done without the run.
