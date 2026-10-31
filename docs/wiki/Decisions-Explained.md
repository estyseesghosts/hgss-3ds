# Decisions explained

This page explains why the project is the way it is. Each entry says what was decided, the reason given, and the evidence in the repo. Each entry links to the file where the decision is written down.

There are three sources, and each entry names its source:

- **Plan decisions.** The decisions table and the decisions log in [gameplan.md](../../gameplan.md), and the ground rules at the top of the plan.
- **Implementation decisions.** The log in [docs/decisions.md](../decisions.md). The plan's own table is authoritative where the two cover the same ground, as the log's header says.
- **Decisions in patch messages.** Choices explained inside the commit messages of the patches in `patches/`.

Where the repo does not give a reason, this page says so.

## Part 1: Plan decisions

### Base the hardware layer on pokeplatinum-3ds

**Decided:** reuse the Platinum port's `3ds_port/`, and contribute back where it makes sense (2026-10-30).

**Reason given:** the layer is game-independent, "tested on hardware", and HGSS is the stated target of that port ([gameplan.md](../../gameplan.md), decisions log). The plan also says about 60k lines of `3ds_port/` are game-independent and carry over ("What we reuse from pokeplatinum-3ds").

**Evidence:** `platinum.lock` pins the commit. The vendored `3ds_port/` is in the repo, and its README describes what is real and what is stubbed. The claim "tested on hardware" is the plan's, and this page did not verify it.

**Source:** [gameplan.md](../../gameplan.md), "Decisions taken" and "What we reuse from pokeplatinum-3ds". See also [platinum.lock](../../platinum.lock).

### Software 2D first

**Decided:** draw 2D on the CPU, and composite it on the GPU (2026-10-30).

**Reason given:** "Proven; fastest path to Oak's speech and gameplay" ([gameplan.md](../../gameplan.md), decisions log).

**Evidence:** the port's software renderer already covers the BG modes, OBJs, windows, blending and brightness ([3ds_port/nitro/src/port_render2d.c](../../3ds_port/nitro/src/port_render2d.c) header). The plan keeps it as the fallback for the later GPU renderer.

**Source:** [gameplan.md](../../gameplan.md), decisions table.

### A PICA200 2D renderer later

**Decided:** a GPU renderer for both 2D engines is a long-term goal (Phase 11). It must match the software renderer pixel for pixel (2026-10-30).

**Reason given:** the owner's goal. The plan says it frees CPU time and may make Old 3DS full-speed possible ([gameplan.md](../../gameplan.md), decisions log; Phase 11, "Success means two things").

**Evidence:** Phase 11 explains why this is hard on the PICA200: there are no fragment shaders, no paletted textures, and the DS blending is per pixel. The design section of the plan follows from that. `docs/2d-gpu.md` is not written yet.

**Source:** [gameplan.md](../../gameplan.md), Phase 11.

### Save format unchanged

**Decided:** the save stays a 524,288-byte raw image, with the same offsets, layouts and checksums (2026-10-30).

**Reason given:** "Cart ↔ port ↔ emulator interchange" ([gameplan.md](../../gameplan.md), decisions log). A player's save is copied between the cart, emulators and the port as a file.

**Evidence:** the save compatibility spec in the plan, the savetool's format description, and the layout probe, which found the 64-bit alignment difference that patch `1001` fixes. See [Save-Compatibility](Save-Compatibility.md).

**Source:** [gameplan.md](../../gameplan.md), "Save compatibility specification"; [tools/savetool.py](../../tools/savetool.py) docstring.

### Infrared through the 3DS `ir:USER` service

**Decided:** Pokéwalker infrared is in scope, and the plan implements it with the 3DS `ir:USER` service (2026-10-30).

**Reason given:** "Owner's requirement" ([gameplan.md](../../gameplan.md), decisions log).

