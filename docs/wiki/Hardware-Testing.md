# Testing on hardware

Everything on this page is a **user run**: the container cannot attach a 3DS,
read an SD card or play a cartridge. Each section says what a passing run
looks like so the result can be recorded without guesswork.

## Before the first run

1. Build the artifacts in the container:

   ```bash
   python3 tools/hgss_game.py --target 3ds --keep-going --out build/game-3ds
   python3 tools/hgss_game.py --game soulsilver --target 3ds --keep-going --out build/game-3ds-ss
   ls build/game-3ds/pokeheartgold.3dsx build/game-3ds-ss/pokesoulsilver.3dsx
   ```

2. Put the game's own files on the SD card (nothing below is committed).
   HeartGold reads `sdmc:/3ds/pokeheartgold/`; SoulSilver reads
   `sdmc:/3ds/pokeheartgold-ss/` and has the same layout:

   ```text
   sdmc:/3ds/pokeheartgold/
     heartgold3ds.pak        # builder/pack.py from your cartridge
     manifest.json
     heartgold.sav           # created by the game; a raw 512 KiB save
     log.txt                 # created by the game (rewritten each start)
     port.ini                # optional, see below
     script.txt              # optional: a tests/inputs scene

   sdmc:/3ds/pokeheartgold-ss/
     soulsilver3ds.pak
     manifest.json
     soulsilver.sav
     log.txt
     port.ini                # optional
     script.txt              # optional
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
`heartgold3ds.pak` (or `soulsilver3ds.pak`). Copy it anywhere the Homebrew
Launcher scans, normally the top of `/3ds`:

```text
sdmc:/3ds/pokeheartgold.3dsx   # HeartGold (about 8.9 MiB)
sdmc:/3ds/pokesoulsilver.3dsx  # SoulSilver
```

The Homebrew Launcher lists it by file name, so `pokeheartgold.3dsx` reads
better than `rom.3dsx`. Only the `.3dsx` is needed: the SMDH metadata and
icon are already embedded in it by `3dsxtool --smdh=…`, so
`pokeheartgold-3ds.smdh` does **not** go on the card. Put the payload next
to the `pokeheartgold/` folder rather than inside it — the folder is
created by the game if missing.

`scripts/build_3dsx.py` stages exactly this layout under `dist/sd-card/`, so
copying is one drag; see the [player's guide](../USER-GUIDE.md#step-4--copy-it-to-the-sd-card).

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

Every 600 frames the shell writes a `FRAMETIME` **sample**, and
`tools/frametime_report.py log.txt` summarizes those samples. Current fields
are `game_us`, `state_us`, `total_us` and H-blank/handoff breakdowns, not the old
`draw_us`/`audio_us` schema. Samples are not an all-frame latency distribution
or a visible-frame rate. Read them alongside the interval `[perf]` counters
described below.

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
audio: mixer 32768 Hz, DSP 32728 Hz (no resampling), 4 blocks of 512 frames
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
| `[compose] GPU display ready (remaster, no classic fallback)` | The compositor initialised; every frame is presented through it. |
| `[compose] display thread ready` | The display thread started before the game. |
| `[compose] drawing: engine A on core N, engine B on core M` | The drawer threads started. On an Old 3DS both engines share one drawer on core 0. |
| `[gx3d] geometry on its own thread (core N)` | The geometry thread started; scenes are built off the game's thread. |
| `3D images: N (scenes drawn: M)` (every 600 frames) | `N` must **advance**: about 300 per 600 frames (30 a second, the scene rate). If it stays low, the 3D is updating slowly whatever the screen shows. |
| `[gx3d] citro3d init failed; there is no display` / `[compose] the GPU display could not be set up; there is no display` / `[compose] the display did not start` | Fatal by design: `DisplayErrorScreen()` is shown and the game does not run. Send the line and the `log.txt`; there is **no 1:1 fallback** to diagnose around. |
| `[compose] couldn't start a drawing thread` / `the V-blank thread` | The display did not start; same error screen. Send the line (core and priority are in the message). |
| `=== exception (type N) pc …` | A real fault in game code; map the pc with `addr2line`. |
| `audio: N dropped frames of M` (at exit) | `N` large relative to `M` means the DSP was starved. |
| `Can't start: …` | The pack or manifest is missing, damaged or from another build. |

