# P1 item 5: `docs/sdk-diff.md` to COMPLETE (agent notes)

## Done (2026-10-31)

Completed `docs/sdk-diff.md` (PARTIAL → COMPLETE) and mirrored the changes in
`docs/wiki/SDK-Differences.md`. Every previously `⏳` section now carries
file/line citations verified against source this session.

Sources used: `build/upstream` was absent, so HGSS citations were verified
against the sibling `~/hgss/pokeheartgold` checkout at the exact pinned commit
(`8cbb4a19`, matches `upstream.lock`). Platinum-side citations come from the
ntrtwl reference headers at `/private/tmp/ntrtwl`, Platinum's NNS patch series
in `~/hgss/pokeplatinum-3ds/patches/NitroSystem/0001–0005`, and the vendored
`3ds_port/`. No repo files outside the three allowed docs were touched; no
`bootstrap.py` run (nothing needed generating).

### §1 Tree and build layout
Noted the resolution without `3ds_port/` edits: `tools/hgss_nitro.py:82-90`
(shim + `SDK=`/`SDK_SRCS=`/`SDK_INCLUDES=`/`ARM7_SOUND_SRCS=` overrides) and
`hgss/compat/include/` on the include path. Remaining work is writing the
compat headers (still only `.gitkeep`), per STATE.md.

### §3 PXI (new §§3.1–3.5)
- Framing: `START 0x02000000` / `END 0x01000000` / cmd bits 8–14 / result bits
  0–7 match; Platinum's reply bit 15 (`0x8000`) is masked off by every HGSS
  parser, so it is accepted and ignored.
- RTC: request `(cmd<<8)&0x7f00` (`rtc_internal.c:31-35`); cmds 16/17/18/39 =
  `0x10/0x11/0x12/0x27`; reply `(cmd<<8)|status` parsed at `rtc_api.c:183-190`;
  shared `0x027FFDE8/EC`; BCD extraction matches `RTCRawDate/Time` layouts.
- TP: cmds 0/1/2/16; request words `0x3000000`, `0x02000100|freq` +
  `0x01010000|vcount`, `0x03000200` (`tp.c:235,260-265,296`); replies
  `END|0x8000|(cmd<<8)|result`, auto-notification bare `0x1000`
  (`tp.c:81-150`); `TpRawPoint` bit-identical to `SPITpData`
  (`x:12,y:12,touch:1,validity:2`); shared `0x027FFFAA`.
- PM: cmds `0x60-0x67`/`0x70-0x74` with multi-word sequences cited
  (`pm.c:198-253`); utility numbers 1/2/3, 4–9, 14 (`pm.c:266-361`);
  backlight bits `reg&8`=top / `reg&4`=bottom (`pm.c:399-404`) match
  `PMIC_CTL_BKLT1=1<<2` / `BKLT2=1<<3`.
- MIC: `0x41` 7-word seq (`mic.c:213-230`), `0x42` single word, `0x51` full
  reply, type-flag values (`mic.c:70-87`); shared `0x027FFF90/94`. Port has
  no mic decoder (only the tag string in `pxi.c:50`); deferred to Phase 7
  `port_mic.c` per gameplan.

### §4 OSSystemWork
Offsets verified by running summation over the header: `real_time_clock`
`0x1E8`, `mic_last_address` `0x390`, `mic_sampling_data` `0x394`,
`touch_panel` `0x3AA`, `command_area` `0x3FE`, total `0x400`; `padding3[2]`
= ntrtwl `ctrdg_*` bytes at `0x39A`.

### §5 ARM7 sound
Entry-point table: seven `N7_SND_*` names → `SND_*` in `sub/lib/src`
(`sound_main.c`, `sound_command.c:57,69,255,270`, `sound_ex_channel.c:71,120`,
`sound_seq_player.c:90`, `sound_shared_work.c:26`); `SNDSharedWork` size
arithmetic re-checked to `0x280`. Rewrite is rename + `int`/`BOOL` line.

### §7 Card backup — closed
`CARD_IdentifyBackup` (`CARD_backup.c:54-72`) runs the core locally then
decides on the PXI reply; port answers IDENTIFY itself, plain-4Mbit-only
(`port_card.c:260-267`). So `_EX` → UNSUPPORTED → fallback to plain type
with identical params. No change needed. (Verified by reading, not run.)

### §9 (new §§9.1–9.2)
- `OSContext` macros identical (SP_SVC 68, CP 72; CPContext 28 → 100 total);
  `OSThread` INFINITY order identical through `systemErrno` (`u32` vs `int`,
  same 4 bytes); `OSThreadInfo` 16 bytes; `OSIrqCallbackInfo {func,enable,arg}`;
  DMA0-3=0-3, TIMER0-3=4-7, table max 22/9-7. Note: no `os/ARM9/irq.h` in HGSS;
  content is in `os/interrupt.h` + `os/common/interrupt_shared.h`.
- NNS table: 0001 covered where applicable (1101 + NNS 0301; no `__alloca` /
  wide literals / `UNPACK_OFFSET_PTR` in HGSS — grep-verified); 0002 N/A (no
  `bufLen/interval` division; shift math at `snd_capture.c:57`); 0003/0004/0005
  counterparts located (`g3d_gecom.c:64-65`, `g3d_sbc.c:462-464`,
  `gfd_vram_transfer_manager.c:244-255`, `g2d_oam.c:224` with bare `0xc0` =
  192) but equivalents not yet written.

## Corrections to the brief worth knowing
- Brief cited `os/ARM9/irq.h`, `os/common/context_shared.h`,
  `os/common/thread_shared.h` as must-reads; the first two do not exist in
  the HGSS tree (used `os/interrupt.h`, `os/context.h` instead).
- Brief's "0x00c0 vs Platinum's 192" for OAM is the same value (`0xC0` =
  192); the real delta is the missing `0x0200` disable bit from 0005.
- TP auto-sampling reply `0x1000` is `(0x10<<8)` with no END bit (port sends
  it at `port_tp.c:45-46`); HGSS parses it at `tp.c:84` without an END check.

## Not done / follow-ups
- Acceptance was by reading + arithmetic only: no `bootstrap.py`, `make test`,
  port build, or DS gate run (out of scope for a docs slice; STATE.md numbers
  untouched). `git status` clean apart from the three docs files.
- `docs/progress/phase-1.md` exit-checklist row still says `sdk-diff.md`
  PARTIAL, and STATE.md "Next actions" #6 still lists sdk-diff completion —
  the lead may want those two lines updated at commit time (out of allowed
  paths for this slice).
