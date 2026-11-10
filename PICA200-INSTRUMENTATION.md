# HGSS-3DS — Instrumentation and evidence specification

**Companion to:** *PICA200 Implementer's Guide for hgss-3ds*, §10 Packet A.  
**Evidence checked:** `estyseesghosts/hgss-3ds` main as inspected 2026-11-09, including `main_3ds.c`, `port_render2d.c`, `port_gx3d.c`, `port_gx3d_pica.c`, `port_compose_pica.c`, `STATE.md`, Half A notes, S20 and Azahar campaign.  
**Status:** an implementation specification, **not** a claim that the additional tracing described below is already implemented.

## A. Verdict: useful instrumentation exists, but it is fragmented

### Already implemented (reuse rather than rewrite)

| Domain | Existing evidence or hooks | Where to start |
|---|---|---|
| Frame and threading | game frame timing, VBlank cadence, drawer CPU wall durations, cores, render/publish work, display loop draws/skips | `hgss/game/app/main_3ds.c`: `PerfLog`, `DrawerMain`, `GpuComposeLoop` |
| Composition | pick/prepare/stage/end CPU phases, queue in-flight/complete, scene/published/dropped/late statistics, GPU fill/list/transfer event callbacks | `3ds_port/game/app/port_compose_pica.c`: `NitroPort_ComposeStats`, `NitroPort_ComposeDbgStats`, `prof_event`, `flight_finish` |
| 2D | per-engine text/affine/OBJ/merge/blend/window/finish/setup ticks; row hits/misses; GPU BG/OBJ take/reject reasons; redraw causes | `3ds_port/nitro/src/port_render2d.c`: `NitroPort_Render2DProfile`; output in `main_3ds.c` |
| Experimental 2D | `partialB` full/band/unchanged outcomes, `partialBChar`, `bandPubB` converted/flushed bytes, diagnostic mismatches | same; options are not proof of production benefit |
| Geometry | scene-flow queued/completed/skipped, worker busy, producer wait, FIFO tagging, bounds cache, hidden-run census, clipped/dropped vertices | `3ds_port/nitro/src/port_gx3d.c` and Half A A0 notes |
| PICA | draw count, command-list peak/cuts, batch preparation, optional index compaction, flush bytes, textures and stage markers | `3ds_port/game/app/port_gx3d_pica.c` |
| Texture/memory | `HGSS_TEXTURE_PROFILE` lookups/hashes/conversions/evictions/alloc/wait/decode/flush; cache and linear-heap statistics | `port_gx3d_pica.c`, `main_3ds.c` |
| Audio/interrupts | mixer/drop counts, HBlank handler/deferred-switch accounting, timer and DMA counters | `main_3ds.c`, `port_os.c`, audio modules |

**Crucial caveat:** many of the deeper flags are experimental, default-off, build-dependent or represented only in dated local work; see `STATE.md`. Do not treat every counter in the docs as enabled in the release binary.

### Critical missing capabilities

1. A **single frame lineage** across game tick → G3 packets → geometry scene → snapshot → drawer → upload → GPU submission/completion → LCD presentation.
2. A **low-overhead timestamped trace** of the critical path, including intervals spent **waiting on another thread**, not merely averages per worker.
3. **GPU-stage accounting** that distinguishes 3D drawing from 2D quads, clear/fill, 2× smoothing and transfers, with explicit limitations of available GSP timing.
4. A **per-frame resource/correctness ledger**, recording who wrote a GPU-visible allocation, when it was flushed, when the GPU began reading it, and when it became reusable.
5. A synchronized **one-frame rendering capture** sufficient to reproduce a mismatch offline (source VRAM/OAM/registers, snapshots, decoded textures, geometry, draw state, and completed output).
6. Automated **invariant alerts**, triggered trace preservation, and performance reports that account for overhead and rejected fast paths.

**Goal:** every slow or incorrect frame must answer “what waited for what, what work was done, what changed, and why was the fallback chosen?” without changing the behavior under investigation.

## B. Three instrumentation modes, not one noisy build

Keep the renderer algorithms and scheduling identical unless a diagnostic explicitly requires a differential comparison.

