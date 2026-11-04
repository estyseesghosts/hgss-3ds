# S20 — Old 3DS performance implementation plan (revised against working tree @ 2cddfe5 + dirty)

Status: **plan only, no code changed by this session.** Read this before delegating anything.

## 0. What this is

The 12-slice research (s19 + 12 background audits) answered *why* the game runs poorly on o3DS.
Since then the working tree moved: HEAD is still `2cddfe5` (2026-11-02) but `git status`
shows **54 modified + 9 untracked** paths. Much of W0/W1/W2-partial/W4-partial/W5-partial
is **implemented in the tree and uncommitted**. This plan revises the findings against
that tree, then gives the lead agent explicit delegation instructions.

Lead agent model: `scoped-implementer` slices + `scoped-explorer` reviews. Main agent
builds and integrates after every slice. Hardware runs are the owner's; never mark
hardware-gated items green yourself.

## 1. Revised ground truth (checked 2026-11-03, tree 2cddfe5 + dirty)

### 1.1 Already in the tree (do NOT re-implement, validate + commit)

| Item | Where | State |
| --- | --- | --- |
| W0 instruments | `hgss/game/app/main_3ds.c:71-76` `LOG_LINE 512`, `LOG_BUF 32KB`, `setvbuf`, 1/s flush + explicit `FlushLog()` at perf block/terminate/reset/exception; `FRAMETIME` carries `hblank_us/handoff_us`, `hb_keep/dma/irq_us` via `NitroPort_HBlankSplit`; `[perf] mem:` line; texScale/core-share logging | Done in tree, uncommitted. Tests `tests/test_hgss_game.py:1317-1371` pin it. Still needed: one o3DS log proving `total_us >= state_us` on 58/58 samples and `hb_*` present. |
| W1 `-O2` | `tools/hgss_game.py:195-236` `port_cflags()` adds `-O2 -fno-ipa-icf`; app belt-and-braces `:1428-1431` | Done in tree, uncommitted. Image 33.26 → 29.84 MiB, artifact 9.40 → 5.76 MB. Still needed: `make test`, both links, DS-gate re-pin check (any `#ifdef` line shift breaks MSL debug-shape pins). |
| W2 split + texScale | `hgss/game/app/main_3ds.c:120-161` `HEAP_O3DS_GAME 12MB`, `remaining <= HEAP_SMALL_TOTAL` branch; `:2405` `texScale = ini ? ini : (new3ds ? 0 : 1)`; `displaycore=` ini `:2031-2042`; `port_compose_pica.c:900-906` explicit scale | Done in tree. Still needed: hardware `[perf] mem:` high-water to settle 12 MB vs 10 MB. |
| W2 lazy + shadow opt-in | `port_gx3d_pica.c:597-603` NULL images + `classic_images_ready()` `:1177-1194`; `NitroPort_PicaSetShadowRoom` `:556-559`, shell passes `shadows=0` | Done in tree. Saves 1,152 + 1,428 KB. |
| W3 H-blank split (timing only) | `3ds_port/nitro/src/port_os.c` `raise_irq(mask,refresh)`, `GxRefresh` once before walk, `sHBlankKeep/Dma/IrqTicks` + `NitroPort_HBlankSplit` | Done in tree. This is measurement, not the compaction. |
| W4 partial | `3ds_port/nitro/src/port_render2d.c` `EngineWindowsRef`, `SEG_CLEAR`/zero-colour skip, `MergeT` opaque-pair path, OBJ span-limited clear | Done in tree (+163/-13). Still needs hardware A/B + pixel-exact test. |
| W5 partial | `3ds_port/game/app/port_compose_pica.c` `rgb565()` arithmetic, `publish_bands` 1× (`:1337-1458`), `picture_holds`/`same_display_state`/`publish_unchanged` (`:1465-1606`), `hasOver = has3d && overBottom>0` on 2× path, `sFilterRequest`/`filter2d` latch, `texScale` in banner | Done in tree (+368/-12). Still needs commit, unit test, hardware run. No test covers `publish_bands`/`publish_unchanged` today. |
| W6 partial | Display on core 1 `priority+2` (`main_3ds.c:2392-2398`, `:405` log line), `sComposing` handshake (`:1143/1180`), APT hold (`:2132-2140`), core-1 share request 80% (`:453-482`, log `asked 80%, allows 80%`) | Done in tree. Still needs hardware proof (libctru documents core 1 as "a single thread"; tree now puts display + geometry there). `GpuStart` has no core-1 fallback (`:388-394`). |
| 3D batch caches | `port_gx3d_pica.c` `set_combiner_cached` (`:993-1001`), `bind_batch_cached` + `sBatchTex` (`:1669-1697`) | Done in tree. Comment notes display-thread build dominated once live. |