Send `log.txt` whole; nothing on that page needs filtering.

The noise-channel fix (patch 1407), the NNS volume-table fix, the
pre-saturation 6 dB headroom and the NDSP staging have no log line on the
console beyond `audio: mixer 32768 Hz, DSP 32728 Hz (no resampling)`; the build
prints `[sound] _0210D750 -> ...` and the pass is by ear. Play a field and a
battle and confirm a note fades out instead of sustaining a high-pitched
crunch into the next note, single sounds are not distorted, and the mix does
not sound rough or crunchy at normal or low console volume. `audio: N dropped
frames of M` near zero is the other half of the audio result.

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
   renders, and nothing fans out from a corner of the picture. `3D
   images:` rises by roughly 300 every 600 frames while 3D is up, and walking
   animates smoothly rather than once a second. The title's Ho-oh has its
   wings and body intact: the camera-plane fan is gone, with no transparent
   gap. The app stays at full speed (no visible
   stutter), and the game runs at its intended speed rather than fast:
   the log's mean `total_us` from `tools/frametime_report.py log.txt` is near
   **16,700** (60 Hz), not 11,500 (87 Hz). Audio is clean by ear — no note
   hangs into the next one (1407 plus the NNS volume table), no sample-domain
   clipping even at low console volume (the 6 dB pre-saturation headroom), and
   no rough/crunchy timbre (the NDSP cache flush and native rate) — and
   `log.txt` ends with a frame count near the script's `end` without an
   exception block.
   `audio: N dropped frames of M` should be near zero relative to `M`.
4. Save a copy of `log.txt` and note the frame count and any dropped frames.

## Settings open and close