| Mode | Production behavior | Cost policy | Purpose |
|---|---|---|---|
| `OFF` / baseline | Existing functional counters only; no timestamp event ring | Bare minimum; match normal build | Ground truth for speed and stability |
| `PROFILE` / always-on diagnostic | Cumulative per-domain counters + coarse wall timings; sampled per-frame summary, e.g. one in 60 | Aim for <2% slowdown; *measure*, never assume | Locate recurring CPU, GPU, queue and upload bottlenecks |
| `TRACE` / triggered capture | In-memory event ring, detailed stage timestamps and bounded frame capture for chosen scene/range | May perturb timing; explicitly measured and reported | Explain a concrete hitch, race, stale image or wrong pixel |

Add independent `VERIFY` / differential correctness mode rather than disguising it as a performance build; it may render both paths or hash large buffers and must **never** be used as performance evidence.

**Do not compile trace writes into hot loops when `OFF`** if measurable overhead remains. A runtime toggle is useful for selecting trace windows, but compile-time gating is needed for clean baselines. One can keep cheap error counters in `PROFILE`.

All formats need `schema_version`, `git_commit`, `dirty_worktree`/build ID, `artifact_sha256`, compiler flags, optimization flags, HG/SS selector, port.ini, model/CPU clock mode, core topology, stereo and smooth3d modes, texture scale, screen swap, run ID, save hash and input-script hash.

### Cost controls

- Never call `fprintf`, `sprintf`, `fflush`, filesystem APIs or the shared `NitroPort_Log` from an inner raster/triangle loop or GSP interrupt callback.
- Do not heap-allocate, take a blocking global logger mutex, call costly formatting functions, or wait for SD I/O from a game, drawer, geometry, GPU, audio, VBlank or GSP callback.
- Use **bounded per-thread single-producer ring buffers** (one writer each), fixed-size binary events, explicit overflow counts and an asynchronous low-priority drain task. Merging is by monotonic timestamps during offline analysis.
- No producer should wait when its trace ring is full: increment `lost_events`, preserve the fault trigger, and stop recording or drop the oldest event by a documented policy. Avoid racing over the ring during a crash dump.
- Reserve crash/exit trace handling and disk flushing separately from normal logging. Flush during a safe thread context or on a controlled stop, not inside an exception/GSP callback. If an exception prevents a clean dump, a retained crash marker and last periodically flushed trace are still useful.
- Add a small pre-trigger buffer (e.g. last 2–5 seconds at sampled event rates) and a short post-trigger segment; cap SD-space use and record any losses. Pick sizes based on measured event volume and Old 3DS memory, not arbitrary megabyte allocations.
- Profile telemetry itself: event writes/s, bytes/s, overflows, drain stalls, flush stalls, high-water, timestamp-call time and percentage of frame budget. Run `OFF` vs `PROFILE` vs `TRACE` against *the same executable configuration* and frozen test; for clean OFF comparison, use a separately built binary with identical optimization flags.

## C. Unified identity: **four clocks, one lineage**

**Do not use a shared `frame` integer without saying what it means.** The game can tick at 60 Hz while the renderer submits and presents fewer unique images. Define:

- `tick_id`: emulated DS logic/VBlank progression.
- `scene_id`: monotonically assigned G3 `SWAP_BUFFERS` scene; preserve existing serial for lookup.
- `snapshot_id`: immutable CPU register/VRAM/OAM/PLTT state handed to a drawer, with source `tick_id`.
- `layer_set_id`: GPU-ready 2D publication; include engine and sequence.
- `submission_id`: unique GPU command-list/transfer submission, with list/queue index, content set, and buffer IDs.
- `presentation_id`: actual LCD buffer swap, separately for top and bottom; link presented `submission_id` and source lineage.

When one source is reused, record a **reuse event pointing at the original ID**. When one scene is dropped or superseded, record its explicit lifecycle outcome, not just a counter increment.

The timeline must show:

`tick N → G3 scene S → snapshot A/B → layer publication P → draw submission G → completion C → top/bottom present V`

and whether the actual LCD re-showed an existing buffer. Correlation is more valuable than one extra decimal place in phase timings.

### Event record