### 1.2 Still open (this plan's work)

1. **W2-remainder (memory):** 1 MB cmd lists → 512 KB (`port_gx3d_pica.c:55,129-138,570`, save 1,536 KB); lower-menu + map 768 KB off on o3DS (`port_compose_pica.c:1040-1043`); right-eye LCD lazy (`:914` still `stereo ? 2 : 1`, save 576 KB); **enforced texture floor** — 4 MB check skipped on last rung (`:842` `small < 2`), ladder keeps weather/menu over cache; `filter=nearest` as launch no-alloc value; effective `texScale` in config (banner now prints it, floor still inferred).
2. **W3-remainder (game thread):** compact `KeepLine` to registers renderer reads (today 2×0x70 B/line = 43 KB copy + 43 KB `memcmp` active scan, `port_os.c:75-81,146-149`); drop per-render 8 KB `ioAll` copy (`port_render2d.c:4079-4082`) and per-frame 8 KB `shifted` copy (`:4514-4520`); keep callback count (effects depend on VCOUNT; weather BG reads result). Acceptance: `hb_*` split shows H-blank <1.5 ms/frame, field effects pixel-identical.
3. **W4-remainder (2D):** skip second `memset(row)` when `t.filled==0`; `SnapshotOver` copy/alpha/second-zero skip when nothing in front; extend opaque-pair merge to `T_LAYER` upper rows; `wide=off` A/B measurement (close §5.2); **lower screen 2D at 30 Hz** (owner decision `decisions.md:136`, `paired_2d_ready :601-624` supports it, no handoff policy implemented — logic stays 60 Hz).
4. **W5-remainder (publish):** port 4-row band diff fully to 1× (in tree, needs test + hardware); over texture as RGBA5551 when nothing translucent (today hard `over16=0` RGBA8 at 1× `:1211,:1399`, double flush + 2-3.7 ms GPU quads); brightness-only publish that skips convert+flush on fade (in tree as `publish_unchanged`, needs test); commit + `NitroPort_PublishDoubledOver` parity test.
5. **W6-remainder (threading):** split drawer A/B, wake display when **A** finishes (today `sDrawFramePending` reaches 0 only after B `main_3ds.c:964-967`); narrow `sSetLock`/`sPresentLock` to reservation/queue (`:3391-3567`, `:3851-3957`); move lower drawer off core 0; keep geometry on core 1, do NOT move engine B there (Platinum: 14-25 fps on 30% share); re-check `sComposing` handshake after any lock change; log accepted APT share (done) and recompute §9 ceiling at 80% (~800 ms/s, not 300).
6. **Audio (no wave yet):** 19-31% core 0 (~4.8 ms/15.6 ms music, 0.36 ms quiet), priority `0x2e` above game/drawer. Distortion fixes (flush, native rate, 6 dB pre-saturation, 256-entry table) done; share untouched. This plan is **measure + minor** only (no mixer redesign): confirm `sSoundMixTicks` excludes `DSP_FlushDataCache`/`ndspChnWaveBufAdd`, add per-block budget log if needed. Any mixer-rate/block change is a separate proposal.
7. **Logging/storage hygiene:** log still on frame path under `sLogLock` (Platinum has dedicated log thread, `3ds_port/game/app/main_3ds.c:1162-1249`, never ported); port `FlushLog` cadence or log thread; wire `FsReadStats`/`NoteGameStall` into HGSS shell so next o3DS log splits game-CPU vs file reads; `pak.c` slot-shrink (`:286-291`) silent on malloc fail — add log line.
8. **Provenance/commit debt:** entire s17/s18/s19 wave + patches 1906-1909 untracked-or-modified; `STATE.md`/s19 table stale (`W5 not started` vs tree has it; `30%` vs log 80%). No build manifest ties any `hgss-private/log-o3ds*.txt` to a source state. `STATE.md:3` says session 19 verified build not run, but logs -2/-3 postdate it.