Once the field is up (the scripted run ends in the bedroom; the start menu
becomes available after mother's dialogue), open the start menu with **X**,
choose **OPTIONS**, then close it with **QUIT**. The game must fade back to
the field and keep running. This was the session-11 crash: `OptionsApp_Free`
freed the BG config one frame before it stopped its V-blank callback, and the
V-blank in between started a DMA from the freed config (patch **1904** fixes
it). A crash here is a data abort in `LoadBgVramScr`; send `log.txt` or the
Luma dump. Open and close it twice in a row; both returns must reach the
field.

## Wild encounters and battles

With a field save (or after getting the starter), walk into tall grass on
Route 29 until a wild Pokémon appears. The battle must load: the screen
transitions, the battle overlays load, the Pokémon and the opponent's
silhouette appear, and the menu accepts input. **Both Pokémon must be
visible** while the menu waits (their 3D sprites are the Poképics; before
patches 0304/0605 they were invisible between attacks). Finish the battle:
the HP bars change and the field returns. Before patch **1905** the
battle aborted on its first frame: `WirelessStatusIcon_LoadChar` read DISPCNT
through `0x04000000`, which is unmapped on the port (log-10: exception,
`r3 04000000`, pc in that function). With that fixed it aborted a little
later, in `BattleSystem_SetBackground`'s 64 KB copy from BG VRAM
`0x06010000` (log-11; the opponent sprite appeared, the player's did not).
Both sites now use the port's arrays. If tall grass instead shows "A
communication error has occurred. You will be returned to the title screen",
that is `PrintErrorMessageAndReset` from a save-substruct assert, not the
wireless stack: the save was written by an early port build with the wrong
substruct CRCs. Patch **1907** migrates them on first read; a clean cartridge
save does not need it. Trainer and link battles take the same
`Battle_CreateWirelessStatusIcon` and battle-background paths, so a trainer
battle is a second check. If it still faults, send `log.txt` and the Luma
dump; the pc maps with `arm-none-eabi-addr2line -e pokeheartgold-3ds.elf`.

Owner-confirmed on Old 3DS hardware (pre-fix `b93c6ad`): the battle message
box renders H-mirrored while status boxes stay correct — a real port defect,
not an emulator artifact. The committed veto (patch 1910, game-driven battle
flag forcing software BGs) is verified on Azahar but still needs a hardware
photo A/B: same grass, same encounter, pre-fix vs veto build at the command
prompt. The exact quad-path defect is open; see the [battle validation
notes](../progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md).

The same class of literal crashed later screens (the Pokémon summary, the
encounter transition, the Hall of Fame, trades); patch 1905 converted all of
them. Reaching any of those screens is a bonus check.

## Closing the app

From the field (or any screen), open the HOME menu and choose **Close**. The
app must return to the HOME menu within a second or two. `log.txt` ends with
the shutdown steps: `[quit] closing`, `[quit] APT unhooked`, `[quit] geometry
stopped`, `[quit] display stopped`, `[quit] sound stopped`, `stopping (frame
N)`. Before the fix it stopped at `closing at frame N` and the console had to
be power-cycled (the geometry thread was joined before its last scene was
drained; log-12). If it hangs, the last `[quit]` line names the stuck step;
send the log.

## The SoulSilver program

Build it with `make game-3ds-ss` (or `--game soulsilver`), copy
`pokesoulsilver.3dsx` to `sdmc:/3ds/`, and the pack to
`sdmc:/3ds/pokeheartgold-ss/`. Start **pokesoulsilver** from the Homebrew
Launcher: it has its own intro/title and writes `soulsilver.sav` and
`log.txt` in that folder. The two games can sit on the same card; neither
reads the other's folder. Check that the title is the SoulSilver one and the
save file appears under the SS folder (not next to HeartGold's).

## Frame-time budget

The shell logs a `frame N` line every 600 frames. Once the instrumentation
build is in, the log also carries per-subsystem milliseconds; the report
format and `tools/frametime_report.py` are described in
`docs/progress/phase-5.md`. Target: **60 Hz** in menus, intro and title. The
frame cap requests that pace (period `SYSCLOCK_ARM11 / 60` = 4,468,530 ticks,
approximately 16,667 us). Samples near that value are a useful check, not proof
of normal gameplay speed. Confirm configuration from the banner rather than
inferring it from latency: overruns, waits and scheduling also affect samples.
`fps=30` deliberately halves the per-frame game clock and is not a playability
fix. Record New and Old 3DS results separately.

## Reading the `[perf]` lines

Every 600 game frames the shell writes one block. The fields that matter, in
the order they answer questions:

| Line | Fields | What they mean |
| --- | --- | --- |
| `FRAMETIME` | `game_us` | `max(total_us - previous state_us, 0)`: residual interval time, not isolated game CPU time |
| | `state_us` | the whole hand-off (`DrawFrame`) for the *previous* frame |
| | `total_us` | sampled interval between `NitroPort_OnFrame` entries; not visible FPS or interval-average update rate |
| | `hblank_us` | the H-blank phase of the hand-off |
| | `hb_keep_us` / `hb_dma_us` / `hb_irq_us` | that phase split: per-line register copy, DMA, interrupt dispatch |
| `[perf] display` | `composes/s`, `build`, `GPU/waited`, `publishes/s`, `publish` | the display thread; `scene build` is its 3D CPU work |
| `[perf] geometry` | `scenes/s`, `busy %`, `ms/scene` | the geometry thread |
| `[perf] audio` | `% of core 0`, mixing ms | the mixer's real time |
| `[perf] drawer` | `frames/s`, `ms/frame`, `render`, `publish` | the 2D drawer |
| `[perf] mem` | `linear_free`, `game_heap_used` | the two numbers the heap split is chosen from |
| `[perf] oam` | `top=A\|B`, `shown`, `identical`, `most-repeated tile TxM` | whether duplicated sprites are in the sprite table or only in the picture |
| `[perf] core 1 share` | `asked N%`, `allows M%` | the APT share actually granted (80% on an Old 3DS, not 30%) |

Rules learned the hard way (see
[3DS-Pitfalls](3DS-Pitfalls.md#diagnose-the-diagnostic-first)):

- These are **wall clock**. On a saturated core every number is inflated by
  preemption, so a large `ms/frame` does not by itself mean that thread is
  slow — compare against what else was running.
- `FRAMETIME`'s `state_us` describes the previous frame, deliberately: an
  interval and a phase from the same instant once produced `total_us` <
  `state_us`, which is impossible.
- `hb_*` are the **last walk's** split. A frame that did not arm H-blank prints
  stale values beside an 8 us `hblank_us`; don’t read them as this frame's.
- `total_us` is entry-to-entry wall time; `state_us` times `DrawFrame`, not
  just a memory copy. The interval can include prior pacing, waits and scheduling.
  `WaitForFrameCap` runs at the end, but its banner alone does not establish
  achieved update cadence. Run 27 has samples around 3 ms despite the 60-Hz
  setting: keep W1 pacing unresolved rather than declaring normal speed or
  deducing sustained 333-Hz updates from sparse samples. Check elapsed frame
  progression, gameplay/audio speed and interval counters together.

## Emulator preflight before another hardware run

Use [Renderer-Validation](Renderer-Validation.md) for the serial Azahar procedure,
capture replay and failure recovery. An Old/New emulator profile proves neither
the console's CPU budget nor APT lifecycle behavior. A forced emulator shutdown
is not HOME-close acceptance; silent operation without `dspfirm.cdc` is not an
audio pass.

The 2026-11-04 candidate is **b93c6ad**. Its final artifacts separately passed SS
Old/New Goldenrod through 5400 and HG Old title through 6600. The longer HG New
new-game-to-bedroom run (19800) used **1dc11dc**, before the final exact scaling
optimization. Do not attribute that entire matrix to the final pair.
Exact hashes, staged paths and pending gates are in [STATE](../progress/STATE.md).
The final field profile has zero CPU scanlines/row copies at 5400 versus baseline
**57408 empty lines / 299 regions**. This establishes reached work avoidance,
not hardware FPS improvement. Run 27 / **96b3131** remains the latest matched
hardware baseline; its later, different-geometry intervals are not Goldenrod
benchmark gains. Full W3/W4 acceptance, geometry CPU attribution and the ≤16-ms
target remain open, as do writer-generation/publish work and W2 battle UI.

Before believing a run, confirm the build is the one you think: the startup
banner prints `2D … (N texels a pixel)`, `the display on core N`, and the
`texscale`/`filter`/`stereo`/`wide` options.

## Old 3DS measurement

Measured across sessions 19–20; the numbers and the work are in
[agent-notes/s20-performance-session.md](../progress/agent-notes/s20-performance-session.md).
Short version: the field runs at 10–14 composes/s (was 0.3), drawer 55–81
ms/frame (was 2,000–4,500), game frame 2.3–9.2 ms (was 13.7–41). Goldenrod
outdoors is the outlier at 5–6 composes/s because it clips ~2,784 vertices per
scene. The remaining wall is the software 2D renderer.

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
3. **Passing:** the trainer, party and location match the cartridge. Raw
cartridge/emulator saves are read as-is after patch **1906** (the CRC table
fix). A DeSmuME `.dsv` still needs `tools/savetool.py normalize` first. A save
written by an early port build has legacy substruct CRCs; the port migrates
them on first read (1907), and the next in-game save writes a clean file.

Record both results (with the save's SHA-1 and the trainer name) in
`docs/progress/phase-5.md`. Do not mark either direction done without the run.