A compact illustrative binary event (not a promised drop-in C implementation):

```c
typedef struct TraceEvent {
    uint64_t tick;       // svcGetSystemTick(), raw, monotonic
    uint32_t lineage;    // the ID appropriate for this event kind
    uint32_t sequence;   // per-thread sequence; detect loss/reorder
    uint16_t kind;       // stable enum, schema-versioned
    uint8_t  thread;     // assigned logical thread ID
    uint8_t  core;       // core observed when emitted
    uint32_t arg0;       // reason/phase/buffer/byte count
    uint32_t arg1;       // auxiliary ID/size/status
    uint32_t arg2;       // flags or reserved
} TraceEvent;            // 32 bytes
```

Record **both** raw timestamps and the timer conversion rule used by the reporter. `svcGetSystemTick` measures elapsed time, including preemption and waiting. **It is not CPU execution time.** Do not add times from simultaneously running threads and call them total frame time.

For high-rate categories, emit **one aggregate event per stage or 60-frame interval**, not one per vertex or scanline. Count granular operations with inexpensive per-thread accumulators.

Use `{stage.begin, stage.end}` only for bounded stages whose call-count and cost justify it; otherwise use one completion event with duration and count. On overflows and missed stage ends, the analyzer must mark spans *incomplete*, not infer a zero-length span.

### Always capture wait causes

One “geometry stage 24 ms” sample can mean 8 ms running + 16 ms descheduled. Each blocking point needs:

- wait begin/end timestamps and reason (`MAILBOX_EMPTY`, `GPU_IN_FLIGHT`, `BUFFER_HELD`, `SET_LOCK`, `QUEUE_FULL`, `VBLANK`, `CARD_IO`, `AUDIO`, `THROTTLE`, `SHUTDOWN`);
- owner or resource ID, queue depth, in-flight count and thread/core;
- whether it waited, was deferred, overwrote obsolete work, or returned without work.

Produce thread/Core Gantt lanes from timestamps; optionally track voluntary wake/wait states. `core idle` sampling is useful context but **not a definitive per-thread CPU-utilization measurement**.

## D. Minimum stage breakdown that will actually locate work

Instrument the following boundaries without rewiring renderer behavior.

| Pipeline | Start/end or counters | What it will distinguish |
|---|---|---|
| Game | game logic; synthetic HBlank/interrupt walk; DMA; input; audio pump | legitimate game cost vs scheduler overhead |
| G3 enqueue | individual packets vs direct single-word commands; ring occupancy/waits; tagged/untagged provenance | front-end overhead vs FIFO fragmentation |
| Geometry | decode op count; matrix ops applied/skipped; lighting; bounds lookup; hidden scan; transform; clip; sort; publish | work repeated despite cached model; visibility vs arithmetic |
| Geometry output | triangles input/visible/clipped/dropped; run/block ID; material/texture ID | whether “fewer vertices” actually saved work |
| 2D state | `TakeState`, source generation and dirty reason; BG/OBJ/palette register signatures; full/partial/unchanged decision | repeated decoding, stale hashes, change detector misses |
| 2D CPU | per-engine BG text/affine/OBJ/window/merge/blend/finish/setup; full vs dirty scanlines; pixel count | which CPU pass actually dominates |
| GPU 2D takeover | eligible/taken/rejected **by reason**, BG slot, map kind, blend/window/mosaic, fallback pixels | why GPU path stays dormant |
| Texture | lookup/size/format; hash/decode/swizzle/allocate/evict; uploads, flush calls and bytes, bytes changed; per-resource generation | expensive churn vs real updates |
| PICA scene preparation | `BuildBatches`, depth normalization, index-eligibility, index probe/compaction, bytes emitted, state changes | GPU feed vs CPU transform cost |
| GPU command | cmd words, draw calls by type (3D, BG, OBJ, outline, shadow, stereo), command splits/cuts, submission and queue wait | too many tiny draws or buffer exhaustion |
| GPU engine | P3D command-list completion; PSC0/PSC1 fills; PPF transfers; optional GSP perf-log differences; 2× smoothing and target count | GPU work vs submission vs conversion/transfer |
| Compositor | set selection, unavailability, prepare/stage/end, CPU waits, buffer count, retained vs repainted, submitted vs skipped | who starves the display |
| Presentation | completed-to-present latency, age of source tick, duplicate frames, scene drops, late/skip reasons, **top and bottom separately** | actual user-visible cadence |
| Memory | heap/linear/VRAM allocation by purpose, peak, fragmentation/failures, held/in-flight resources | high-water and hidden copies |
| Output correctness | bounded frame hash, depth/stencil status where available; first mismatch location; state snapshot ID | wrong pixels tied to exact producer |

