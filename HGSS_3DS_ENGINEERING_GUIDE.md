# HGSS-3DS: Engineering Principles for a Fast, Correct Native Port

**Standing technical guide for developers and coding agents**  
**Repository:** `estyseesghosts/hgss-3ds`  
**Reviewed against repository main at `078beff6` (2026-11-06)**  
**Scope:** architectural decisions, PICA200 rendering, Old 3DS performance engineering, evidence, diagnostics, correctness, and agent conduct.  
**This is not a development schedule, recovery plan, or list of implementation waves.**

> **Central rule:** Preserve the existing working port while replacing repeated CPU work with persistent, hardware-friendly representations. Passing tests and producing logs are not substitutes for a measured improvement on a real Old 3DS.

## 1. Purpose and engineering standard

This project already does something difficult: HeartGold/SoulSilver game code runs natively on 3DS, reaches actual gameplay, draws real 3D through PICA200, handles two screens and audio, and supports movement on a launch-model 3DS. Do not rewrite or dismiss the functioning game/SDK port merely because the rendering backend is slow.

The remaining performance problem is largely a **translation-architecture problem**. The Nintendo DS has specialized 2D display engines and a fixed-function 3D engine. The 3DS has a *different* programmable-vertex/fixed-fragment GPU, with different texture, blending, memory and command-submission rules. A port must reinterpret the DS's drawing instructions; it cannot simply forward them as if both consoles had the same graphics hardware.

The codebase has nevertheless spent too much effort accelerating software emulation and qualifying narrow GPU fast paths. Those optimizations were sometimes worthwhile, but they have a ceiling. An optimization that makes a software scanline 20% cheaper is irrelevant to the long-term architecture if that scanline should not be computed on the CPU in the first place.

Working standards:

- **Make costly work disappear**, rather than only making it cheaper. Prefer persistent textures plus changing scroll coordinates over reconstructing and uploading a fully scrolled framebuffer; prefer GPU vertex transforms where their semantics and cost make sense over repeatedly transforming whole vertex streams on the ARM11.
- **Do not confuse the GPU compositor with GPU rendering.** Uploading CPU-produced 2D pictures to textured quads is still software rendering followed by GPU presentation.
- **Keep a correct reference path.** The existing CPU renderer is valuable as an oracle for pixel and state comparisons. It must stop being the routine main path for ordinary gameplay where a proven GPU alternative exists.
- **Preserve DS game behavior.** Presentation pacing is distinct from the game's 60-Hz simulation/callback assumptions. Setting `fps=30` to get an apparent stable display is not a valid optimization: this port's setting slows the game clock.
- **Old 3DS is the performance authority.** Azahar is for functional reproduction, not a stand-in for the original console's CPU contention, cache, GPU transfer and thread behavior. New 3DS is a separate performance profile, not an acceptable proxy.
- **Correctness remains mandatory**, but a synthetically correct optimization is not a performance result. Both must be demonstrated, independently.

Performance figures in this guide are **historical measurements, not a claim about the current HEAD's FPS**. Results must be tagged to the tested executable and configuration.

## 2. The actual history: what worked and what keeps failing

### 2.1 Meaningful achievements

The Session 20 hardware investigation reported approximately:

| Measurement | Earlier hardware state | After major corrections |
|---|---:|---:|
| Old 3DS field compositions/s | 0.3–3.6 | 10–14 |
| Both-screen drawer wall time | 2,000–4,500 ms/frame | 55–81 ms/frame |
| Old 3DS game-frame cost | up to 41 ms | 2.3–9.2 ms |
| 3D geometry time | 32.8 ms/scene | 19.9–24.7 ms/scene |
| 3D texture room | 408 KiB | roughly 7.6–8.1 MiB |

The causes were concrete: the 1,092 game objects were initially compiled at `-O0`; the heap split chose the wrong policy on an Old 3DS; H-blank emulation performed around 192 expensive thread switches per frame; and geometry used software-emulated double precision where float sufficed. Fixing these **removed broad categories of overhead**. These should be the model for new work.

The more demanding **frozen Goldenrod moving benchmark** subsequently remained around **8.25 compositions/s in hardware run 27**, even though its drawer/render numbers improved somewhat. Do not compare that figure with the earlier 10–14 figure as if the scene and benchmark were identical. In Goldenrod, geometry, drawing, submission and CPU-core contention interact. See [Session 20](docs/progress/agent-notes/s20-performance-session.md) and [W3/W4 hardware notes](docs/progress/agent-notes/o3ds-w3-w4-local-validation.md).

### 2.2 Recurrent failure patterns

| Failure mode | Repository evidence | Engineering correction |
|---|---|---|
| **An experiment is not actually compiled into the running code.** | Half A's GPU-geometry flags reached app C flags, but not `libnitro.a`; prior `-gxhot` smoke tests used unchanged library code. Dead-by-default paths had latent compile errors. | Prove effective flags, object rebuild, linked symbols/sections and runtime path activation in the **exact** tested artifact. |
| **Synthetic fixtures misrepresent gameplay.** | A GPU background shortcut assumed two visible text BGs; Goldenrod had a third enabled but transparent front BG. | Capture and replay real DS register/VRAM/OAM state from the problematic scene; derive fixtures from it. |
| **A test enforces an incorrect assumption.** | Battle GPU nomination relied on a nonexistent overlay name, and a test validated the dead check. | Audit the premise against game state and actual overlay data; tests may encode bugs. |
| **A GPU optimization does additional CPU work.** | Hardware logs 35/36 showed indexed vertex preparation adding ~1.3 seconds per 120 preparations, with no vertex-byte reduction in that scene. | Require a meaningful primitive/data/CPU-work reduction before claiming a draw-method improvement. |
| **An optimization cannot engage in the intended scene.** | `partialB` was bit-exact in fixtures but produced zero in-game band takes until further character-change handling; logs diagnosed why it fell back. | Instrument the exact admission gate once, exercise it on the frozen scene, and stop if that case remains ineligible. |
| **Timing is not causal.** | Prior profiles included truncated lines, incorrect core-share assumptions, stale H-blank timing fields, and wall-clock time containing preemption. | State the exact event/time measured, separate active CPU work from waiting, and pair every result with a controlled A/B. |
| **Correctness proof is presented as a speedup.** | Large matrix arithmetic suites, reduced test scanlines and Azahar smoke runs were at times the strongest evidence for a change. | Keep functional validation, CPU work elimination and hardware presentation improvement as **three distinct claims**. |
| **A workaround becomes a permanent architecture.** | Battle dialogue mirrored on GPU quads; the game-state veto restores correct text but leaves the actual GPU defect unresolved. | Narrow fallbacks are appropriate for safety, but carry their reason and coverage forward as explicit technical debt. |
| **Parallel work creates hidden integration failures.** | Per-agent changes to build flags, headers, library compilation and common renderer code interacted unexpectedly. | Delegate by a proven dependency/ownership boundary; one integrator owns end-to-end binary identity and the hardware experiment. |

