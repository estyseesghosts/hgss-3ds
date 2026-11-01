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
     port.ini                # optional, see below
     script.txt              # optional: a tests/inputs scene
     dspfirm.cdc             # optional: only if you dumped your console's
   ```

   `port.ini` is `key=value`; the only key today is `script`:

   ```ini
   script=script.txt
   ```

3. `dspfirm.cdc` is the console's own DSP firmware dump. **Without it the
   game runs silent** and `log.txt` says so; that is expected, not a crash.

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

## The scripted New 3DS run

1. Copy `tests/inputs/newgame-buttons.txt` to `sdmc:/3ds/pokeheartgold/script.txt`
   and set `script=script.txt` in `port.ini`.
2. Start the app and leave the console alone. The script presses START on the
   title, selects **NO INFO NEEDED**, advances Oak's speech and enters the
   bedroom, frame-for-frame as the PC run.
3. **Passing:** the top screen shows the same sequence as the PC screenshots
   (logo, movie, title, menu, Oak, gender, naming, bedroom); the app stays at
   full speed (no visible stutter), and `log.txt` ends with a frame count near
   the script's `end` without an exception block.
4. Save a copy of `log.txt` and note the frame count and any dropped frames.

## Frame-time budget

The shell logs a `frame N` line every 600 frames. Once the instrumentation
build is in, the log also carries per-subsystem milliseconds; the report
format and `tools/frametime_report.py` are described in
`docs/progress/phase-5.md`. Target: **59.8 Hz** in menus, intro and title.
Record the New 3DS numbers and, separately, the Old 3DS numbers.

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