For each counter include **attempted, succeeded, rejected and fallback** totals. A zero `take` can be a feature that never executes, not a correct optimization. A nonzero “cache hit” can be dominated by hits that only save a few instructions.

**Do not overinterpret** `P3D`, `PPF` and `PSC0/PSC1`: these are GX hardware event categories, not a complete seven-stage PICA execution profiler. Event callback arrival times are not exact start/end of independent shader stages. The existing custom frame queue does **not** necessarily use `C3D_FrameBegin/End`; `C3D_GetDrawingTime` and `C3D_GetProcessingTime` are useful for conventional citro3d examples, but are not automatically the correct measurements of this compositor. Consult the actual queue lifecycle before instrumenting it.

### Hardware-facing optional probes

- libctru's `gspSetEventCallback` exposes P3D, PPF and PSC completion events; record **queue/submission IDs** alongside event timestamps, and handle cases where multiple operations fall within one rendered frame.
- On supported libctru/GSP versions, investigate `GSPGPU_SetPerfLogMode` + `GSPGPU_GetPerfLog` for cumulative P3D/PPF/PSC/DMA durations. Check availability and required permissions on target firmware; **difference cumulative counters** between known boundaries, discard initial invalid readings and account for counter wrap and overlapping operations. Do not assume measurements are single draw calls.
- 3dbrew documents **eight GPU hotspot counters** tentatively corresponding to array read, vertex, setup, raster, texture, lighting, TEV and framebuffer work. Their meanings are *educated guesses* and access may be restricted; they belong in a **separate research-only probe**, disabled in normal builds. Do not promise per-pixel stage occupancy from these counters, or write undocumented GPU registers on ordinary users' consoles.
- `svcGetSystemTick` is appropriate for elapsed stage timestamps; record the runtime conversion consistently and calibrate instrumentation overhead. Probe API failure explicitly rather than silently writing zero GPU times.