These failures do **not** mean the agents accomplished nothing. They mean the project has optimized for **generating changes and diagnostic evidence** more consistently than for **closing one causal performance hypothesis**.

## 3. Understand the source architecture before changing it

Read the active paths, not just old plans. Relevant source locations at the date of this guide:

- `hgss/game/app/main_3ds.c`: the live shell, display-thread orchestration, `RenderScreen`, CPU drawers, frame handoff, logging and presets.
- `3ds_port/nitro/src/port_render2d.c`: the large DS-2D emulation engine; `DrawEngineT` resolves backgrounds, sprites, priority, windows, alpha and CPU pixel planes. It also attempts some GPU BG/OBJ nominations and whole-region commits.
- `3ds_port/game/app/port_compose_pica.c`: PICA200 composition, texture publication, quads, frame targets, post-processing and display handoff.
- `3ds_port/nitro/src/port_gx3d.c`, `port_gx3d_vertex.c`, `port_gx3d_prune.c`: DS 3D command interpretation, geometry, transforms, clipping/pruning and GPU vertex preparation.
- `3ds_port/game/app/port_gx3d_pica.c`: PICA200 draw commands, textures, batches, scene preparation and GPU waits.
- `3ds_port/game/app/gx3d_passthrough.v.pica`: existing vertex program; its own comment states that CPU transformation, clipping and viewport work were already performed.
- `tools/hgss_game.py`, `3ds_port/nitro/Makefile`: what is actually built, where each flag is propagated, and which archive gets rebuilt.
- `docs/progress/STATE.md` and `docs/progress/agent-notes/`: **evidence ledger**, not infallible or necessarily current truth. Entries from earlier commits must not be silently reinterpreted as results for HEAD.

### Present 2D path, conceptually

```text
DS game writes registers / tilemaps / character data / OAM / palettes
                │
      captured or mirrored DS state
                │
          Render2DSplit / DrawEngineT
           ├─ supported BG/OBJ region ──→ texture + PICA quad
           └─ remaining region ────────→ CPU scanline rasterizer
                                            │
                                     CPU under/over planes
                                            │
                                     conversion/tiling/upload
                                            │
                  PICA compositor ←─────────┘
                         │
                  display framebuffer
```

A GPU quad **does not** automatically mean the CPU avoided palette decoding, hashing, sorting, region bookkeeping, pixel-plane clears or uploads. Inspect the entire path before describing it as GPU-accelerated.

### Present 3D path, conceptually

```text
DS GX/G3 state and command stream
             │
   ARM11 interprets matrix/vertex commands
             │
       CPU transforms / clips / expands
             │
        PICA-ready triangles and batches
             │
      PICA vertex program (mostly final mapping)
             │
              GPU rasterization
```

This is a functional translation, but it strands a meaningful amount of vertex work on a relatively slow ARM11. A faster GPU backend is not equivalent to merely using `C3D_DrawArrays` instead of software pixel rasterization.

## 4. DS graphics and PICA200 are not interchangeable

### 4.1 What the DS gives the game

The DS has **two specialized 2D engines**. They fetch and combine tilemap backgrounds and OAM sprites, interpret palette indices, apply priority, affine transforms, windows, mosaic and alpha/brightness effects, and can change state at scanline boundaries. The CPU often changes **metadata and VRAM**, not a whole finished image.

Consequently a tilemap scroll does not mean *rerender every tile in software*. On the DS, the display engine changes the addresses from which it fetches pixels. Porting that operation as a full CPU render is a substantial semantic mismatch.

The DS 3D engine separately interprets its own fixed-point matrices, polygon attributes, texture formats, clipping, depth and edge-marking behavior. Some of this can be translated into PICA states; some requires emulation and special-case handling.

### 4.2 What PICA200 actually provides

PICA200 is a GPU with programmable **vertex** processing and a largely **fixed-function fragment pipeline**. It supports multiple texture units, texture combiners (TEV stages), alpha testing, blending, depth/stencil, scissor, framebuffer operations, and GPU display/texture transfers. It is *not* a modern GLSL fragment-shader GPU. Its vertex shaders use a specialized, limited instruction set and non-IEEE reduced-precision floating point.

Crucial implications:

- **No generic programmable pixel shader.** Do not design an arbitrary fragment-time DS palette lookup, per-pixel tilemap decoder or arbitrary blending algorithm as if GLSL fragment shaders were available.
- **No native DS palette-indexed texture format.** The PICA texture list includes RGBA8, RGB8, RGB565, RGBA5551, RGBA4, intensity/alpha and ETC1 variants; it is not a DS 4-bpp/8-bpp indexed-background engine. Palette resolution generally occurs on CPU during texture construction/updates, or through a deliberately tested restricted combiner/lookup technique—not automatically in hardware.
- **Texture memory is tiled.** PICA textures use an 8×8 swizzled layout. A naïve linear image upload either requires conversion or is wrong. Repeatedly retile entire images is avoidable if swizzled assets remain persistent and dirty regions are updated.
- **TEV is useful but finite.** It can combine textures, constant colors and vertex colors for selected effects. It cannot reproduce every DS arbitrary first/second target, per-pixel window, blending and priority rule in one universal pass.
- **Vertex work *can* move to hardware.** Ordinary transforms and vertex-affine operations are suitable candidates, provided input data and precision contracts are designed for PICA instead of inherited from the CPU-raster path.
- **Transfers and synchronization are real costs.** CPU cache maintenance, GPU commands, texture relocation, render-target clears, command list pressure, and GPU waits can erase a nominal GPU benefit.
- **Framebuffer dimensions, orientation and addressing differ.** The physically rotated 3DS framebuffer and the 400×240/320×240 displays require explicit coordinate and UV conventions. A quad that looks correct in a host test can still be mirrored by a live PICA state/upload error.