**Caveat in the record:** the decisions table says "Implemented with", but Phase 9 makes that conditional. It names two paths, `ir:USER` (path A) and direct `i2c::IR` access (path B). It says "Try A first; fall back to B if A's wire format or timing is wrong", and it requires a go/no-go test with a 20-of-20 requirement before game integration ([gameplan.md](../../gameplan.md), Phase 9). So the table records the path to try, not a verified choice. Nothing has been built for Phase 9 yet.

**Source:** [gameplan.md](../../gameplan.md), decisions table and Phase 9.

### Online stubbed; 3DS-to-3DS over UDS later; DS-to-3DS not pursued

**Decided:** Wi-Fi and online are stubbed. Local play between two 3DS consoles over UDS is Phase 10. DS-to-3DS linking is not pursued (2026-10-30).

**Reason given:** "Protocol mismatch; no raw 802.11 access in native 3DS mode" ([gameplan.md](../../gameplan.md), decisions log). Phase 10c explains it: the DS uses Nintendo's own wireless protocol on raw 802.11 frames, run by the DS's wireless firmware. Native 3DS software gets only UDS, which is a different protocol. There is no supported way for homebrew to send raw 802.11 frames in native 3DS mode.

**Evidence:** the plan's Phase 10c, and the NitroSDK card stub (patch `0701`), which is the same approach of stubbing what cannot work yet. The plan's suggested alternative is moving saves with a PC save tool, which is not built.

**Source:** [gameplan.md](../../gameplan.md), Phase 10c.

### Display: DS screens centered 1:1 at first

**Decided:** the first display shows both DS screens centred at 1:1. Wide-screen and remaster layouts come later, "if at all" (2026-10-30).

**Reason given:** the table records the choice without a stated reason. The plan's component map gives one for the Platinum layouts: the wide-screen and lower-menu paths are "tied to Platinum's UI patches" and are "later or never" ([gameplan.md](../../gameplan.md), "Component map").

**Source:** [gameplan.md](../../gameplan.md), decisions table; Phase 5, step 2.

### GPL-3.0

**Decided:** the project is GPL-3.0 (2026-10-30).

**Reason given:** reuse of the Platinum port's GPL-3.0 code makes the HGSS port GPL-3.0 as well ([gameplan.md](../../gameplan.md), decisions log and ground rule 6).

**Evidence:** [LICENSE](../../LICENSE). [NOTICE.md](../../NOTICE.md) says the license covers only the project's own code, and that the game, the decomp, and the proprietary DS toolchain are not covered.

**Source:** [NOTICE.md](../../NOTICE.md).

### The decomp is pinned and patched, never edited in place

**Decided:** ground rule 1 in the plan.

**Reason given:** the fork renames types daily, so the pin moves only at phase boundaries. A rename upstream can break patches even when the matching build still passes ([gameplan.md](../../gameplan.md), Phase 0, pitfalls).

**Evidence:** `upstream.lock`, `tools/bootstrap.py`, and the patch directories. See [Patch-Stack](Patch-Stack.md).

**Source:** [gameplan.md](../../gameplan.md), ground rule 1; [docs/BASE.md](../BASE.md).

### The DS build stays byte-matching

**Decided:** ground rule 2 in the plan. Every game patch is guarded by `SDK_PORT_3DS`, or is a GCC-only fix that MWCC never sees.

**Reason given:** the DS build is the reference. A patch that changed DS behaviour would make the port diverge from the game the player has.

**Evidence:** the phase-1 record says the HeartGold ROM built with the SDK patches has SHA1 `4fcded0e…`, with 0 failed checks. See [Patch-Stack](Patch-Stack.md).

**Source:** [gameplan.md](../../gameplan.md), ground rule 2; [docs/progress/phase-1.md](../progress/phase-1.md).

### No game content in the repo, releases or CI artifacts

**Decided:** ground rule 4 in the plan. The builder makes the data pack from the user's own `.nds`.

**Reason given:** the game belongs to its rights holders, and the plan says this follows the Platinum port's policy ([gameplan.md](../../gameplan.md), ground rule 4).