### 1.3 Corrections to earlier research premises (do not regress)

* `sToRgb565` is 64 KiB, not 128 KB.
* Long hold is `sComposing` (whole `NitroPort_ComposeFrame`), not `sSetLock`/`sPresentLock` (pick/queue only). Game thread takes no compose lock in frame path.
* 0.34% drop figure is New-3DS log-8 pre-fix; o3DS logs are 0 dropped (priority `0x2e` bought it from game/drawer time).
* `port_log.c` has no lock; lock is `hgss/game/app/main_3ds.c:178,253-269` (`d7dd537`).
* L2 has no isolated A/B; 13.2→35.7 Hz = 2.70× < 3× clock ratio.
* `fps=30` halves game clock (diagnostic only). Lower-30Hz means 2D publish only, logic stays 60 Hz.
* `wide=off` is layout choice, not accepted performance knob; keep wide on + `wide=off` launch option for W4 measurement.

## 2. Delegation plan (parallel, disjoint files)

One worktree per implementer (`git worktree add .worktrees/<slice> -b wave/<slice>` from `main`).
In each: `python3 tools/bootstrap.py --clean` once, serially (shares `build/upstream`, `build/hgss-sdk`).

**Ownership (exclusive — no other slice touches these paths):**

| Slice | Objective | Allowed paths | Patch range |
| --- | --- | --- | --- |
| S1-build | Commit W1 cleanly: `-O2` pin, `-fno-ipa-icf` guard, sanitizer branch order, code-size test | `tools/hgss_game.py`, `tools/hgss_nitro.py`, `tests/test_hgss_game.py`, `3ds_port/nitro/Makefile`, `docs/progress/**`, `docs/decisions.md` | none (no game patch) |
| S2-mem | W2-remainder: 512 KB cmd lists, menu/map off on o3DS, right-eye lazy, enforced floor, `filter=nearest` no-alloc, effective texScale log | `3ds_port/game/app/port_gx3d_pica.c` cmd-list hunks only (`:55,:129-149,:546-570`), `3ds_port/game/app/port_compose_pica.c` `compose_init` ladder only (`:821-1080`), `3ds_port/game/app/port_pica.h` if prototype needed | none (port-layer only) |
| S3-2d | W4-remainder pixel-exact: filled-empty skip, SnapshotOver skip, T_LAYER pair path, KeepLine-adjacent `ioAll`/`shifted` read-only skips if safe | `3ds_port/nitro/src/port_render2d.c` only | none |
| S4-pub | W5-remainder: 1× band-diff hardening + test, RGBA5551-over when opaque, fade fast-path test, overBottom skip audit | `3ds_port/game/app/port_compose_pica.c` publish hunks only (`:1188-1700`), `3ds_port/nitro/src/port_upscale.c` | none |
| S5-os | W3-remainder: KeepLine compaction, drop 8 KB copies, keep callback count, hb-split log line | `3ds_port/nitro/src/port_os.c`, `3ds_port/nitro/src/port_timer.c`, `3ds_port/nitro/src/port_dma.c`, `3ds_port/nitro/src/port_thread.c`, `3ds_port/nitro/src/pxi.c`, `3ds_port/nitro/src/svc.c` | NitroSDK 06xx next free |
| S6-shell | W6 + instruments + profiles: drawer A/B split + wake-on-A, lock narrowing, lower-drawer placement, lower-30Hz handoff, `displaycore`/`wide`/`texscale`/`fps` ini, heap-split settle, log-thread-or-flush, FsReadStats wiring; owns `main_3ds.c` exclusively | `hgss/game/app/main_3ds.c`, `hgss/game/app/3ds_*.c`, `hgss/game/app/*.h`, `hgss/compat/src/**`, `patches/pokeheartgold/17xx` (shell-side only, next free above 1763) | 17xx |
| S7-audio-store | Measure-only + hygiene: audio budget log, save/FS log lines, pak-shrink log; no mixer redesign | `3ds_port/nitro/src/port_sound_chip.c`, `3ds_port/nitro/src/port_snd.c`, `3ds_port/nitro/src/port_card.c`, `3ds_port/nitro/src/port_fs.c`, `3ds_port/nitro/src/port_overlay.c`, `3ds_port/nitro/src/port_log.c` | none (or NitroSDK 06xx if S5 unused — coordinate via lead) |