Primary platform sources: [3dbrew hardware](https://www.3dbrew.org/wiki/Hardware), [PICA registers and formats](https://www.3dbrew.org/wiki/GPU/Internal_Registers), [shader instruction set](https://3dbrew.org/wiki/Shader_Instruction_Set), [citro3d base API](https://github.com/devkitPro/citro3d/blob/master/include/c3d/base.h), [citro3d TEV API](https://github.com/devkitPro/citro3d/blob/master/include/c3d/texenv.h), and [citro3d render queue implementation](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c).

### 4.3 The useful mapping is **behavioral**, not one-to-one register emulation

| DS rendering behavior | PICA-friendly representation | Important mismatch / acceptance condition |
|---|---|---|
| Scrolling text background | Persistent rendered tilemap texture, UV offset; or visible tile quads from a cached atlas | Avoid whole-image CPU regeneration on scroll. Texture contents/palette/flip and wrap must stay exact. |
| Dynamic BG tiles | Update affected swizzled texture tiles/regions | Changes to DS character bytes or palette may invalidate only dependent tiles; ownership and dirty tracking must be trustworthy. |
| Tilemap entry changes | Update affected cached cells, tile quads or texture rectangles | Tile indices, h/v flip, map sizes, bank offsets and wraparound need coverage. |
| Ordinary OBJ sprites | GPU quads and cached OBJ textures/atlas | Honor DS OAM order, priority, shape/size, transparent color zero, palette and OBJ mapping mode. |
| Affine OBJ / affine BG | Vertex transform / UV transform where representable | Double-size sprite behavior, center, rounding, clip and wrap must be validated. |
| Multiple BG/OBJ priorities | Explicit ordered layer or band draws | Depth alone does not reproduce DS per-pixel priority/tie rules and transparency. |
| Simple translucency/brightness | TEV constants, vertex color, target blending in a proven order | DS coefficient clamping, target masks and semitransparent-OBJ behavior need exact test cases. |
| Windows and OBJ windows | Scissor for rectangular regions, stencil/mask where needed | Scissor alone is not an arbitrary per-pixel DS window; masks may require an additional render pass or fallback. |
| Mosaic | Quantized UV/geometry or preprocessed tile data | Sample origin and pixel grouping matter; prototype actual PICA rounding. |
| H-blank/per-line offsets | Compact runs or scissored strips with per-run UV/state | Do not emit 192 separate GPU draws by default. Coalesce identical runs. Keep the game’s H-blank/IRQ semantics intact. |
| Capture-sourced/bitmap/exotic modes | Separate proven path or software fallback | Do not silently reinterpret a display mode as ordinary BG quads. |
| 3D polygon position | PICA vertex transformation where proven | Original DS fixed-point transforms/clipping and PICA float precision are different; mixed CPU/GPU results need an explicit equivalence contract. |
| Final presentation | GPU render target and display transfer | Preserve orientation, color format, source/destination validity, synchronization and ownership. |

**A single GPU quad per BG is not, by itself, a tilemap renderer.** That quad needs a ready-to-sample texture representing the correct map content. If the CPU rebuilds it from scratch every update, most of the intended benefit disappears. Conversely, turning every 8×8 tile into its own draw call can trade CPU raster time for command/state overhead. Both representations are valid candidates; measure the actual cost on hardware.

## 5. Preferred 2D architecture: persistent state, cheap per-frame commands

### 5.1 Separate DS truth from 3DS representation

Keep an authoritative snapshot of what the game wrote: relevant display registers, VRAM mappings, BG map/character data, palettes, OAM, window controls, brightness and per-line effects. The snapshot must be **coherent**—a renderer reading a mixture of before/after writes is not correct.

From that snapshot, maintain *derived GPU resources*: cached resolved tile textures, atlas allocations, dirty map regions, GPU sprite descriptors and draw commands. These derived resources are disposable and can be rebuilt; they are **not** a second source of truth. A new framebuffer, screen swap, save load or scene transition must never accidentally reuse state tied to a prior snapshot.

Separate modules by responsibility, not just by file length:

- DS-state capture and versioning;
- map/tile/palette dependency tracking;
- GPU texture cache and swizzle/upload;
- background geometry and scrolling;
- sprite/OAM translation;
- DS priority/window/blend composition;
- GPU submission, synchronization and resource lifetime;
- software reference and validation.

Do not replace today's multi-thousand-line monolith with a different GPU monolith. Avoid stateful cross-module hidden statics when an explicit per-engine/per-frame context is possible.

### 5.2 Two legitimate BG representations—choose by evidence

**A. Flattened GPU background texture.** Maintain a GPU-sampleable image of the map; use a quad with changing UVs for scrolling. Update only dirty map/character/palette-dependent regions. This minimizes draw calls and is attractive when scrolling is frequent and tile edits are sparse. Downsides: full-map storage, texture size/padding, wrapped regions, and palette edits may make updates costly.

**B. Cached tile atlas plus visible tile quads.** Resolve each distinct visual tile in an atlas, derive visible cells/UV flips from the map, and draw coherent batches. This avoids full-map rebuilding and can handle rapid map changes. Downsides: visible tile count, batch/command overhead, atlas churn, palette variants, and per-cell draw submission. Use indexed/batched geometry only when its measured reduction exceeds its preparation cost. Do not assume tile instancing is a free PICA feature.

A hybrid is sensible: a precomposed/background-wide texture for stable field layers; an atlas/dirty-cell path for character animation or rapidly changing regions. The choice is **a measured trade-off**, not an ideological requirement.

Important implementation invariants:

- **Scroll changes coordinates, not image contents.** `HOFS`/`VOFS` changes should not invalidate decoded tile pixels unless a special rendering mode truly changes them.
- Cache keys include the things that change pixels: source bank and mapping, character bytes, map entries, palette contents/version, flips, bit depth, relevant visual modifiers. They must **exclude scroll** from the pixel-content key while retaining scroll in draw state.
- Dirty tracking must cover every write path (CPU stores, DMA, decompress/copy, VRAM remapping, palette and OAM changes). If writer coverage is incomplete, keep a conservative fallback. **An unverified generation counter is worse than an expensive hash.**
- If hashing is the safety check, report bytes hashed and active CPU time. Do not replace a scanline renderer with a full-VRAM scan and call the CPU cost eliminated.
- Tile transparency must be tested against referenced pixel indices, not assumed from tile index zero. Goldenrod exposed this exact problem.
- Palette edits may affect tiles referenced in multiple backgrounds or atlas entries. Track dependencies or invalidate a safe superset.
- Do not store redundant 2×, right-eye, menu or shadow textures when the active Old 3DS profile does not use them. Allocate lazily; measure peak *remaining* memory, not just startup free space.

### 5.3 Sprites: translate OAM semantics, not just sprite pictures

An OBJ is not simply a textured rectangle. Build an explicit object record carrying OAM index/order, DS priority, size/shape, palette, tile mapping, flip, affine matrix/double-size semantics, object mode, mosaic and transparency state.

Group only objects whose **ordering and blending** may be changed without changing visible pixels. Grouping across a texture or stencil boundary previously duplicated NPCs and created halos; that is evidence to constrain state merging, not a reason to abandon all batching. Keep separate correctness fixtures for ordinary sprites, semi-transparent sprites, affine sprites, OBJ-window masks, disabled sprites and sprites crossing the 3D plane.

Do not reuse stale `ObjFrame` state across disabled-OBJ frames. The repository already found that bug. Distinguish "OAM contains entries" from "the engine currently displays these entries".

### 5.4 Priority, windows and blending are the dangerous part

DS 2D priority is evaluated at the pixel level. GPU depth ordering and painter-ordered quads work only for **specific proven compositions**. Build explicit, small state classes: opaque backgrounds without windows; sprites with color-zero transparency; known alpha effects; rectangular windows; per-line changes; complex modes. Document when a draw ordering can represent a class exactly, and when an intermediate surface or fallback is necessary.

Use PICA's TEV, alpha test, blend, scissor and stencil **for the operations they support**. Do not claim one combiner setup reproduces all DS first/second-target blending or window-mask logic. Some operations need precomputed masks, multiple draws, a compositing surface, or CPU fallback. Beware extra passes and render-target transitions—they can be more expensive than a narrow software case.

A region-wide fallback may be correct, but it is often a blunt performance policy. If one 8-pixel region has an unusual mask, a full-width, full-height CPU-rendered frame is too large a penalty. Prefer compositional partitioning **when proven safe**: isolate the exception into a bounded rectangle/band and keep independent regions on GPU. Do not pretend software and GPU layers can always interleave with no intermediate surface.

A debug `GPU-take` count is not a measure of success. Report **CPU pixels/scanlines actually skipped**, bytes converted, bytes uploaded, texture-cache hits, draw calls, and presentation rate. A taken BG that still triggers a complete setup/hash/plane clear is only a partial win.

### 5.5 Screen layout should not contaminate core rendering semantics

The project defaults to the larger 3DS canvas/remaster path, including 400×240/320×240 display targets. Keep the DS logical pixel coordinate system and the 3DS presentation coordinate system conceptually separate. Wide-field extension, 1.25× scaling, texture filtering and touch mapping are **presentation policies**. They must not accidentally alter map wrapping, DS blending rules or OAM priority.

Do not silently disable wide mode to claim a speedup. A reduced-quality configuration may serve as a bounded diagnostic control, but user-visible layout is a project requirement unless the owner explicitly changes it.

## 6. Use PICA200 for the 3D work it is good at

The current `gx3d_passthrough.v.pica` describes its input as already transformed and clipped by the CPU. That is a valid compatibility starting point, but it is not the end state of hardware acceleration.

### What is reasonable to move

- Position, projection and texture-coordinate transforms expressed as a PICA vertex program, with matrices/uniforms updated when relevant DS state changes.
- Repeated vertex attributes and static model/draw data into persistent or efficiently reused GPU buffers, where validity and lifetime can be proven.
- Ordinary rasterization, depth/stencil and supported fixed-function blend/texture states.
- Independent draw commands and state transitions where the GPU does less work than rebuilding equivalent CPU geometry.

### What is **not** automatically transferable

- Interpreting the arbitrary DS GXFIFO stream and all matrix-stack semantics inside a PICA shader.
- DS exact fixed-point arithmetic, clipping rules, polygon ordering, edge marking, shadow passes and W/Z-buffer behavior.
- Dynamic per-polygon software-generated geometry, special 2D-in-3D sprites, and all transformation-dependent CPU decisions.
- Any effect relying on PICA executing a general fragment program (it cannot).

Treat a GPU-transform path as a **feasibility question with bounded input classes**, not a promise that eliminating CPU transforms will be easy or bit-exact. Design the interface around *pre-transform vertices plus DS matrix state*, rather than trying to reverse transforms after the existing CPU pipeline. Confirm that the resulting shader fits instruction/uniform budgets, uses valid PICA numerical ranges, and preserves clipping/ordering within the visual contract.

PICA uses a reduced-precision floating-point representation, not ordinary IEEE float32 throughout its shader pipeline. Host mathematical equality is not enough. Validate near-plane crossing, perspective, orthographic sprite quads, degenerate polygons, and large coordinates on the **real GPU**. Do not resurrect the historical `w <= 0` fan workaround without understanding why it removed real Ho-Oh wing geometry; use the subsequent zero-area camera-plane rule as the documented regression case.

**Important:** The indexed-vertex experiment already demonstrated that deduplication can be net-negative. If expanded triangle vertices differ because of per-triangle pixel expansion, there may be almost nothing to share. Do not build a new indexing subsystem without measuring the input duplication rate and candidate preparation cost first.

## 7. GPU memory, command submission and synchronization

GPU work has an entire cost chain:

```text
construct or update data
    → convert / swizzle if required
    → CPU cache maintenance or GPU transfer
    → command recording / state changes
    → GPU queue execution
    → render-target/display transfer
    → V-blank presentation
```

A performance report that only times `C3D_DrawArrays` has omitted most of the pipeline.

### 7.1 Keep resources persistent and owned

Track an explicit owner for each texture, atlas, command buffer, vertex buffer and render target. Record the source generation, valid contents, last GPU use and point at which reuse/freeing is safe. Avoid allocation and deletion in the steady-state frame loop. A cache hit that still performs full-size conversion, upload, or GPU wait is not a real cache hit.

Memory is scarce. Old 3DS has 6 MiB of physical GPU VRAM, but much of it is used by framebuffers and system/runtime allocations; PICA resources may also be backed by suitable linear memory. The application's usable memory depends on process configuration, image/BSS size and the selected heap/linear allocations. The repo measured an approximately 64-MiB Old 3DS process and earlier wasted several MiB. **Do not treat physical VRAM size, free linear heap and process virtual memory as interchangeable numbers.**

Texture-format arithmetic matters: a 512×256 RGB565 texture is 256 KiB; an RGBA8 version is 512 KiB, *before* extra buffers and other resources. Doubling both dimensions multiplies texel count by four. Carefully account for padding to supported texture dimensions and for duplicate display sets. Default to compact formats only when they preserve required alpha/color behavior.

### 7.2 Respect cache coherency and GPU ownership

- Textures and vertex/command buffers touched by the CPU must be visible to the GPU; use citro3d/libctru's appropriate flush/transfer mechanisms for the actual memory type.
- Do not overwrite a texture/vertex buffer that an in-flight GPU command may still read. Fencing or ring-buffer ownership is a correctness requirement, not optional debugging overhead.
- Do not introduce a GPU→CPU readback merely to make the software compositor happy; that can serialize the pipeline. Prefer keeping the rendered data on the GPU until display transfer.
- Use citro3d's frame queue and synchronization contracts, not an assumed immediate-execution model. The library's `C3D_FrameBegin` can wait for a previous queue; `C3D_FrameEnd` and transfers have their own behavior. The source implementation—not just an API name—defines the actual stalls.
- Log **bytes transferred**, **transfer count**, **cache flush bytes**, **queue waits**, **GPU timings**, **command-list occupancy**, **allocations** and **resource evictions** where instrumentation can do so without materially altering behavior.

Relevant upstream implementation: [citro3d `renderqueue.c`](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c), [`texture.c`](https://github.com/devkitPro/citro3d/blob/master/source/texture.c), [`base.h`](https://github.com/devkitPro/citro3d/blob/master/include/c3d/base.h).

### 7.3 Avoid replacing one bottleneck with another

A GPU rewrite can make things slower if it submits hundreds of tiny draws, changes TEV/stencil/texture state per tile, copies whole atlases each frame, clears targets unnecessarily or waits on an in-flight texture after every update. Conversely, a few large quads backed by persistent textures can be efficient even if creating those textures initially costs CPU time.

Measure both **steady scrolling** and **cold/transition** workloads. Averages can hide multi-second texture-conversion hitches; the historical 8.9-second conversion episode came from inadequate texture room and churn. Maintain high-water/low-water memory and worst-frame data. Avoid a fixed optimization that improves idle but makes transitions or battles unusable.

## 8. Threading, game timing and presentation are distinct

The game's update cadence, geometry-scene creation, CPU-drawer output, GPU composition and actual display presentation are **different counters**. Do not treat any of them as synonyms for FPS.

In this port the game clock expects the 60-Hz frame model. The project’s interim original-3DS goal is **playable presentation around 30/s without slowing that clock**; New 3DS has a separate 60/s goal. The frozen Goldenrod benchmark—not a title screen or empty bedroom—is the stress case.

Older hardware investigations exposed two severe classes of thread bugs:

1. The H-blank walk issued ~192 scheduler switches per frame. The handlers were not the main issue; the dispatch mechanism was. Removing those switches made a major difference.
2. A GPU worker was created with an unfavorable priority/core arrangement and starved behind game/CPU 2D work. Its image counter advanced far more slowly than the game scene counter.

The fix is not to randomly move more work onto core 1. Old 3DS CPU share and priority are constrained; one attempt to move audio to core 1 saturated the geometry/display budget and regressed presentation. New 3DS has different available resources. Always query the actual processor assignment and permitted APT core share on hardware, and measure the system after the thread change.

**Backpressure matters.** If CPU drawers publish around 58/s but the display composes only around 30/s, the project may be spending substantial CPU time producing images that will never be shown. Define a queue/drop policy that prioritizes the **newest complete coherent scene**, preserves safety of in-flight buffers, and never drops game-state or audio updates. Dropping render work is not dropping the simulation clock.

Every timing report must distinguish:

- `game ticks` / elapsed time / input-to-visible latency;
- `3D scene produced` versus `GPU scene drawn`;
- `2D image produced/published` versus `composed frame`;
- `frame submitted` versus actual `displayed/swapped frame`;
- active CPU work on each thread versus elapsed wall time including preemption/waits;
- audio drop count and clean HOME/close/sleep behavior.

A sample every 600 frames is not a frame-time distribution; `FRAMETIME` is documented in this repository as a sampled entry-to-entry interval with previous-frame fields, not isolated game CPU time. Reconcile counters against the actual source instrumentation before making deductions.

## 9. The performance evidence contract

**This is the most important process change.** An agent's claim must be precise, disprovable and traceable to a real binary.

### 9.1 Three different proof types

**A. Functional proof:** The code executes the intended path and produces the correct pixels, ordering, game behavior and memory safety. Host fixtures, reference renders, emulator replays and real-device observations support different parts of this.

**B. Work-elimination proof:** A measured expensive operation is actually absent or reduced in the target scene: fewer CPU scanlines, less decoded/tiled data, fewer transforms, fewer cache rescans, fewer uploads, less command preparation or fewer waits. A counter for "GPU nominated" is insufficient; show actual committed takes and what work they eliminated.

**C. Performance proof:** The exact change improves comparable presentation throughput, frame-time distribution and/or CPU/energy behavior on a real Old 3DS, without unacceptable regression. If performance does not improve, the performance claim is **not proven**, regardless of A and B.

Do not combine A, B and C into the sentence "tests passed; optimization successful."

### 9.2 Every experiment has a claim sheet

Record, in a short entry alongside the change:

```text
Hypothesis:               What costly operation should disappear, and why?
Target scene:             Which frozen save, input script and frame window?
Target hardware:          Old 3DS; relevant clock, core/share and launch settings
Baseline artifact:        Git SHA + binary SHA-256 + effective flags + config hash
Candidate artifact:       Git SHA + binary SHA-256 + effective flags + config hash
Activation proof:         Where does the real code run, and how many times?
Work proof:               Before/after CPU work, bytes, draws, waits and uploads
Correctness proof:        Real scene state + forced software reference + edge cases
Hardware result:          Paired trial results and frame-time distribution
Regression surface:       Battle, title, menus, transition, audio, sleep/close
Conclusion:               beneficial / neutral / harmful / inconclusive, with cause
Next decision:            keep / revise bounded issue / revert / archive
```

There should be **one primary hypothesis** per comparable experiment. Auxiliary controls may be used, but do not combine unrelated optimizations and then claim to know which one helped.

### 9.3 Binary identity is a hard gate

The project has already lost time to stale archives and flags that never reached the code they were supposed to change. A runtime banner alone does not prove a build flag reached a library object.

For each relevant compilation unit:

- Identify whether it builds into the app ELF or an SDK archive.
- Verify all compile definitions and options **for that unit**, not just the launch command.
- Ensure changing a flag invalidates/rebuilds the affected object and archive; preferably use fresh output directories for controlled trials.
- Inspect map/symbol/object output or disassembly when practical. If the change is compile-time only, prove the affected code differs in the relevant binary.
- At runtime, log a unique build/config identity and a bounded execution/eligibility counter from the actual changed path.
- Preserve each `.3dsx`, build sidecar, config, frozen inputs and SHA-256. **Do not compare a dirty, unattributed artifact with a clean candidate.**

If the optimized path never runs, the experiment has failed its activation gate. Do not add five more performance counters before checking it.

### 9.4 Benchmark conditions

Keep the frozen Goldenrod save, scripted movement, clock/time-of-day, game/profile settings, stereoscopic state and scene window identical. Compare baseline/candidate pairs, preferably in repeated alternating trials to reduce warm-state and environmental bias. Separate cold texture loads from warmed motion and quiet/idle from moving scenes. Include the known 3D-heavy area and the software/OBJ-heavy transitions.

A useful minimum report includes:

| Domain | Values to record |
|---|---|
| Presentation | unique frames actually displayed/s, median/p95/p99 intervals, long stalls, latency if measured |
| Simulation | game ticks/s, timer pacing, input cadence; **no slowed clock** |
| CPU 2D | active work by engine/layer, pixels and scanlines, setup/hash/OBJ/blend, CPU planes and publication |
| 3D preparation | scene count, input/output vertices, clipped/rejected count, transform/clipping CPU time, indexed-prep cost |
| GPU | actual scene draws, command words, number of draws/state changes, wait time, GPU execution/transfer time when available |
| Texture/memory | bytes decoded/swizzled/uploaded/flushed, cache hits/evictions, memory low-water and allocations |
| Robustness | audio drops, visible correctness, HOME/close/sleep result, OOM/fallback count |

**Wall-clock phase times from concurrently scheduled workers are not additive CPU costs.** If a 400-ms "drawer render" contains substantial preemption, it does not prove 400 ms of renderer instructions ran. To distinguish these, use meaningful per-thread active CPU counters where available and bounded controlled on/off experiments. If accurate CPU-time attribution isn't available, state that limitation explicitly rather than inventing a percentage.

An emulator can prove that a command path is entered and produces a particular image. It cannot certify the Old 3DS's actual CPU budget or speedup.

## 10. Diagnose a specific contradiction, not the entire engine again

The current cycle tends to look like this: a fast path does not engage; several counters are added; these disagree; more counters are added; a new optimization arrives without a conclusive hardware result.

Replace that habit with a **bounded decision procedure**:

1. State the observation in a sentence tied to one exact artifact and scene: e.g. "GPU BG1+BG2 were committed but the CPU still rendered 192 empty lines per region."
2. List at most three plausible, mutually distinguishable causes. Identify the **one piece of evidence** needed to separate them.
3. Check existing logs and source control flow first. When necessary, add a **single local discriminator** at the decision point, not a new global telemetry framework.
4. If the discriminator cannot be trusted, validate **the discriminator** with a deliberately triggered case before reading its ordinary data.
5. Implement one correction, or show why a correction is unsafe. Preserve old and new run identities.
6. If the promised work disappears but FPS does not improve, **accept that the work wasn't currently binding**. Do not indefinitely optimize it just because it is now interesting.

Examples of especially important diagnostic pitfalls from this repository:

- `OAM attr0 bit9` is not simply "disabled" for affine sprites. Previous diagnostics lied about visible OBJs. Compare with the real semantics of the bit in context.
- VRAM writes between two sequential hashes can produce disagreement even if both hash functions are correct. An intrusive full-state logging pass can alter scheduling and make a race vanish.
- A `Render2DSplitIfChanged` skip still must propagate fresh GPU scroll/quad metadata; skipping only pixels but reusing stale layout is wrong.
- `no mismatches` when a fast path had **zero takes** is not evidence that its taken case is correct.
- A fixture that has never reproduced the real failure can "pass" indefinitely; it isn't a regression test for that failure.
- If a patched library object is stale, all performance conclusions using that build are void. Check actual archive membership and content.

**Stop adding diagnostics once the decision is identifiable.** Prefer a small repro and one decisive comparison to a general profiler that changes the workload.

## 11. Correctness policy: a GPU path must be safe to trust

The software path is the correctness oracle for 2D. Preserve an independent forced-full reference, rather than comparing two paths that both use the same optimization's cache or assumptions. Compare the actual visible pixels, alpha where semantically relevant, and the metadata that determines ordering/texture selection. Match state snapshots and frame phase; mismatched screenshots and VRAM dumps from advancing dialogue are invalid evidence.

Validation needs to cover **the transitions**, not just stable frames:

- Field motion and scrolling; same map with modified tiles/palette;
- background enable/disable, blanking, VRAM remap, screen swap;
- textboxes, menu highlighting, character BG updates;
- ordinary/affine/transparent/bitmap OBJ, OBJ-window and disable/re-enable;
- battle transition, dialogue, Pokémon, status boxes and return to field;
- H-blank register changes, windows, color effects, mosaic;
- title scenes, camera-plane polygons, W/Z/depth/edge cases;
- buffer reuse, GPU in-flight updates, OOM and cold texture eviction;
- HOME close, sleep/resume, audio and save behavior.

Two-dimensional pixel equality is desirable and practical for a software baseline. For 3D PICA rendering, record **explicit** tolerances and known precision/depth differences; do not silently loosen goldens to hide a bug. Real-device screenshots or framebuffer captures must test the final GPU state, not only host-side quad UV calculations. The battle's mirrored text is an instructive example: the host quad path looked plausible while real PICA output was wrong.

The game and NitroSDK patch stack must preserve DS behavior outside `SDK_PORT_3DS` changes. Keep both HeartGold and SoulSilver matching-build gates as the relevant authority when changing common game/SDK behavior. Do not mark them passed when the necessary toolchain or data is absent. Use sanitized host fixtures for C memory-safety checks, but do not treat them as ARM11 ABI/CPU/GPU proofs.

## 12. Agent conduct and code review rules

Existing `AGENTS.md` gives delegation rules. This guide adds a technical acceptance standard.

### Required behavior

- **Read the live implementation and the relevant dated hardware note before proposing an edit.** `gameplan.md` is useful intent; it is not evidence that a proposed design exists or has worked.
- Identify the *entire* operation being changed: DS writer → captured state → CPU work → GPU data → frame presentation. Improving a leaf function may merely move time into another leaf.
- State whether a change is a compatibility fix, CPU-work elimination, GPU offload, memory reduction, scheduling correction or diagnostic-only change. These are different categories.
- Prove execution in the exact target binary and scene. A flag or test-only path is not a shipping feature.
- Keep file ownership narrow, interfaces explicit and shared state understandable. Avoid giant new switch statements, megafunctions or separate copies of the same pixel/UV algorithms.
- Avoid parallel implementation when two slices alter shared layout, state lifetime, render ordering, build flags or the same archive. Parallel read-only research and disjoint independent fixtures are fine.
- Explain any fallback: what condition triggers it, how frequently it triggers in the frozen scene, and its actual CPU cost. Fallbacks should be safe **and visible**, not silently permanent.
- Run tests that genuinely exercise the changed code, then the available broader gates. Report unavailable checks truthfully. Never silently weaken a golden to get a green suite.
- Make the small verification run reproducible from source + build identity + input data. Avoid manually staged, ambiguous experiment names.
- Include regressions and negative results in the final report. An optimization with no measured gain is not a victory because it generated more instrumentation.

### Forbidden reasoning patterns

- "We use PICA, therefore the path is hardware-accelerated." **Which expensive CPU work stopped?**
- "The new code compiles, therefore the benchmark measured it." **Which object contains it, and does the live path enter it?**
- "The test is exact, therefore it's fast." **Where is the actual Old 3DS timing delta?**
- "The emulator runs fast/slow, therefore the console will." **Measure the console.**
- "A scene without the difficult BG/window/sprite looks correct, therefore the general path works." **Exercise real register combinations.**
- "The diagnostic says `unchanged`, therefore nothing wrote VRAM." **Validate writer coverage and coherent snapshots.**
- "Add another global log or fallback condition." **Explain the one missing distinction and its predicted outcome.**
- "Move everything to GPU immediately." **Test fragment/TEV, texture, submission and precision feasibility first.**
- "The fix is simple if we disable wide screens, audio, fades, shadows or battle effects." **Those are product regressions, not silent performance wins.**

A report must conclude *what became cheaper or unnecessary*, *what did not*, *what was measured on real hardware*, and *what remains uncertain*. Coding agents must not create a speculative multi-workstream plan in place of answering the immediate engineering question.

## 13. How to evaluate design alternatives without another endless rewrite

For any proposed GPU subsystem, ask the same questions:

1. **What DS behavior must it preserve?** Write down relevant register/state classes and transitions.
2. **What physical PICA feature performs that behavior?** Vertex transform, texture fetch, TEV, blend, stencil/scissor, target transfer—not a generic invocation of "GPU".
3. **What still happens on the CPU every frame?** Be explicit about decoding, map walks, sorting, hashing, conversion and command generation.
4. **What survives between frames?** Stable texture allocations, tiles, atlases, resource versions, geometry and command data.
5. **When does it invalidate, and how is that proven safe?** Cover all DS write paths and same-frame races.
6. **How much GPU work replaces the CPU work?** Count draw calls, state changes, bytes, clears, offscreen targets and waits; a one-quad design and a thousand-quad design have different costs.
7. **What is the minimal real-game counterexample?** Include a frozen field state and at least one transition, not only a hand-built synthetic case.
8. **Can the Old 3DS afford it?** Measure resource high-water, CPU load, command/transfer overhead and presentation on hardware.
9. **How does it fail safely?** Use a bounded, observed fallback; avoid corrupted pixels or stale textures.
10. **What evidence would make us abandon it?** State a stop condition before investing in more generalization.

This is **a design-review test**, not a sequential implementation plan. It applies whenever an agent proposes a cache, a shader, a sprite batch, a GPU transform, a texture format change or an instrumentation addition.

## 14. Technical cautions particular to this repository

- `RenderScreen` currently nominates upper-screen GPU BGs conditionally; engine B and the rest of engine A may still be software. Count actual work avoided per engine.
- `DrawEngineT`'s `frameOk` / region-commit logic excludes meaningful live combinations. A small number of GPU take counters does not prove a full scene GPU path.
- Existing CPU planes can still be filled, cleared, converted, hashed or uploaded after a GPU take. Trace the call all the way through `port_compose_pica.c`.
- Current GPU BG textures are **resolved pictures**. A future retained-atlas path is not equivalent to current `gpuBgPicture`/key caching; measure decode/publish cost rather than assuming the cache already avoids it.
- The existing PICA vertex program is primarily a final mapping of CPU-prepared geometry. 3D rasterization offload is real; full 3D transform/clipping offload is **not** already complete.
- Indexed drawing can be harmful where per-triangle vertex expansion removes all sharing. Hardware log 36 is an explicit counterexample.
- The 2026-11-06 `partialBChar` work has host and emulator activation evidence, but the documented candidate's Old 3DS speedup is not established by those runs. Preserve that distinction.
- Source comments and historical plans sometimes use outdated line numbers or old flag names. Resolve symbols in the current revision.
- The battle GPU veto restores correctness, but the per-row horizontal text mirror is an unresolved live GPU/upload/state issue. Do not generalize the veto into a permanent rendering design.
- Ensure that the same application and archive flags are active. `tools/hgss_game.py` alone cannot guarantee a `3ds_port/nitro` compile-time experiment was enabled.
- The reported Old 3DS core-share limit was 80% in a particular hardware run, contrary to earlier 30% assumptions. Query actual granted values rather than hardcoding either.
- A `fps=30` cap changes game speed. A 30/s presentation policy must not do that.
- Never infer a CPU-compute bottleneck from wall time alone under overlapping threads. That mistake already sent development in circles.

## 15. Definition of an engineering improvement

A worthwhile change should leave the project with **less recurring work, a simpler or better bounded rendering architecture, an independently validated visual result, and a measured gain on the scene that needed it**.

A useful correction can still be retained if its performance effect is neutral—e.g., it fixes stale OBJ semantics—but it must be described as a **correctness fix**, not sold as an FPS breakthrough.

Likewise, a failed idea is useful when its failure is established decisively and cheaply: "the indexed path performs all the original vertex uploads plus extra index/preparation work" is a valuable conclusion. More diagnostics attempting to rescue that conclusion without a changed premise are not.

**Success for this project is not more logs, more flags, more passing fixture assertions, or a GPU-related file growing larger. It is a native HeartGold/SoulSilver port whose ordinary DS drawing operations are represented efficiently on PICA200, with the software renderer reserved for the cases that genuinely need it, and whose performance and correctness have been proven on the launch-model 3DS.**

---

## References and evidence index

### Repository source

- [Main 3DS shell and render handoff](https://github.com/estyseesghosts/hgss-3ds/blob/main/hgss/game/app/main_3ds.c)
- [Software DS 2D engine and GPU BG admission](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_render2d.c)
- [PICA compositor / texture publication](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_compose_pica.c)
- [GX3D command translator](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_gx3d.c)
- [GPU vertex preparation / index compaction](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_gx3d_vertex.c)
- [PICA draw backend](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_gx3d_pica.c)
- [Current PICA vertex shader](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/gx3d_passthrough.v.pica)
- [Current project evidence ledger](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/STATE.md)

### Project investigations

- [Session 20: first major Old 3DS performance gains](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s20-performance-session.md)
- [Goldenrod GPU-2D design discussion (historical plan; not a directive here)](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s21-goldenrod-2d-kill-plan.md)
- [Real Goldenrod BG-topology and matrix work](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/o3ds-w3-w4-local-validation.md)
- [Battle UI mirror: veto versus root cause](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md)
- [Run 26: stale OBJ state and limits of the hardware evidence](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/o3ds-log26-disabled-obj.md)
- [Half A integration / flags not reaching the archive](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-integration-wave2.md)
- [Half A current hardware and emulator results](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-session-2026-11-06.md)
- [Hardware testing / measurement semantics](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/wiki/Hardware-Testing.md)

### Platform documentation

- [3dbrew: 3DS hardware](https://www.3dbrew.org/wiki/Hardware)
- [3dbrew: PICA internal registers, texture formats, blending/TEV](https://www.3dbrew.org/wiki/GPU/Internal_Registers)
- [3dbrew: PICA shader instruction set and floating-point behavior](https://3dbrew.org/wiki/Shader_Instruction_Set)
- [3dbrew: memory layout](https://www.3dbrew.org/wiki/Memory_layout)
- [citro3d: draw and buffer API](https://github.com/devkitPro/citro3d/blob/master/include/c3d/base.h)
- [citro3d: TEV combiners](https://github.com/devkitPro/citro3d/blob/master/include/c3d/texenv.h)
- [citro3d: render queue, frame/transfer timing and sync](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c)
- [citro3d: texture allocation/upload](https://github.com/devkitPro/citro3d/blob/master/source/texture.c)
- [devkitPro textured cube: a minimal end-to-end PICA example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/textured_cube/source/main.c)