**Evidence:** the `.gitignore` excludes `*.nds`, `*.sav`, `*.pak`, `*.cia`, `mwccarm*/`, `roms/`, `saves/` and `tests/golden/`. The matching workflow has no upload step, on purpose ([.github/workflows/matching.yml](../../.github/workflows/matching.yml)). [NOTICE.md](../../NOTICE.md) lists what is not included.

**Source:** [gameplan.md](../../gameplan.md), ground rule 4; [NOTICE.md](../../NOTICE.md).

### Keep `3ds_port/` byte-identical to Platinum

**Decided:** Phase 1, step 1. The vendored port stays byte-identical at first. HGSS changes go in new files, or in patches meant for upstream (plan).

**Reason given:** the plan's risk register says drift causes "fixes duplicated or lost", and asks for generic fixes to go upstream as PRs ([gameplan.md](../../gameplan.md), risk 7).

**Evidence:** [docs/port-wiring.md](../port-wiring.md) keeps the wiring out of the vendored Makefiles, and suggests a shim under `build/` instead.

**Source:** [gameplan.md](../../gameplan.md), Phase 1 step 1 and risk 7; [CONTRIBUTING.md](../../CONTRIBUTING.md).

### Patch the SDK by area, not a 1:1 rebase of Platinum's series

**Decided:** the plan's Phase 1 step 4 expected 27 NitroSDK and 5 NitroSystem patches rebased from Platinum. The work was redone patch by patch, numbered by area ([docs/progress/phase-1.md](../progress/phase-1.md), "Deviations"; [docs/decisions.md](../decisions.md), 2026-10-31).

**Reason given:** Platinum's patches do not apply to HGSS's differently organised SDK. `git am` cannot apply them. The triage found 4 trivial, 16 moderate, 3 rewrite and 4 not applicable. Each new patch names its Platinum counterpart in its message.

**Source:** [docs/progress/phase-1.md](../progress/phase-1.md); [gameplan.md](../../gameplan.md), Phase 1.

## Part 2: Implementation decisions

Each entry below is a row of [docs/decisions.md](../decisions.md). The reason is the one the row gives, with the repo evidence added where there is some.

### Environment and toolchain

**Private folder at `~/hgss-private`.** The user's MWCC, Nitro SDK and ROMs live in a folder outside the repo, and the install script copies from it into git-ignored paths. The reason in the row is the user's instruction ([tools/install-ds-toolchain.sh](../../tools/install-ds-toolchain.sh) header).

**The supplied SDK is `NitroSDK-4_2-071210-jp`.** The decomp's setup script names the 3.2 SDK. The row says the 4.2 archive was the only one available, and that "the matching build [is] the test". The phase-0 record, however, says the matching run used SDK 3.2 templates ([docs/progress/phase-0.md](../progress/phase-0.md), "Matching build: resolved"). The repo does not settle which SDK the final run used. See the notes at the end of this page.

**The install script copies the whole `mwccarm/` tree.** The reason is that the decomp's `common.mk` uses several compiler versions, not one. `common.mk` sets `2.0/sp2p3` for `lib/NitroSDK` and `lib/MSL_C`, and the row lists others. The license is found through `LM_LICENSE_FILE` ([tools/install-ds-toolchain.sh](../../tools/install-ds-toolchain.sh) header; decomp `common.mk`).

**The SDK's LCF and response templates come from `include/nitro/specfiles/`.** That is where the SDK, and the decomp's setup script, put them ([tools/install-ds-toolchain.sh](../../tools/install-ds-toolchain.sh) header).

**OrbStack on Apple silicon, with amd64 through Rosetta.** The user approved installing it. The phase-0 record says the final matching builds ran with Rosetta on. The QEMU run used distro Wine 9.0, and its timing is recorded as not comparable ([docs/progress/phase-0.md](../progress/phase-0.md), "Runtime findings" and "Matching build: resolved").

**The container is Ubuntu 24.04, not the decomp's 22.04.** devkitARM's pacman package is linked against glibc 2.36, and on 22.04 `arm-none-eabi-gcc` fails with `GLIBC_2.36 not found`. The Dockerfile uses `ubuntu:24.04` ([.devcontainer/Dockerfile](../../.devcontainer/Dockerfile)).