S3 vs S5 both touch H-blank-adjacent copies: boundary is `port_os.c` (S5) vs `port_render2d.c` (S3). S2 vs S4 both touch `port_compose_pica.c`: boundary is init-ladder (`:821-1080`, S2) vs publish (`:1188-1700`, S4). Lead resolves any overlap by assigning the hunk, not by shared editing.

Model guidance: S3, S4, S5, S6 + every review = strong model. S1, S2, S7 = default ok.

## 3. Waves

* **Wave 0 (lead, no delegation):** re-verify §1 (`bootstrap`, `make test`, `make port-tests`, both harness builds, real pack opens). Write `docs/progress/phase-3.md` gap if still open. Check `makerom`/`bannertool`/Azahar/headless-melonDS availability. Message user: golden captures, hardware checklist, `dspfirm.cdc`. Do NOT commit the dirty tree yet.
* **Wave A (parallel):** S1, S3, S4, S5, S7 (no `main_3ds.c`, no overlapping files). Each slice: implement → host tests → PC replay (`boot_smoke` + scripted new-game→field) → report back with diff + test tails.
* **Wave B (parallel after A merges):** S2 (needs A4 publish baseline), S6-shell (needs A results for drawer/lock/30 Hz policy). S6 is serial inside: config → drawer split → locks → 30 Hz → log/FS wiring.
* **Wave C (lead):** full DS-gate HG+SS at `/work/pokeheartgold` (never `/work/patched`), re-pin MSL `*-shape.json` with tool normalizers if any patch shifted lines; `symbol_crosscheck.py` missing 0; `--check-overlays` 129/0; commit per slice; update STATE/phase-4/phase-5/decisions/wiki/Patch-Stack; draft upstream PRs under `docs/outreach/` (do NOT open without user).
* Hardware: lead hands user one staged `.3dsx` + `port.ini` + `Hardware-Testing.md` checklist (New + Old 3DS, frame budget, save round-trip). All hardware exits stay **pending user run**.

## 4. Acceptance matrix (run + quote tails per slice, lead re-runs at merge)

```text
python3 tools/bootstrap.py --clean                  # N patches, 0 fail
make test                                           # exit 0 (355+ Python + SDK suites today)
make port-tests                                     # exit 0 (all suites incl nitrosystem 17122/0)
python3 tools/port_compile_survey.py                # 46/46
python3 tools/hgss_game.py --target host32 --keep-going --out /work/pc -j5   # 1092/1092, 0 unresolved
build/pokeheartgold-pc --check-overlays             # 129 overlays (2 with init), 0 failed
python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc  # missing 0
python3 tools/boot_smoke.py --pak /work/heartgold3ds.pak --manifest /work/hg-data/manifest.json
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/3d -j5      # 1092/1092, 0 unresolved
ls -l build/pokeheartgold.3dsx build/pokesoulsilver.3dsx  # exist, validate_3dsx passes
# DS gate (lead, after any patch wave): clone build/upstream → /work/pokeheartgold, toolchain + mwasmarm_patcher, matching-build HG (4fcded0e…) + SS (f8dc38ea…)
# Perf slices: PC replay scripted new-game → field +ow/battle frames pixel-identical (2D exact, 3D tol 8/255); o3DS log shows [perf] display untruncated, total_us = game+state+other, linear_free high-water, texture converted 0 after first frame, settle waits ~0
```

Slice-specific gates: S3/S4 pixel-identical on field frame + textbox + battle intro + fade + lower menu; S5 field effects unchanged (wave phases, wipes, fades, battle slides, weather BGs); S2 `linearSpaceFree() >= 4 MB` after compose init (target 6-8 MB with W1); S6 composes/s +20-50% at same quality, HOME/lid still settle (`sComposing` handshake re-checked).

## 5. No-gos (stop + ask instead of guessing)