Verified pointers:
- [devkitPro citro3d `renderqueue.c`](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c)
- [libctru/GSP GPU performance log notes](https://rust3ds.github.io/ctru-rs/crates/ctru_sys/fn.GSPGPU_GetPerfLog.html)
- [3dbrew GPU external registers: hotspot profiling](https://www.3dbrew.org/wiki/GPU/External_Registers)
- [libctru GSP event definitions](https://rust3ds.github.io/ctru-rs/crates/ctru_sys/index.html)

## E. State captures: make wrong pixels reproducible without guessing

Add **one-shot frame capture**, requested for a specific input tick/scene or triggered on an invariant mismatch. Capture all inputs from the **same immutable snapshot**, including:

1. Game/engine state identifiers: tick, overlay/battle state, top/bottom engine mapping, VRAM-bank mappings, video mode, full register snapshot and all HBlank line effects for that frame.
2. Actual source bytes relevant to drawing: BG VRAM, OBJ VRAM, palettes (including extended palettes), OAM, LCDC banks, display-capture inputs if used. Record exact source region sizes, mapping generations, and CRCs. Do not reuse earlier frame data.
3. Which state the renderer **actually consumed**: `TakeState` generation, `Render2DSplit` inputs/outputs, geometry `scene_id`, layer under/over published buffers and selected GPU BG/OBJ slots, validity keys, dirty bands, rejected reasons.
4. GPU resources **as submitted**: converted/swizzled texture payload bytes (and hash), their exact dimensions/format/filter/wrap and addresses or stable allocation IDs; vertex/index payloads; list of draw calls; shader ID/version and uniform values; TEV, alpha/depth/stencil/blend, scissor/viewport; polygon/texture IDs.
5. Completed GPU outputs: target dimensions/format, colour buffer (after completion/safe transfer), and optional depth/stencil dump if supported; actual chosen LCD buffer and presented image ID.
6. Source and destination CRCs per stage, optional first differing tile/scanline/pixel coordinates, and a safe fallback flag.

**Capture ordering:** freeze or copy the CPU snapshot once at the existing handoff boundary. Hold/reference the exact GPU resources by their in-flight ownership ID until the GPU signals completion; then copy completed buffers. **Never** read live VRAM, upload textures, and framebuffer in three different frames and call it “the same frame.” Snapshot mode may deliberately pause the pipeline to make this feasible; it is a correctness probe, not a speed benchmark.

**Privacy/content:** captures from the user's legally supplied game data can contain copyrighted assets and potentially save contents. Store them locally, choose a minimal reproducible subset, and avoid committing their bytes to the public repository. Commit the capture reader/schema and synthetic fixtures instead.

### Known bug recipes

**Mirrored battle dialogue:** trigger at command phase, collect the full BG/OBJ/OAM/palette/LCDC state, source tile bytes, atlas bytes *after* swizzle, UVs, TEV state and completed target for the same scene; compare successive text rows. Keep patch 1910's battle-active GPU-BG veto until the hardware capture is explained and the fast path passes.

**Stale/flickering pixels:** correlate `snapshot_id` → `layer_set_id` → GPU allocation generation → completion → `presentation_id`; print which display buffer was swapped, whether it had a complete picture and which previous display presented it. Check cache flush and in-flight overwrite hazards before blaming texture filtering.

**Geometry copies / halos:** correlate draw ranges, material/stencil/texture state and source G3 command lists. Validate every draw first/count and its membership in exactly one batch. Do not “fix” the symptom by changing DS sprite/OAM code: prior duplicated NPCs were 3D.

**Missing Pokémon:** trace scene triangles before and after clip, texture cache failure, opaque vs translucent pass and depth/stencil rejection; include a known battle Poképic fixture and known reference triangle count.

## F. Automated invariants and alert-triggered trace windows

Add *non-fatal* detectors in `PROFILE`; record the reason and enable a short trace window only when allowed/safe. In `VERIFY`, assert or dump immediately:

- `published_scene <= produced_scene` and every scene outcome is accounted for once: drawn, superseded or intentionally dropped.
- No GPU read begins before source dirty ranges are flushed, and no in-flight allocation is overwritten/reused.
- `submission_id` completes before its LCD buffer becomes eligible for presentation.
- Buffer reuse happens only after all consumers finish; no layer set is published while its drawer writes it.
- CPU snapshots and BG/OBJ keys use **the same generation** as the underlying bytes.
- GPU BG/OBJ fast-path selection excludes known unsupported blend/window/mosaic modes.
- A counted GPU fast-path `take` means a quad was actually submitted; a fallback has a reason code.
- Index-buffer count and draw ranges fit their allocations; draw counts satisfy primitive constraints; finite shader uniforms.
- Presentation repeats are labeled repeat, not new composition; dropped-scene counters match lineage reconciliation.
- Audio drop, failed GPU allocation, command-list cut, timeouts and trace loss are always visible.

Set **trigger types**: 100 ms+ stage hitch; repeated VBlank misses; queue unexpectedly full; memory low-water crossing; copy/CRC mismatch; GPU timeout; missing heartbeat; incorrect display generation; texture cache invalidation mismatch. Use configurable thresholds, not hard-coded conclusions; a single cold load is not an ongoing steady-state bottleneck.

When triggered, collect the preceding buffered events and the next bounded window, then emit a compact `ALERT` with IDs and resource state. Avoid continuously dumping giant binary surfaces.

## G. Host tools and report format

Write narrow, testable tooling under `tools/`:

- `tools/trace_decode.py`: decode binary versioned events, verify monotonic sequence/overflows, join related IDs, output CSV + Chrome/Perfetto-compatible trace JSON. A Gantt view should show game, geometry, drawers, display, audio, VBlank/GSP and SD writer as separate lanes.
- `tools/trace_report.py`: one-page causal report: game ticks/s, unique top/bottom presents/s, presentation age distribution, stage p50/p95/p99/max, queue waits by reason, critical-path chain for each hitch, CPU vs GPU vs copy/publish, memory and correctness alerts.
- `tools/trace_diff.py`: compare **matched run manifests**. Refuse if build ID, hardware, options, save/script hash, scene interval, trace schema or clock mode is absent or different, except for explicitly whitelisted changed experiment flags. Show confidence intervals across repeated runs; never label an Azahar speed win an Old 3DS hardware win.
- `tools/capture_frame.py`: validate and inspect a one-shot frame bundle, CRCs and attachment references; compare CPU reference layers with submitted and completed GPU stages, giving the earliest stage where pixels diverged.
- `tests/fixtures/trace_*`: synthetic controlled unit tests for event loss, wraparound, out-of-order completion, multiple GPU operations per frame, stale resource generations, duplicate presentations, clock conversion, and corrupted capture files.

Output example (**invented format, not real measured results**):

```text
RUN id=o3ds-goldenrod-001 commit=... exe_sha256=... model=O3DS flags=...
WINDOW ticks=600..1200 elapsed_ms=... scene=Goldenrod
TICKS 600/600 expected; top UNIQUE_PRESENT 244; bottom UNIQUE_PRESENT 132
CRITICAL_PATH top_frame=812:
  G3 packets enqueued tick=810, geometry queued scene=411
  GEOMETRY queued->done 28.4ms (decode 6.4; transform/clip 15.9; waiting 5.1)
  2D snapshot=811: BG redraw 9.2ms; published set=682
  GPU submission=513: P3D/PPF aggregate 3.1ms; queue wait=1.6ms
  LCD presentation=519: source_age=3 ticks; previous image repeated twice
CAUSE ranking: geometry service time > BG redraw > queue wait
INVARIANTS mismatch=0 cmd_cuts=0 lost_trace_events=0
OVERHEAD profile-vs-off +1.1% median wall (measured on matched experiment)
```

Distinguish **service time** (work with no known waits), **elapsed wall time** (including descheduling), **queue delay**, **resource wait** and **display age**. The term “critical path” must follow actual IDs/timestamps, not simply sum the largest stages from unrelated frames.

### Minimum frozen test corpus

- Goldenrod moving field, 600+ DS ticks, same path/weather/time-of-day.
- Static field with no input: proves unchanged-path reuse.
- Field camera scroll with static map data: isolates texture-/geometry-cache reuse.
- Animated lower menu / Pokégear: OAM and text dirty ranges.
- Battle entry → command → dialogue → return: text, transparency, scene synchronization.
- Intro/cutscene/Ho-oh: alpha, camera clipping, custom effects.
- Long-loop run with map transitions, HOME/START+SELECT teardown, audio and memory high-water.

For each scene: one **uninstrumented** baseline and one **profiled** comparison, then targeted `TRACE` or `VERIFY` for unexplained cases. On every hardware claim, at least three matched A/B runs where feasible; show variation rather than one lucky result.

## H. Implementation assignments (strict agent boundaries)

### Packet I1 — Trace skeleton and run manifest (lead-owned integration)

**Allowed:** new `hgss/diagnostics/trace.{h,c}`, new host `tools/trace_*`, one narrow hook from `main_3ds.c`; don't change renderer.  
**Deliver:** stable 32-byte event schema, timestamp conversion, per-thread buffers, bounded logger, overflow accounting, run manifest, trace decoder, synthetic tests, OFF/PROFILE/TRACE comparison.  
**Pass:** decoder reconstructs artificial timeline and losses; initial real hardware run has zero missing/lost records at chosen sampling; compile sizes and performance deltas recorded.  
**Avoid:** global mutex in fast paths, SD writes on GPU/audio/core IRQ, giant inline logger, introducing new `__aeabi_*` math in a hot path.

### Packet I2 — End-to-end frame lineage (requires I1)

**Allowed:** narrowly instrumented boundaries in `main_3ds.c`, `port_gx3d.c` and compositor, via hooks only; avoid altering mailboxes.  
**Deliver:** tick/scene/snapshot/set/submission/presentation IDs, queue wait reasons, scene lifecycle reconciliation, top/bottom presentation age; timeline export.  
**Pass:** all completed/dropped/retained scenes reconcile for title/Goldenrod/battle; no lineage gap or untracked LCD repeat; no correctness diff.

### Packet I3 — Stage and resource ledger (requires I1; serialize with renderer optimization work)

**Allowed:** hooks to existing profiler and cache counters in `port_render2d.c`, `port_gx3d_pica.c`, `port_compose_pica.c`; separate small resource map module if needed.  
**Deliver:** per-stage histograms, resource generations, flush/copy bytes and reasons, command-list ownership, GPU event attribution.  
**Pass:** fixture scenarios force each reason; request/completion invariants survive delayed GPU completion; hardware `OFF` vs `PROFILE` A/B stays within established overhead budget.

### Packet I4 — Deterministic capture and differential tool (requires I1/I2, correctness mode only)

**Allowed:** new capture module, snapshot/copy hooks at existing handoffs, `tools/capture_frame.py`, synthetic fixtures.  
**Deliver:** exact-format one-shot frame bundle with immutable input, submitted GPU state/resources and completed image, frame diff with *earliest divergent stage*.  
**Pass:** deliberate one-byte tile/palette/UV mutation detected at the correct stage; battle mirror repro captured with synchronized IDs; no leaked frame ownership or hangs.

### Packet I5 — Hardware study (research-only, independent from performance claims)

**Allowed:** tiny isolated diagnostic executable/tests, not production rendering path.  
**Deliver:** proof whether GSP perf logs and PICA hotspot counters are accessible on O3DS/N3DS, calibration vs simple known GPU loads, failure/permission matrix.  
**Pass:** raw values, documented conversion, repeated controlled stimulus and OFF-run control; mark uncertain counters **unknown**.  
**Avoid:** unverified raw register access in the game, kernel patches as baseline, treating P3D callback latency as isolated TEV/texture throughput.

## I. Mandatory report template

Every performance or graphics-debug agent report must contain:

1. **Reproduction:** exact git SHA + dirty diff manifest; executable hash; model and clock; config; save/script hashes; scene/tick bounds.
2. **Hypothesis:** one causal claim, prediction and a clear falsification test.
3. **Before:** full run manifest; real hardware vs emulator distinguished; `OFF` and, if used, profiler overhead; stage and presentation metrics.
4. **Change:** narrow file list; intended affected stage; no unrelated refactors.
5. **After:** matched A/B table, median/percentiles/tail, scene IDs of outliers, unchanged correctness/capture hashes; actual CPU/GPU allocation/command changes.
6. **Conclusion:** retained/reverted; objective reason; what the evidence **does not** establish.
7. **Open issues:** remaining unobserved stage, invalidated assumptions, missing hardware proof, next smallest experiment.

**Stop conditions:** any telemetry accounting failure, untracked render snapshot, missing/lost trace without disclosure, uncalibrated hardware counter, hidden output mismatch, new audio drop, GPU hang, or “speed win” only seen with incompatible benchmark settings. The measurement infrastructure itself must not become another source of ghosts.

### References to preserve in the repository

- [HGSS `main_3ds.c` instrumentation](https://github.com/estyseesghosts/hgss-3ds/blob/main/hgss/game/app/main_3ds.c)
- [HGSS `port_compose_pica.c` GPU event callbacks](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_compose_pica.c)
- [HGSS `port_gx3d_pica.c` command and texture stats](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_gx3d_pica.c)
- [HGSS `port_render2d.c` counters and GPU fast-path reasons](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_render2d.c)
- [Half A hardware and probe notes](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-session-2026-11-06.md)
- [Battle mirror forensic notes](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md)
- [GSPGPU performance log caution](https://rust3ds.github.io/ctru-rs/crates/ctru_sys/fn.GSPGPU_GetPerfLog.html)
- [3dbrew hotspot counters — speculative stage labels](https://www.3dbrew.org/wiki/GPU/External_Registers)

**The central deliverable is not a longer text log. It is a reproducible timeline that identifies the exact bottleneck or first incorrect render stage, together with proof that observing the problem did not substantially change it.**