**devkitPro comes from its apt repository, with `dkp-pacman -S 3ds-dev`.** The row says the repository returns a 403 to other user agents, so the key is fetched with `wget -U "dkp apt"`, as the official installer does.

**WineHQ stable 10.0, pinned.** The row says Wine 11.0 aborts in `ntdll/unix/virtual.c` under emulation. The distro's Wine 9.0 runs MWCC but cannot load the Nitro SDK tools, which fail with status `c0000018` ([docs/progress/phase-0.md](../progress/phase-0.md)).

**The install script transcodes the two LCF templates from cp932 to UTF-8.** The SDK 4.2 templates have Shift-JIS comments, and the decomp's Python scripts read UTF-8. The phase-0 record says the transcode is not needed with SDK 3.2, and that the script does nothing harmful with ASCII input ([docs/progress/phase-0.md](../progress/phase-0.md)).

**`CC` is left unset when building the Platinum port.** The row says the port's build honours `CC ?= cc`, and that the container's `CC=gcc-10` fails on unnamed parameters. The vendored files in `3ds_port/` do not show that line, so this page takes the claim from the row without checking it. The survey tool says to use the container's default gcc, as Platinum does ([docs/decisions.md](../decisions.md); [tools/sdk_compile_survey.py](../../tools/sdk_compile_survey.py) docstring).

### Tools and tests

**`savetool` finds slot sizes from the footers.** Slot sizes depend on the compiled struct sizes. Hard-coding them would be a guess, so the tool discovers them, and `--layout` overrides the result ([tools/savetool.py](../../tools/savetool.py) docstring).

**`make test` is the tool tests plus `tests/sdk/`. `port-tests` is separate.** The row says CI must not report green for an unmet exit criterion, and must not report red for a known placeholder. `port-tests` fails until the wiring exists, so it is kept out of `make test` ([Makefile](../../Makefile) header).

**The layout probe is local.** It needs MWCC to regenerate the reference, and needs the decomp's headers ([docs/decisions.md](../decisions.md), 2026-10-31; [tests/layout_probe/run.sh](../../tests/layout_probe/run.sh)).

### SDK patches

**Each asm-to-C hunk uses the same guard, and is checked for DS neutrality.** The row says every asm-to-C hunk uses `#ifdef SDK_PORT_3DS`, the C version, `#else`, the original, and `#endif`. Each one is proven DS-neutral with `gcc -E -P`, and each has a host test with a mutation check. The reason is hard rule 3: MWCC must compile exactly the original text ([docs/decisions.md](../decisions.md), 2026-10-31).

**`OS_SaveContext` and `OS_LoadContext` do not call the CP context functions.** The port's `HandOver` already saves and restores the coprocessor state for each thread. A restore before the switch would overwrite the state of the thread that is leaving. The `HandOver` calls are in [3ds_port/nitro/src/port_thread.c](../../3ds_port/nitro/src/port_thread.c) (at the `CP_SaveContext` and `CPi_RestoreContext` calls). See patch `0501`.

**The channel-0 dummy DMA is skipped under the port.** The DS writes a dummy transfer on channel 0. The row says Platinum's guard gives the same effect, and that without it the dummy would overwrite the real transfer's registers. See patch `0601`, part (b).

**GX FIFO sends use plain stores in patch `0201`.** The row says HGSS's `g3imm.c` stores the opcode with a plain store before it sends the data. If only the send went through the port's 3D translator, the matrix words would reach the packed-command decoder without their opcode. Routing is deferred to a later patch that also changes the caller. See patch `0201`.

**`OS_ReleaseLockID` follows the HGSS ARM shift.** The row says the HGSS assembly is the source of truth. The two behaviours differ only for invalid IDs at or above `0x160`. See patch `0201`.

**`u64` and `s64` are 4-byte aligned under `SDK_PORT_3DS`.** The row cites the measurement: without it, the devkitARM and `-malign-double` builds differ from MWCC in 55 save-struct values ([tools/layout_probe](../../tools/layout_probe/)). See patch `1001` and [Save-Compatibility](Save-Compatibility.md).