1. No game content in git: no ROM/save/pak/narc/3dsx/cia/golden frame/game-art screenshot/SMDH-or-banner-from-cart/`dspfirm.cdc`. Outputs under `build/` or `/work` only. `tests/golden/` stays ignored.
2. Decomp never edited in place. Every game/SDK change = numbered patch under `patches/`, guarded `SDK_PORT_3DS` with DS text under `#else`, plus `gcc -E -P` identity proof. `bootstrap.py --clean` must still apply all patches.
3. `3ds_port/` divergence is recorded, not silent: every hunk gets a decisions-log row + Patch-Stack entry + upstream-PR draft if generic. Never weaken a test or add a blanket game-symbol stub (MB exception stands, user-approved).
4. Save format frozen: 524,288 B, same offsets/layouts/checksums. `make layout` 0 differences. Any save-touching change needs `savetool.py` check + real-cart round-trip procedure.
5. Enum size `-fno-short-enums`, `signed char`, wrap/strict-aliasing flags stay as Platinum set them. `PM_KEEP_ASSERTS` stays (operands have side effects — Oak's char tasks).
6. Do not change H-blank callback count or VCOUNT semantics; do not move `NitroPort_HBlankLinesOnce` after drawer scan without proving weather-BG offsets still read correctly.
7. Do not raise drawer above game on core 0; do not halve game clock (`fps=30` is diagnostic only); lower-30Hz is publish-only, logic stays 60 Hz.
8. Do not claim `-O3`/`-Os`/LTO/thumb/fast-math/NDEBUG without a hardware A/B (all evaluated + rejected in s19 §8; `-O2 -fno-ipa-icf` is the pinned choice).
9. Do not touch `upstream.lock`/`platinum.lock`. DS gate must run at `/work/pokeheartgold` (absolute-path pin); needs `make -C tools/mwasmarm_patcher` first. Any `#ifdef` line shift → re-run HG+SS gate + re-pin with normalizers, never hand-edit shape JSON.
10. No outward push: upstream PRs drafted under `docs/outreach/`, ask user before opening. No new tool dependency (`makerom`, melonDS, Azahar) without asking. Same failure twice → stop, report, do not try a third variant blind.
11. `gameplan.md` Phase checkboxes are lead-only. Agents never edit `gameplan.md`. One logical change per commit + trailer, tree clean (`git status --short` empty except ignored `build/`, `.worktrees/`).

## 6. Traps (paid for, do not rediscover)

* Mac `build/` vs container `build/` (named volume `hgss-build`); `bootstrap --clean` deletes `build/upstream` — serialize with builds/tests.
* `make test` + harness builds share `build/hgss-sdk` — serialize.
* `3dsxtool` relocation false-positives on `symbol - 0x06800000` folds (patch 1903 pattern: `noinline` base read); `validate_3dsx()` must pass.
* Weak hook binds NULL (RenderScreens lesson) — new hooks need strong-reference test.
* Double-buffer blit: copy both screens every frame, skip only composite.
* Framebuffer rotation already in `port_compose_pica.c` — do not fix twice.
* `fprintf(stderr)` + console paint; exception handler takes no lock; APT single owner (game thread via `NitroPort_OnFrame`); `threadJoin` success ≠ finished (use `sGameThreadAlive`).
* Literal DS addresses (`0x02…/0x04…/0x05…/0x07…`, sound block): grep before assuming logic bug; `tests/test_hw_addresses.py` gates it.
* `-include` resolves via project include path — force absolute paths for system headers.
* `3ds_port/pak/src/pak.c` hard-codes Platinum SHA1s — harness uses build-private copy with HGSS digests; same pattern for any vendored file needing HGSS constants.
* HGSS overlays are unnamed FAT 0-128 (513 entries, 384 FNT names); pak stays ID-indexed; game code `IPKE` (HG) vs `IPGE` (SS).
* ASan: `ASAN_OPTIONS=report_globals=0` for overlay-reload replay (section `memcpy` vs per-global redzones); boot-only phase runs with global checks on.

## 7. Definition of done + final report

Done when: W2 floor ≥4 MB + 0 conversions after first frame; W3 H-blank <1.5 ms + effects unchanged; W4 drawer down with pixel-identical goldens + `wide=off` measured + lower-30Hz landed; W5 publish <35 ms contended / ~0 static, colours/alpha bit-exact; W6 composes/s +20-50%, HOME/lid settle; `make test`/`port-tests`/`layout` green; HG+SS gate SHA1s match; hardware checklist handed to user with exact build + what passing looks like; docs/STATE/decisions/wiki/Patch-Stack updated + committed. Anything needing console/captures stays **pending user run with procedure**, never marked done.

Final report: what was done, exact commands + result tails, what remains + why, decisions/actions needed from user.