## Part 3: Decisions recorded in patch messages

These choices are explained in full inside the patch messages in `patches/`. Each patch's commit message is the record.

- **The card SPI stub reports an IR chip with status `0xAA`** (patch `0701`). The field code compares the second probe against exactly `0xAA`, and any other value starts an anti-piracy task. The message says the wait-cycle settings the caller passes have no effect while no peer answers. Real IR is Phase 9 ([patches/NitroSDK/0701](../../patches/NitroSDK/) message).
- **IRQ requests are cleared bit by bit** (patch `0601`, part (a)). On the DS, `IF` clears the bits written as 1. A plain store would drop every other pending request.
- **The C version of `MATH_QSort` follows the asm's control flow, not a generic quicksort** (patch `0801`). The order of equal elements, and the sequence of comparator calls, depend on the algorithm, so the output order of equal elements would change under a different algorithm.
- **The `CountLeadingZeros` helper returns 32 for zero** (NitroSystem patch `0301`). The DS `clz` instruction gives 32 for zero, and `__builtin_clz(0)` is undefined.
- **`OS_SpinWait` returns at once on the port** (patch `0401`). The DS version is a cycle delay, and nothing on the port needs it.
- **The `int` return type is added to one prototype, on the port only** (patch `1101`). GCC 14 and later reject a prototype with implicit `int`. The patch message says the definition already returns `int`, so the DS text does not need to change.
- **The ARM9 sound flush lives in `SND_command.c` on the port** (patch `1404`). The decomp mixes a 4-byte `sFreeList` pointer (in `SND_command.c`) with a 40-byte `CommandState` (in `snd_flush_command.c`). The DS keeps them in separate objects; the port links both. Moving the flush next to the state is the smallest change that keeps the MWCC text untouched.
- **Sound registers are indexed by the DS address's low 16 bits** (patch `1405`). The mixer reads `SOUNDCNT` at `0x500` and channel 4 at `0x440`; the first port subtracted `0x400`. The volume table is NitroSDK 4.2's real 724-entry table, because its low entries are non-zero and an `attack = 0` envelope depends on them.
- **3D texture pages become host pointers** (patch `1406`). The loader's copy expressions stay the DS ones; only the LCDC page number is translated. Keeping zero entries zero preserves the "no second block" meaning.
- **NitroDWC's asm data is linked even though its functions are stubbed.** Every `lib/NitroDWC/asm` object has no `T` symbols; the lobby and wifi_setup code reads the tables directly. Four libc-state blobs that reference a non-TLS `errno` are excluded.
- **The DS secure-area images are excluded from the PC/3DS build.** They are embedded MWCC asm, no source references them, and the bytes are proprietary firmware.
- **The overlay self-check disables static initializers while it loads.** It tests the loader (`.data` restored, `.bss` cleared), not the game's init code; an application overlay's initializer may touch game state. The `.sinit` pointers are still validated against `FS_LoadOverlayInfo`.
- **The symbol cross-check is an allowlist, not a silent pass.** A missing symbol means a source file was left out; every absent symbol needs an explicit replaced/stubbed reason in `tools/symbol_crosscheck_allowlist.txt`.

## Notes on the record

These are inconsistencies between the repo's own documents, reported here without being fixed.

- **Which SDK made the matching build.** `docs/decisions.md` (2026-10-30) says the supplied archive is the 4.2 Japanese SDK and that it is used. `docs/progress/phase-0.md` records the final matching run with "SDK 3.2" templates, and `tools/matching-build.sh` also says "SDK 3.2 templates".
- **Decision table versus Phase 9.** The plan's table says infrared is "implemented with" `ir:USER`. Phase 9 makes that conditional on a go/no-go test. See the entry above.
- **Plan's Phase 1 rebase.** The plan says to rebase 27 and 5 patches. The record says that was not possible and was replaced by the patch set in `patches/`, but the plan itself is not updated.
