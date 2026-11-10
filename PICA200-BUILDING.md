# PICA200 Implementer's Guide for `hgss-3ds`

**Purpose:** Engineering reference and implementation instructions for making Pokémon HeartGold/SoulSilver run efficiently and correctly on the Nintendo 3DS GPU, especially the original 3DS.

**Research date:** 2026-11-09. **Repository inspected:** [`estyseesghosts/hgss-3ds`](https://github.com/estyseesghosts/hgss-3ds), `main`, latest inspected commit `295ddef7edb1a2c268e7c7944b03b57cf0de57db` (2026-11-06). The repository's `docs/progress/STATE.md` and dated agent reports also describe **uncommitted local work**, so a feature described in a report is not automatically present, enabled, or hardware-validated on `main`.

**Intended readers:** Rendering implementers and reviewers with C, devkitARM, libctru/citro3d, and the HGSS port's architecture available. This is a **technical design guide**, not an authorization to rewrite the renderer, change the DS game's logic, or relax correctness checks.

## 0. The central diagnosis

**The PICA200 is capable of drawing the HGSS graphics workload. Feeding it is the problem.** The current port already uses the 3DS GPU for native-resolution 3D, composition, selected tiled backgrounds, and selected sprite quads. But it still **emulates much of the DS 2D video pipeline on the ARM11, converts DS G3 commands into screen-space triangles on the ARM11, hashes/decodes content, and repackages those results into GPU inputs**. This is very different from a commercial 3DS game, which normally maintains GPU-native meshes, textures, materials, and display lists.

The highest-leverage architectural improvement is **persistent GPU-native scene data with small, correctness-certified updates**: background maps that scroll by changing UVs, cached sprite/tile atlases, and geometry streams that can skip repeated DS command interpretation. Use the existing CPU renderer as an oracle and as fallback for DS-specific effects that do not map exactly to PICA hardware.

This is **not** a project to replace every DS pixel operation blindly with a quad. HGSS has priority, colour-effect, window, HBlank, mosaic, and 3D/2D interaction rules. Some modes can be accelerated cleanly; some require multi-pass rendering; others should remain software until proven exact.

### Non-negotiable outcomes

- Preserve game **60 Hz logic/DS VBlank semantics** independently of the number of presented frames. A 30 Hz presentation mode must not halve game speed.
- Render correctly on **Old 3DS**, not only New 3DS or Azahar.
- Preserve DS sprite/3D priority, translucent polygon order, stencil/polygon IDs, alpha, windows, palette changes, text, touch mapping, and scripted effects.
- Keep any fallback **complete, observable, and correct**; missing an eligible fast path is a performance issue, not a rendering defect.
- Measure **CPU work, GPU execution, transfer/publish, and GPU submission separately**. Higher `comp{s}/s` in an emulator is not proof of hardware speed.

## 1. PICA200 hardware: what actually matters

| Component | Hardware reality | Consequence for HGSS |
|---|---|---|
| Vertex processing | Three vertex shader processors plus a shared geometry/vertex-capable processor; shaders use PICA assembly and an unusual float24 representation | A native shader can transform quads/meshes; game-independent DS fixed-point semantics need validation before moving math there |
| Geometry processing | Limited programmable geometry shader, *not* desktop geometry/compute | Do not use geometry shaders to fabricate thousands of UI sprites or simulate a DS scanline renderer |
| Rasterization | Hardware triangles, clipping, interpolation, depth/stencil, culling | Tiny JRPG geometry is not the scary part; CPU transformation and draw submission may dominate |
| Pixel processing | **No programmable fragment shader**. Three ordinary texture samplers plus a fourth procedural texture stage | No arbitrary per-pixel palette lookup or software-style conditional blend in a GPU shader |
| Texture environment | Six configurable **TEV** colour/alpha combiners, plus dedicated LUT lighting/fog/blend hardware | Express common tint, fade, masking, highlight, simple texture blending with state, not software pixels |
| Textures | GPU-native tiled layout, commonly 8×8 Morton blocks; texture dimensions typically powers of two, with hardware limits | Convert/swizzle on asset change, cache; do not convert a static atlas every frame |
| Memory | **6 MiB dedicated VRAM**, plus GPU-accessible **linear FCRAM** constrained by process memory mode | `8 MiB texture cache` in linear memory is valid; **do not** assume 8 MiB is fitting inside 6 MiB VRAM |
| Transfers | Separate GSP/GX commands for display transfer, copies, fills, format conversion and supported scaling | Present/copy without a CPU pixel loop; transfer work still costs bandwidth, time, and synchronization |
| Stereo | Extra right-eye work and surfaces when enabled | Explicitly benchmark two eyes; disable/reduce optional effects before sacrificing correctness on Old 3DS |

Sources: [3dbrew hardware](https://www.3dbrew.org/wiki/Hardware), [PICA internal registers](https://www.3dbrew.org/wiki/GPU/Internal_Registers), [3dbrew GPU guide](https://www.3dbrew.org/wiki/GPU/Programming_Guide), [3dbrew memory allocation](https://www.3dbrew.org/wiki/Memory_Allocation), [citro3d texture interface](https://github.com/devkitPro/citro3d/blob/master/include/c3d/texture.h).

### 1.1 The crucial palette restriction

The DS displays indexed 4-bpp or 8-bpp BG and OBJ graphics using hardware palette memory. **The PICA200 is not a drop-in DS colour-index/palette lookup engine.** A shader like `colour = palette[texel.r]` is unavailable because the PICA cannot execute arbitrary fragment code or dynamically read a palette texture as a function of a prior texture sample. Avoid designs that assume native paletted PICA textures.

**Implementable alternatives:**

1. **Expand each DS tile (or atlas page) to RGBA5551/RGB565/RGBA4444 when its source bytes or palette actually change.** Reuse the output across scroll, camera movement, and redraws.
2. **Cache by tile graphics + effective palette bank + colour-effect mode** and keep per-tile/page dirty generations; when one palette entry changes, update only the pages referencing it (or conservatively all of that palette bank).
3. For frequently palette-cycling materials, compare a **small number of pre-expanded variants** and switch texture/page binding, instead of recomputing a full screen.
4. If many palettes change continuously, leave that region on the CPU **unless a measured batched upload path wins**.

The sprite atlas code already performs partial expansion. Generalize its **lifetime and invalidation** rules rather than building a new palette subsystem.

### 1.2 Formats and memory arithmetic

At 512×512, RGBA8 consumes 1,048,576 bytes; a 16-bit format such as RGBA5551/RGB565/RGBA4444 consumes 524,288 bytes. At 400×240, an RGB565 render surface is 192,000 bytes and RGBA8 is 384,000 bytes, **before depth/stencil, alignment, multiple eyes, and buffers**. Depth24/stencil8 is another 4 bytes per pixel when present.

- **RGB565:** opaque colour; good for backgrounds and many framebuffers, but differs from original DS 5/5/5 precision in the green channel. Verify conversion and blending.
- **RGBA5551:** useful for 1-bit transparency, tile art, silhouettes and sprite atlases.
- **RGBA4444:** more alpha levels at half RGBA8 storage, but only if fidelity tests accept its precision.
- **RGBA8:** effects requiring finer intermediate alpha/colour; pay only where necessary.
- **ETC1 / ETC1A4:** compressed storage for larger, photographic or replacement assets; these are **lossy** and should not replace bit-exact original pixel-art tiles or reference buffers.

Power-of-two padding, wrap behaviour, UV edge gutters, flipped V coordinates, 8×8 tiling, target dimensions, and cache flushes are part of the data contract. Do not infer pixel layout from a host RGBA array.

## 2. How a normal 3DS game addresses PICA200

A commercial game generally owns its **GPU-native assets and render graph**, not a DS hardware emulator. A typical homebrew implementation exposes the same broad path using **libctru + citro3d** (and optionally citro2d):

```text
Game scene graph / animation / materials / cameras
    -> CPU culling & draw-record construction
    -> GPU-native geometry buffers + GPU tiled textures in GPU-accessible memory
    -> PICA vertex shader program (.v.pica -> picasso -> .shbin)
    -> per-pass state (depth, stencil, TEV, blend, textures, uniforms)
    -> GPU command list (register writes + draw arrays/elements)
    -> GSP GPU queue / PICA command processor
    -> vertex -> triangle setup -> rasterization
    -> texture sampling / lighting LUT / TEV combiners
    -> alpha + stencil + depth + blend
    -> tiled render target
    -> GPU display transfer / optional colour conversion
    -> display framebuffer -> GSP buffer swap -> LCD at VBlank
```

**What is sent down the wire?** PICA commands are **register writes** with a parameter and header (minimum 8 bytes), sometimes additional words. They configure shader attributes, texture addresses, TEV stages, stencil/depth state, vertex/index sources, and issue `DRAWARRAYS` / `DRAWELEMENTS`. This is not an OpenGL or DS G3 instruction stream once it reaches the GPU. [3dbrew register layout](https://www.3dbrew.org/wiki/GPU/Internal_Registers).

**Division of responsibility:**

- **Game/rendering system:** scene graph, draw order, texture residency, transformations, visibility, pass graph, frame-to-frame changes.
- **citro3d/libctru:** configure buffers/programs/TEV/targets, construct PICA register-command lists, manage GSP/display transfer operations.
- **PICA200:** vertex work, triangle rasterization, texture samples, lighting LUTs, fixed-function fragment combining, depth/stencil/blending.
- **GSP transfer/display:** move/convert completed images, present to the two LCDs, manage synchronization.

The user program generally **does not poke physical GPU registers itself**; it uses GSP-mediated interfaces or carefully constructed commands. Low-level raw register work is useful for debugging or targeted unexposed hardware features, not a default shortcut past citro3d.

### 2.1 A normal minimal render frame (conceptual)

```c
// PSEUDOCODE: not a patch to main_3ds.c; the real compositor has
// its own double-buffered command-list and GPU-flight design.

begin_frame_when_previous_resources_are_safe();

for_each_visible_render_pass(pass) {
    select_render_target(pass.target);
    set_viewport(pass.viewport);
    set_shader_program(pass.vertex_shader);
    set_vertex_attributes_and_buffers(pass.meshes);

    for_each_ordered_draw(pass.draws) {
        bind_texture_only_if_changed(draw.texture);
        set_TEV_and_depth_stencil_alpha_only_if_changed(draw.material);
        update_uniforms_only_if_changed(draw.transform);
        draw_indexed_or_arrays(draw);
    }
}

flush_only_CPU_modified_GPU_input_ranges();
submit_GPU_commands();
await_or_fence_only_where_resource_reuse_requires_it();
transfer_finished_target_to_LCD_framebuffer();
present_at_VBlank();
```

This is the *design*, not a request to use `C3D_FrameBegin()` in the port's display thread. **`port_compose_pica.c` and `port_gx3d_pica.c` already implement a customized, overlapping two-flight command queue because a standard citro3d begin can block on the prior GPU frame.** Replacing that with a simple official example may regress throughput or introduce races. Read [citro3d renderqueue.h](https://github.com/devkitPro/citro3d/blob/master/include/c3d/renderqueue.h) and the local flight-ownership code together.

### 2.1.1 Actual citro3d calls: a textured quad batch

The following are **real API names** from devkitPro's [textured-cube example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/textured_cube/source/main.c) and [TEV header](https://github.com/devkitPro/citro3d/blob/master/include/c3d/texenv.h), shown as a **fragment of an already initialized render pass**, not a standalone buildable program:

```c
// Once per compatible material run, after binding the intended program,
// vertex attribute layout, vertex buffer, target and depth/blend state.
C3D_TexBind(0, &atlas_page);

C3D_TexEnv* env = C3D_GetTexEnv(0);
C3D_TexEnvInit(env);
C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR,
              GPU_PRIMARY_COLOR);
C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

// The shader must output GPU_PRIMARY_COLOR, position and UV attributes.
// Vertices in this run must be contiguous and in DS-correct draw order.
C3D_DrawArrays(GPU_TRIANGLES, first_vertex, vertex_count);
```

This performs **texture × interpolated vertex colour**, not a palette lookup. Correct alpha-test and blending states, other TEV stages, scissor, depth/stencil and shader state must be configured by the surrounding pass. **In `hgss-3ds`, use the existing `bind_tex` / TEV-state dedupe wrappers instead of pasting these calls into every quad.** Each extra bind can flush PICA texture cache or enlarge the command list. `C3D_TexUpload` expects GPU-compatible texture data/layout; inspect the current port's swizzle/conversion functions rather than sending row-major DS tile bytes directly.

The official introductory route to a complete program is [devkitPro simple_tri](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/simple_tri/source/main.c): `gfxInitDefault` → `C3D_Init` → `C3D_RenderTargetCreate`/`SetOutput` → shader/buffers/TEV setup → `C3D_FrameBegin` → `C3D_RenderTargetClear`/`C3D_FrameDrawOn` → uniforms/`C3D_DrawArrays` → `C3D_FrameEnd`. Again, **HGSS's existing double-flight renderer has stricter queue ownership than this teaching example**.

### 2.2 The six TEV stages as a restricted material language

A useful HGSS 2D material vocabulary might be:

| Material | TEV / hardware concept | Limitation |
|---|---|---|
| Opaque tiled map | texture × vertex/constant colour | Palette must already be expanded |
| Sprite with cutout | texture + alpha test, then alpha blend if needed | Preserve DS priority and transparent index semantics |
| Day/night field tint | multiply by a constant or per-vertex colour | DS brightness math/rounding is not necessarily identical |
| Fade to black/white | constant-colour blend, full-screen overlay or TEV operation | Full-screen draw adds fill; state-only variants need tests |
| Water shimmer | animated UVs, texture scroll, optionally second texture with TEV | Underlying tile/map effects may need software fallback |
| Highlight / light mask | base texture × brightness/light texture, optional add | No arbitrary per-pixel branch or dynamic palette lookup |
| Soft shadow | translucent textured ground decal/ellipse | Use depth ordering to avoid hovering/double shadows |
| Fog | depth-based hardware fog LUT or textured overlay | DS fog/depth rules may differ; verify rendered scene |

Do not rebuild a CPU 400×240 RGBA image just to change a tint or scroll offset if the scene can be expressed by one shader constant or UV matrix **and** this is pixel-correct for the active DS display mode.

### 2.3 A precise distinction: which work belongs in the vertex shader?

A native PICA vertex shader can do ordinary model/view/projection transforms; for a single quad, it can transform screen positions and UVs. This saves CPU work when transforming many vertices and can allow reusable object-space buffers. It **cannot** exactly emulate every DS 3D fixed-point overflow/rounding/w-buffer/clipping/polygon rule automatically. The current port puts DS G3 geometry into screen-space GPU vertices on purpose. A proposed GPU-transform fast path must be **class-gated** and compared to the CPU reference in camera/near-plane, clipping, lighting, alpha, and edge-marking cases.

## 3. What `hgss-3ds` does today: audited source-to-pixel path

```text
HGSS game + NitroSDK/NitroSystem (DS APIs)
    |
    +-- DS G3 command FIFO / display lists
    |     -> 3ds_port/nitro/src/port_gx3d.c
    |        (DS command decode, matrix state, CPU transforms,
    |         clip/cull, shade, scene staging)
    |     -> 3ds_port/game/app/port_gx3d_pica.c
    |        (build draw batches, convert/cache texture, allocate/flush,
    |         TEV/depth/fog/stencil and GPU draw commands)
    |     -> PICA200 3D render pass
    |
    +-- DS BG/OBJ/window/blend/HBlank emulation
          -> 3ds_port/nitro/src/port_render2d.c
             (software scanline/layer renderer; partial GPU BG/OBJ selection)
          -> split UNDER and OVER layers + retained/quads metadata
          -> 3ds_port/game/app/port_compose_pica.c
             (publish dirty texture regions; GPU BG/OBJ quads;
              integrate 3D between layers, optional effects/stereo)
          -> final top 400x240 / bottom 320x240 frames
          -> display queue / VBlank
```

See source: [HGSS shell](https://github.com/estyseesghosts/hgss-3ds/blob/main/hgss/game/app/main_3ds.c), [G3 translator](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_gx3d.c), [PICA G3 backend](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_gx3d_pica.c), [software/part-GPU 2D](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_render2d.c), [compositor](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_compose_pica.c).

### 3.1 Already implemented; do not propose these as missing

- **Native PICA 3D:** `NitroPort_PicaPrepareScene`/`NitroPort_PicaDrawScene`, depth, stencil, opaque/translucent passes, fog support, optional edge/shadow passes; not a CPU 3D framebuffer in the normal remaster path.
- **GPU compositor:** upper 400×240, lower 320×240, split layering, GPU presentation, old/new-console configurations, GPU-flight queues, VBlank presentation, partial dirty uploads.
- **Selected GPU-drawn text/affine backgrounds:** a BG can be replaced with a textured GPU quad while eligible; its scroll is then UV-based rather than requiring full CPU rasterization.
- **Selected OBJ quads:** an atlas and transparent gutters can make some sprite composition GPU-native.
- **3D texture decode/tile/cache:** PICA texture cache with a **configured 256-entry / 8 MiB cap** and LRU policy in `port_gx3d_pica.c`; texture binding and TEV state are already deduplicated in places.
- **Software 2D optimizations:** row caches, change signatures, dirty-row publishing, fast opaque combines, partial engine-B redraw experiments.
- **Profiling:** frame/geometry/drawer/audio/publish counters, GPU-event callback profiling, texture statistics, command-buffer usage and numerous detailed scene counters.
- **HBlank switch fix:** thread switching was deferred across the synthetic scanline walk; don't reintroduce 192 scheduler handoffs per frame.

### 3.2 Incomplete or unproven

| Gap | Current evidence | Specific next engineering task |
|---|---|---|
| **Full GPU-native DS 2D** | `port_render2d.c` still does CPU tilemap/OBJ/window/merge/scanline work; GPU BG/OBJ takeover limited to safe cases | Extend **specific proven modes**; do not assume GPU 2D is absent |
| **Engine B / bottom screen fast path** | Lower content frequently still CPU-rasterized, plus partialB/band publishing experiments | Retained GPU-native bottom BG/OBJ pages or efficient partial-band fallback; test touch/menu transitions |
| **Remaining engine A eligibility** | BG takeover requires active 3D BG0 and rejects windows/HBlank/effects/modes; many scenes fall back | Report reasons and add independent compatibility classes, one by one |
| **Battle GPU BG correctness** | **Real hardware** mirrored some battle dialogue when BG quads took over; patch 1910 adds a battle-active veto | Diagnose from *same-frame full VRAM/OAM/palette/GSP input*; keep veto until hardware test proves exact |
| **G3 geometry translation overhead** | CPU transforms/clips/normal lighting; Goldenrod field has thousands of vertices and many clipped | Reduce repeated FIFO interpretation and clipping for stable lists; only then prototype restricted native mesh path |
| **Effective indexed vertex reuse** | Experimental compaction often produces **identical** compact vs original vertex bytes plus indices | Retain array submission where no beneficial reuse; do not sell indexes as automatic savings |
| **Permanent tile/OBJ invalidation graph** | Mostly per-frame hash/read/expanded atlases and conservative fallback logic | Add page-level generations/invalidation tied to writes/DMA and compare with full hashes in diagnostic runs |
| **Texture residency vs memory pressure** | Old 3DS heap improvements already freed about 7.6–8.1 MiB for textures in a measured session | Formal budget per cache/ping-pong target/eye and OOM fallback; avoid hidden allocations |
| **Native-quality wide-field mode** | Phase A uses 320×192 DS-space top canvas scaled to 400×240; 400×240 1:1 field remains plan | Implement per-field only after camera, map, visibility, script and collision bounds review |
| **HGSS-specific wide menus and UI** | Platinum remaster compositor ported; HGSS bag/party/PC/battle layout/overlay handling are separate work | Build screen-specific layout tables and visual captures, not generic GPU refactor |
| **Real hardware optimization gate** | Dated logs show bottlenecks; several newest changes only Azahar/fixtures or pending hardware A/B | Freeze revision, save, path, runtime flags, screenshots and measure matched hardware runs |

**Status caveat:** `docs/wiki/Architecture.md` and other early pages contain older statements such as “classic 1:1 renderer” and “GPU 2D not started”. Those statements are superseded for Phase A and later GPU quad work by the code, README, dated performance notes and top of `STATE.md`.

## 4. Bottlenecks: use the project's measurements, not intuition

The following are **historical per-session measurements, not a unified Nov 9 baseline**:

| Test | Observed | Interpretation |
|---|---|---|
| Old 3DS post-S20 field | **10–14 composed fps**; Goldenrod outlier **5–6** | Not a raster triangle limit proved by measurement |
| Old 3DS drawer | **55–81 ms**; render **39–59 ms**; publish **16–23 ms** | Software 2D + texture publication dominate that run |
| Old 3DS geometry worker | **19.9–24.7 ms/scene**, and a high-clipping Goldenrod scene worse | Full-rate 3D scenes need reduced translation or reuse |
| New 3DS S20 | Game around 60 ticks/s but **30–32 composes/s**, upper drawer ~5.7 ms | Core contention and optional rendering cost still matter |
| New 3DS 2× smoothing | About **6 ms GPU/frame** in the cited heavy-area profile | Optional quality is not free; disable before starving presentation |
| Azahar Nov 6 experiment | **60 game ticks/s**, **~23–24 presented fps** after cache changes at Old-3DS model/80% clock | Emulator result; no hardware guarantee |
| Goldenrod G3 FIFO list table (Azahar) | Hash table 256→1024 changed ~13.7→22.6 emulator composes/s | Command identity and worker queueing mattered; don't generalize to hardware until measured |
| Indexed stream (device diagnostic) | Identical compact/original bytes plus index overhead; ~1.3 s wall / 120 preparations | Index fast path is often wasted work in this workload |

Read [S20 hardware report](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s20-performance-session.md), [Half A session](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-session-2026-11-06.md), [Azahar 30fps campaign](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/az30-campaign.md), and [`STATE.md`](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/STATE.md).

**Never compare a CPU timing captured inside an emulator with real-hardware wall time as though they were the same measurement.** Label sample origin, hardware mode, clock, core placement, debug flags, and frame interval on every result.

## 5. Practical 2.5D JRPG renderer: exploit what barely changes

An HGSS frame is full of repeated **tile graphics, palette banks, camera scroll, small sets of character poses, translucent effects, windows and text**. These are ideal for caching. Think in **content-addressed GPU resources** rather than output pixels.

### 5.1 Tile backgrounds: make a scroll cost only a uniform/UV change

**Current problem:** on many software-rasterized paths, a one-pixel camera scroll changes almost every displayed screen pixel even though the underlying tile data is identical. Recomputing all scanlines is needless if the material/priority mode is GPU-safe.

**Target representation:**

```text
DS source: char blocks + screen map + BG palette + BG control/scroll
    -> decode changed tiles into RGBA5551 atlas pages (CPU only on dirties)
    -> keep a GPU-native page/resource map, with ownership and generation
    -> draw one or a small number of stable map quads/segments in PICA
    -> change UV offsets for scroll, not tile pixels
    -> redraw/decode only tiles/pages actually changed by game/DMA
```

**Implementation details:**

1. **Inventory BG modes by frequency:** text BG, affine BG, large-map, bitmap, palette modes, extended palette, mosaic, scroll, and BG-priority relationships. Record real frame counts and GPU takeover reject reasons.
2. **Version source ranges, not just a global “VRAM changed” bit.** A char tile dirty bit, palette-bank generation and map entry generation allow targeted invalidation. Instrument every path that changes DS VRAM (CPU stores, DMA, LCDC mapping, register-driven copy, overlay update). If a write cannot be observed, conservatively invalidate the affected bank.
3. **Track aliasing and bank mapping.** A DS VRAM bank remap invalidates which source address a tile refers to; pointer equality does not imply unchanged content. Extended palette bank ranges must be correctly masked. Prior experiments found real alias/hash bugs.
4. **Make atlas page identity include the effective palette.** A tile's bits are not its colour. A 4-bpp tile using palette bank 4 is a different GPU texel resource from the same tile using palette bank 2.
5. **Preserve tile gutters.** Clamp atlas-UV sampling to texel centres for nearest; for bilinear filter, pad/duplicate edge texels per sprite/tile to prevent neighbour bleed. Do not assume 1.25× scaling with nearest has uniform pixel widths.
6. **Keep map quads in DS BG priority order.** If the BG sits between 3D and OBJ, place it in the correct compositor split/pass; merely drawing it before or after all 3D changes the game.
7. **Start with a conservative eligibility class:** opaque text BG, stable priority, no per-scanline register changes, no DS window/mosaic/colour effects and no captured/bitmap source. Expand classes only after side-by-side replay.
8. **When the map is larger than a GPU texture**, segment into pages/chunks. Use repeated UV within power-of-two wrapping only when valid; do not pad a repeating 256-wide tilemap to 512 and expect identical wrap periods.
9. **Count both upload and draw costs:** decoded tiles/frame, atlas pages written, bytes flushed, bind calls, draw calls, PICA time, composition latency, memory high-water.

**The largest speed win is not reducing one multiplication per screen pixel. It is removing the whole per-screen-pixel CPU pass on every scrolling frame.**

### 5.2 Sprites: use GPU atlases, but respect DS compositing semantics

The existing OBJ-atlas shortcut is valuable but incomplete. Next steps:

- Separate **source tile identity** from **sprite instances**. Ten NPC instances using one graphic share texels but require distinct positions and OAM transforms.
- Keep animation as **UV rectangle changes** for frames already resident, rather than regenerating all sprite texels.
- For CPU-origin DS OBJs, carry: priority, palette bank, transparency, OAM index tie-break, affine matrix, double-size, mosaic, OBJ-window, semi-transparency, and screen engine.
- Split sprites into **strict painter-order runs** where GPU material/state can be shared safely. If a priority tie or alpha interaction changes, end the batch. **No sorting by texture across an order-sensitive boundary.**
- **Pseudo-instancing:** there is no modern `glDraw*Instanced` contract to rely on. Encode multiple sprite quads in one vertex buffer/run; batch only when their page, TEV, clipping, render target and order permit it. Do not issue one `C3D_DrawArrays` per tiny quad when a safe contiguous run would do.
- **Keep wide OG sprites separate from G3 billboards:** on the field, character graphics may actually be DS 3D polygons, not OAM sprites. S20's diagnostics showed the engine-A OAM table empty while visible characters were drawn by the 3D path. Rewriting OAM alone will not fix character rendering.
- Any sprite overlap, shadow, polygon blending or stencil effect must preserve the existing draw contract. An apparently equivalent batch can still duplicate a polygon if it crosses a bound range or misses state restoration.

### 5.3 The lower screen is a major optimization target

HGSS's touch UI is often mostly static. It is wasteful to rerender and republish a complete 320×240 texture when only a small cursor, indicator, moving icon or number changed.

**Two viable paths:**

- **Incremental software reference path** (lower risk): existing `partialB`, `partialBChar` and `publish_bands` concepts can redraw only verified dirty scanline ranges and send those bands, with a full scanline render on unmodelled modes. Already partially implemented behind flags, but runtime coverage and hardware speed need proof.
- **GPU-native engine-B subrenderer** (higher reward): background tile pages and OBJ atlases stay on GPU; draw them to the lower render target with their own priority and effect state, without needing a 3D BG0 on engine A. The software renderer is fallback for display capture, per-line changes, bitmap modes, windows, unsupported effects or potential palette bugs.

**Do not clone engine A's BG eligibility test** into engine B: `frameOk` in `port_render2d.c` currently demands a visible 3D BG0. The bottom screen needs its own compatibility classifier, not a forced `has3d` flag.

**Retained scene/cached UI layer trick:** if a menu page is static, render the page once to a texture, then draw changed cursor/highlight/text over it. This is legal only if the DS effect stack (window, per-pixel blends, colour math) is identical to the cached image's state. State version changes invalidate the page.

### 5.4 Treat text as glyph updates, not full-screen colour updates

In RPG menus a few characters change far more often than the backdrop. For safe layouts:

- Atlas glyphs or tile text frames with stable palette keys.
- On an updated dialogue box, rebuild only tile rows that contain new glyphs; keep the background/frame unchanged if state permits.
- For typewriter effects, upload changed glyph tiles/ranges rather than a whole 400×240 composite.
- A held dialogue screen should be **near-zero CPU raster and near-zero upload**, unless another underlying layer legitimately changes.
- Changes in line-height, window clipping, DS BG char memory, cursor, input prompt, fade or palette must invalidate relevant tiles, not silently reuse stale text.

**Existing bug gate:** a prior direct-GPU BG path produced **horizontally mirrored battle dialogue on real Old 3DS hardware**. Its battle-active veto is an intentional protection, not accidental dead code. Do not turn GPU text batching back on globally as an “optimization.” Diagnose and pass this exact case first. [Battle validation report](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md).

### 5.5 Pixel-sprite art can remain pixel-sprite art

At a 3DS top display of 400×240, rendering original 256×192 DS sprites in a 320×192 logical scene and scaling by 1.25× fills 400×240. This **does not make the original pixels more detailed**. For a pixel-perfect world, a genuine **400×240 1:1 field viewport** can show more tiles and sharper 1:1 art; it requires careful camera/frustum/map/script extension. Keep UI at its intended scale when necessary.

Choose deliberate output policy by screen type:

- **Field:** test 400×240 1:1 world-space viewport when map limits, NPC culling and event triggers safely support it. No fake off-screen world content.
- **Battle:** retain stable layout, no casual texture rescaling that smears sprites; GPU-native 3D backdrop and battle Pokémon, correct priority and text.
- **Menus and title:** scale/layout/illustration per-screen; some images should use original resolution and clean side art rather than a single global bilinear rule.
- **Text/UI:** prefer intentional pre-expanded glyphs or sharp 1.25× over a game-wide smoothing filter that changes readability.

## 6. GPU rendering “cheats”: proven ideas and safe adaptations

These are **deliberate ways to avoid drawing expensive work**, not excuses to break HGSS visuals.

### Cheat A — A textured quad can replace thousands of rebuilt pixels

A tiled background or a pre-rendered static menu becomes a GPU texture on a quad. Scroll it by changing UVs or a transform. **Used/endorsed pattern:** [citro2d](https://github.com/devkitPro/citro2d) supports sprite sheets, tint, transforms, combined 2D/3D. **HGSS adaptation:** existing GPU BG-quads do this for a subset of compatible DS states. Expand proven cases instead of reimplementing the whole compositor.

### Cheat B — Pre-bake expensive colour decisions

**Developer case study: Jazz² Resurrection's 3DS PICA backend.** It uses CPU-transformed sprites with a passthrough PICA vertex shader, texture combiners for effects, a software-produced lightmap, cached palette-baked texture pages, Morton-swizzled native textures, and lightweight 16-bit surfaces. It also **flushes modified vertex ranges once per command list rather than per draw**, because a GSP flush request is expensive. [Primary implementation docs](https://de4th.dev/jazz2/docs/consoles.html).

**HGSS adaptation:** bake a constrained lighting or palette combination **on source/content change**, keep it resident, and draw as texture/surface. But do not draw/change a whole lighting texture every tick when a small TEV colour, vertex colour or constant can do it. Measure both approaches.

### Cheat C — Limited fixed-function materials can create rich effects

**Game Freak / Creatures case study (documented, not conjecture):** the Sun/Moon developers reported a bespoke engine for their game's draw workload with roughly **10% lower processing load** than their prior general path; their artists rebuilt constrained fixed-function material combinations for individual Pokémon. **Solgaleo** uses masked constant colours and line-style emissive textures; **Lunala** uses sphere/environment mapping with masks and animated multi-UV effects. [CGWORLD technical report, July 2017](https://cgworld.jp/feature/201707-cgw227GG-pokemon.html).

**HGSS adaptation:** water tint, sunlight shifts, crystal highlights, fog, night palettes, battle aura and Pokégear UI gleam can often be expressed through texture masks, TEV constants and UV motion. **Do not claim HGSS currently needs Sun/Moon's complex normal/shading maps:** our first priority is moving pixels off the CPU and maintaining DS appearance.

### Cheat D — Billboard instead of geometry

**Same CGWORLD report:** Sun/Moon battle effects were produced from pre-rendered image sequences, packed into textures, and displayed as billboards and particles. This avoids simulating a large volumetric effect. A 2.5D JRPG can use the same general trick for smoke, dust, sparkles, hits, weather and glow.

**HGSS adaptation:** when adding optional visual polish, favour a small quad with cached animation frames, correctly depth-sorted or draw-order staged. Existing retail DS effects must remain faithful before adding replacement animation.

### Cheat E — Software references can stay, but render targets should be GPU-owned

**Developer case study: P3P3DS's documented PICA GE translation.** Its 3DS backend describes a content-hashed texture cache, Morton swizzling, source-format→GPU-format expansion, GPU render targets and treating texture-backed render targets specially. Its technical report correctly highlights the cost of re-expanding indexed sources on every bind. [Project documentation](https://github.com/endercodezz/P3P3DS/blob/master/docs/3DS_PLATFORM.md). Those are **self-reported project implementation notes**, not independently verified performance results.

**HGSS adaptation:** own GPU-resident textures for retained BG/OBJ pages, treat content changes distinctly from camera scroll, and never overwrite resources while an in-flight GPU command list can still sample them.

### Cheat F — Precompute static geometry/list commands

The port's `HGSS_GX3D_LISTCACHE` experiment recognizes repeat display lists and can skip repeated hidden-vertex scans. In the Azahar Goldenrod campaign, changing cache table size from 256 to 1024 materially improved emulator throughput. **The opportunity:** static world geometry can be retained or fast-pathed by immutable content plus transform/version identity.

**Do not use raw pointer identity alone.** Memory allocators recycle pointers, display-list contents can be overwritten in-place, and DS matrix/material state changes meaning. Key on immutable identity or versioned source + length + relevant state, check mutation and include a **full audit/invalidation** mode. The experimental list cache already documents these hazards.

### Cheat G — Cheap shadows and lighting

A small translucent oval/sprite under a character is often more appropriate than a high-resolution dynamic shadow map; depth testing stops it drawing through solid world geometry. Day/night tint through TEV/vertex colours can replace a pixel-by-pixel CPU overlay. However, the existing port already has **optional shadow machinery** and some DS scenes paint their own shadows: avoid drawing a second shadow on top. Test camera height, stairs, contact points, and alpha blend.

### Cheat H — Render only what changed, but track hidden dependencies

Dirty regions can be vastly smaller than screen size in menus; background scrolling still changes the whole *visible* image, but not tile *content*. Source-content invalidation and screen-space changed rectangles are different optimizations. The engine-B band test reports 392 vs 4,800 rasterized scanlines in its synthetic sequence, but that is a **fixture**, not a demonstrated 12× whole-game speedup. [Half A lane L report](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-laneL-wave2.md).

### Cheat I — Keep a 60 Hz game clock and decouple present rate

When two LCD outputs have different update needs, consider 60 game ticks, 30 lower-screen publishes, and 30 or 60 upper-screen presents as **separate, explicitly named clocks**. The prior Azahar `lower30+drawerAB` experiment cut lower render/upload work and improved compose rate while retaining game ticks. Verify Old 3DS actual touch/UI responsiveness and hardware time before adopting.

### Cheat J — Save bandwidth before chasing shader arithmetic

Using an appropriate 16-bit target/texture can halve traffic versus RGBA8. Avoid needless PPF colour conversion/readback and do not flush an entire 512² atlas when one tile row changed. But beware the traps: a small number of giant `GSPGPU_FlushDataCache` requests may outperform thousands of tiny calls; benchmark **batching requests** and total flushed bytes. Memory bandwidth and GSP submission overhead matter as much as pixel format.

## 7. Native 3D path: where the current PICA utilization is weakest

### 7.1 Why the triangle count is misleading

The 3D viewport draws polygons on hardware. Before PICA receives them, however, `port_gx3d.c` interprets DS display-list commands, maintains DS matrix stacks, applies fixed-point transformations, calculates normals/lighting, clips polygons, expands triangle strips, performs DS-specific depth mapping and hands over a scene. `port_gx3d_pica.c` then builds draw batches, normalizes depth and flushes GPU vertices.

A few thousand triangles can therefore cost **tens of milliseconds of ARM11 geometry-work** even when PICA renders them quickly. The measured Goldenrod scene had roughly 10,700 input vertices and 2,784 clipped ones in one earlier report; those counts do not represent PICA fill-rate pressure.

### 7.2 Optimize in this order

1. **Measure G3 commands per scene:** packet words, source-list identities, BEGIN/END runs, normal+matrix commands, vertices transformed, vertices clipped, invisible lists scanned, batches issued, and worker wait/backpressure.
2. **Avoid repeated command interpretation** for immutable display lists. The near-term target is source-validated caches of *command runs* and conservative skip of *hidden* runs; build on current G3 experiment rather than duplicate it.
3. **Cache transformed geometry only with complete dependencies:** source content, DS matrices/stack generation, projection/view, lighting/material, viewport, fog/depth mode and geometry semantics. Separate an *object-space mesh cache* from a *screen-space transformed scene cache* to avoid invalidation errors.
4. **Move object-space transforms to a PICA vertex shader only for a verified class** of meshes if a controlled A/B shows a win. Start with static map meshes with ordinary projection and no tricky DS clipping or vertex colour changes; keep the DS CPU transform reference for all other geometry.
5. **Reject nonbeneficial indexing early:** before copying/compacting, sample whether local identical vertices are genuinely available. The existing bit-exact near-zero-reuse results warn against blindly building index buffers.
6. **Cull early, not late:** object bounds → view frustum, then if reliable skip the entire display list. Culling must include any sprite cards or geometry extending outside the usual bound, off-screen shadow effects, and objects visible in widened field mode.
7. **Avoid GPU state churn:** run safe adjacent batches with cached texture/TEV/depth/stencil state, hoist texture lookups/decodes. Do not reorder blending or merge batch ranges without a formal equivalence proof.
8. **Revisit numerical precision only with proof:** software double→float yielded real gains already. PICA float24 vs DS fixed 20.12 rules can move clipping, edges and depth. Never blindly `-ffast-math` across the renderer.

### 7.3 DrawElements is not a magic fix

An indexed buffer helps only if many triangles share *identical post-transform and post-attribute vertices*. Perspective/depth normalization, per-triangle expansion for edge coverage, texture coordinates, opacity/material, and DS polygon ID can make two spatially equal corners **not identical GPU vertices**. The observed index experiment copied as many vertex bytes as the unindexed path and added index bytes. Keep `C3D_DrawArrays` as the default where that is true.

### 7.4 How to prototype a real GPU-transform fast path

**Isolate one repeatable mesh class** (e.g., static opaque architecture):

- Read the original DS positions/texcoords from a validated display-list entry once.
- Compile persistent object-space vertex/index data in GPU-compatible layout.
- Maintain a minimal PICA vertex program with model/view/projection matrices; output texture coordinates, colour, and depth needed by the existing fragment TEV/depth contract.
- Compare GPU PICA output against current translated reference for wide/off, stereo/off, near-plane crossings, extreme transforms, depth equal tests, lighting, texture animation, and off-screen boundaries.
- Measure **worker CPU ms saved minus GPU command, shader/vertex cost and cache pressure**; include all required conversion work.
- Fall back immediately on incompatible commands, mutation, unusual matrix mode or unproven blend/lighting state.

This is a **targeted native-path experiment**, not a reason to remove G3 emulation. The DS command stream may not expose reusable immutable mesh resources as conveniently as a retail 3DS asset engine.

## 8. Materials and effects: a scene-specific plan

| HGSS use case | PICA-efficient treatment | Fidelity gate |
|---|---|---|
| Overworld buildings / ground | Cached 3D map geometry + normal depth/stencil; GPU BG tile quads where correctly ordered | Near-plane, sprite overlays, scroll seams, alpha cuts |
| 2.5D character billboards in world | Cached textured 3D cards, optional vertex transform, nearest sampling | Frame animation, transparency, shadow, priority, terrain elevation |
| Water / animated terrain | Scroll texture UV and atlas-frame change; optional second TEV mask | Tile animation schedule and palette effects |
| Weather overlays | GPU quads/particle sprites or cached full-screen image sequence | HBlank windows, fades, palette changes, translucency order |
| Time-of-day | Palette variant selection or TEV tint when mathematically equivalent | DS 5-bit colour/brightness rules |
| Dialogue/menu | Retained frame + dirty text tile updates; GPU quad display where proven safe | Battle mirror veto, font edges, windows, blend |
| Pokémon battle images | GPU-native textured polygons already present; ensure texture residency and valid G3 decode | All Pokémon visible, no clipped/zero-W polygons, animation timing |
| Simple battle transition | Prebaked alpha mask, screen quad, TEV/constant fade | Per-line/hardware window effects may force software |
| Shadows | Reuse painted DS shadows or low-cost decal before optional dynamic maps | No duplicated halos, correct depth and terrain placement |
| Stereo | Draw second eye only if enabled/required, reuse safe textures and geometry | Correct camera convergence, stereo slider, GPU/framebuffer memory |

**Avoid making the port more visually elaborate before its existing effects are correct.** The first goal is to make each correct frame cheaper, not to add bloom, shader postprocessing, geometry shader particle systems or high-resolution 3D effects.

## 9. Draw order and DS exactness: the correctness contract

The port cannot freely batch by texture/material if that changes order. DS engine A's 3D surface is one BG competitor among priority layers, and the DS blends/compares certain translucent polygons using polygon IDs and stencil-like rules. The existing renderer uses opaque passes, a separate alpha-31 path, translucent passes and edge-marking copies for reasons.

**Prior failure you must not repeat:** a previous optimization merged opaque batches across a texture change and relaxed stencil state, causing **duplicate NPCs and rectangular halos**. It was reverted. The problem was not “sprites drawn by the wrong 2D renderer”: the visible objects were 3D geometry. [S20 postmortem](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s20-performance-session.md).

Every GPU takeover decision needs a **material and ordering equivalence declaration**:

```text
Eligibility = same source content/snapshot
           && supported display mode and bank mapping
           && known source palette and transparency semantics
           && same effective DS priority/tie-breaking
           && same windows/alpha/mosaic/brightness behaviour
           && same 3D-under/over ordering
           && safe immutable GPU resource lifetime
           && tested shader UV and orientation
```

A **fallback reason enum** should identify the failed clause. If unsupported, draw the affected region through the reference path, not an approximate replacement. Record evidence for the top ten rejection modes before adding a more general fast path.

### Exactness traps

- **Snapshots:** `NitroPort_Render2DTakeState` and `NitroPort_ComposePublish` separate game writes from drawer/display reads. Do not hash a live pointer from one phase and draw a snapshot from another.
- **Cache generations:** source pointer reuse, bank remap, palette changes, transparent index, affine map byte size, extended palettes, OAM attr bits, window state, HBlank events and brightness are all inputs.
- **Uploads:** CPU must flush modified GPU-visible memory before PICA reads it; never rewrite an in-flight page/vertex buffer. GPU completion fence/serials must be part of resource ownership.
- **Framebuffer double buffering:** not changing the source image does *not* imply the LCD's next buffer already contains it. Reuse retained complete frames or redraw presentation as required.
- **Texture V orientation:** PICA texture orientation can appear upside-down relative to conventional CPU linear arrays. A prior **battle text mirrored horizontally within one region** demonstrates why screenshots and exact UV/data captures are indispensable.
- **Pixel filter:** 1.25× scaling with nearest causes uneven 1- and 2-pixel widths; with bilinear it can soften edges. Preserve user-selected policy and check every seam/gutter.
- **GPU hazards:** shaders must write configured output attributes correctly; 3dbrew documents GPU freezes from consecutive `MOVA` instructions and malformed vertex attributes. Do not tune shader microcode without validating on hardware.
- **GSP fill/transfer:** framebuffer clears, render-target format conversion and transfers are separate GPU operations with synchronization and cache consequences; a CPU screenshot buffer is not authoritative before the transfer finishes.

See [3dbrew GPU pitfalls](https://www.3dbrew.org/wiki/GPU/Pitfalls), [PICA internal registers](https://www.3dbrew.org/wiki/GPU/Internal_Registers), and [3DS display transfers](https://www.3dbrew.org/wiki/GPU/External_Registers).

## 10. Specific guide for the implementer: work packets

Each packet below is intended to become a **bounded agent assignment**. The repo's [`AGENTS.md`](https://github.com/estyseesghosts/hgss-3ds/blob/main/AGENTS.md) requires objective, allowed paths, deliverables, acceptance checks, avoidables and a stop condition. **Read `gameplan.md` and `docs/progress/STATE.md` before touching code.** Do not assume a work packet is automatically safe to parallelize with another that edits the same renderer file.

### Packet A — Establish a hard GPU/CPU accounting baseline (**first; lead-owned**)

**Objective:** Create a reproducible, matched hardware measurement over Goldenrod and two correctness-heavy scenes (field→battle dialogue and animated lower menu).

**Read:** `hgss/game/app/main_3ds.c`, `port_compose_pica.c`, `port_gx3d_pica.c`, `port_render2d.c`, `docs/progress/STATE.md`, dated benchmark notes.

**Allowed edits:** diagnostic counters, benchmark driver/scripts, documentation. No rendering behaviour.

**Deliverables:** frozen save hash, input script, build SHA and flags, exact hardware model/clock/memory mode, per-scene packet/vertex counts, G3 CPU stages, CPU drawer BG/OBJ/blend/setup, submit/command buffer and GSP flush count, GPU draw+PSC+PPF times, published/complete/dropped frames, per-screen output age and core-idle attribution.

**Acceptance:** independent 60 Hz game tick count; normal and instrumented runs compared; same input replay/frame digests; screenshots; clean exit; no rendering regression. Hardware matched A/B must use *the same* revision and save, differing by the measured single variable. Existing profilers can be reused; instrumentation must be bounded to avoid changing scheduling.

**Avoid:** screenshots alone as a correctness oracle for moving scenes, Azahar wall time as Old3DS timing, and full-frame logging every tick. **Stop** if event callback overlap/timing attribution cannot be calibrated.

### Packet B — Fix the battle GPU-BG/text mirror before widening eligibility (**correctness blocker**)

**Objective:** Identify why text rows flip in the GPU quad path; preserve current battle-active veto until a hardware-verified replacement exists.

**Read/edit:** `port_render2d.c` GPU BG nomination and text decode, `port_compose_pica.c` GPU quad builder/draw, `hgss/game/app/main_3ds.c` instrumentation, focused test fixtures. Patch stack only if a game-level hook genuinely requires one.

**Deliverables:** synchronized capture of CPU source IO/BG/OBJ/OAM/palette/LCDC state, texture bytes **after swizzle and before GPU read**, UV/vertex data, TEV/stencil/blend state, GPU output and a single battle transition frame where the issue reproduces. A test asserting correct left-to-right text rows.

**Acceptance:** visual/digest equivalence in field, battle entry, command menu, dialogue transitions on **Old 3DS**; no extra uploads/misordered quads; veto removed only for the exact class proved safe. **Stop** if capture state is from different frames or no hardware reproduction exists.

**Avoid:** changing arbitrary flip flags, trusting the old incomplete VRAM capture, or removing the battle veto to gain fps.

### Packet C — Extend retained BG content and invalidation (**largest structural win**)

**Objective:** Preserve decoded tile/char graphics across scroll while sending only safe BG quads; establish page-level GPU texture correctness.

**Read/edit:** `3ds_port/nitro/src/port_render2d.c` BG decode/nomination and cache, `3ds_port/game/app/port_compose_pica.c` BG texture residency/upload, new small dedicated `3ds_port/nitro/src/...` helpers if justified, focused tests. Keep experimental code isolated from shared hot loops until measured.

**Deliverables:** source/atlas page key and version contract, dirty tile/page producer hooks or conservative hash fallback, full vs partial upload byte counts, cache hit/eviction metrics, explicit GPU eligibility matrix, fallback reasons.

**Acceptance:** same-state bit-exact software-vs-GPU for opaque text and affine BGs, VRAM bank swaps, static/scroll, palette edits, 4/8bpp, transparent cases, and prior clipping/camera repro. Old3DS real hardware: reduced CPU text BG raster and fewer flushed bytes *without increasing GPU or total latency*. Test emulator separately.

**Avoid:** claiming PICA samples raw DS paletted textures; a whole-frame hash that costs more than a redraw; reallocating/pinning huge 512² surfaces unnecessarily; taking battle UI before Packet B.

**Stop:** if a renderer snapshot or dirty generator has unknown untracked writers.

### Packet D — Lower-screen dirty ranges and optional GPU-native 2D (**high value, separate ownership**)

**Objective:** first certify existing `partialB/partialBChar/bandPubB` on hardware; then decide if GPU-native engine B is worth its extra render passes and memory.

**Read/edit:** `port_render2d.c` engine B, `port_compose_pica.c` under/over publish, `main_3ds.c` config/telemetry, tests. **Cannot parallel-edit `port_render2d.c` with Packet C** without a serialized integration plan.

**Deliverables:** per-reason full/band/unchanged attribution, changed tile→screen scanline mapping, palette/OAM invalidation, published bytes, correct 30 Hz lower presentation experiment with 60 Hz game logic, dynamic UI correctness fixtures.

**Acceptance:** identical lower-screen and touch behaviour across menu cursor/text, Pokémon party, bag, Pokégear, battle UI, fades, HBlank/mosaic, row reuse, disable/enable and LCD swaps. Measurable Old3DS reduction versus a *matched* baseline.

**Avoid:** mistaking 0 band-takes for successful acceleration (older reports observed 0), or inferring hardware gains from 12× synthetic scanline reductions. **Stop** when fallback reason accounts for most workload; profile it before adding complexity.

### Packet E — G3 display-list fast path, not global renderer rewrite (**geometry bottleneck**)

**Objective:** reduce repeated DS command interpretation and transforms for stable world geometry, preserving exact DS state semantics.

**Read/edit:** `port_gx3d.c`, `port_gx3d_pica.c`, `nitro_port_gx3d_vertex.h`, independent test fixture(s). Coordinate with Packet A on telemetry; no simultaneous unrelated edits of PICA backend.

**Deliverables:** packet provenance, per-list version key, cache hit/miss reason, bounded scratch memory, full content audit/allocator invalidation, eligible hidden run skips, transformed vertex counts and result digests, emergency-disable flag.

**Acceptance:** existing G3 unit tests, host reference comparisons, 12k synthetic runs including mutation/heap reuse, battle/field visual golden, and **measured hardware** less worker CPU time and more completed scenes. If index compaction doesn't shrink vertex stream, choose `DrawArrays` and report no speed benefit.

**Avoid:** opaque batch merging, stencil changes, stale matrix reuse, skipping visible geometry, unbounded hash scanning, fixed-point→float changes without tests. **Stop** if a cache key cannot account for mutation or a known DS command side effect.

### Packet F — Prototype GPU vertex transforms on one mesh class (**conditional, after E**)

**Objective:** determine whether native object-space vertices + PICA matrices save enough ARM11 work to justify per-class path.

**Read/edit:** add one dedicated `.v.pica` and bounded wrapper; do not change every DS G3 call; maintain reference pipeline and active pass.

**Deliverables:** class eligibility proof, shader binary build, working sample fixture, exact/nonexact pixel and depth diff reports, GPU-vs-CPU time table and automatic fallback.

**Acceptance:** deterministic scene geometry/edge clipping, fog, shadows and depth on Old/New 3DS; net speed gain with GPU memory and command costs included. **Stop** if shader float24/depth differ in a way that makes DS scenes visibly incorrect or GPU becomes slower.

**Avoid:** assuming the fourth vertex processor always exists as a vertex lane when geometry shaders are in use, and “GPU = free maths.”

### Packet G — Wide-field and native-layout polish (**after renderer stability**)

**Objective:** make the 400×240 viewport useful for the field, and give UI explicit layout rules without violating gameplay or visibility.

**Read/edit:** HGSS-specific shell, wide-view hooks and screen UI/layout code through numbered port-only patches where required; do not directly mutate generated `build/upstream` or vendored `3ds_port/` without upstream-style patch bookkeeping.

**Deliverables:** field camera and map-edge test matrix, newly visible entity culling/script behaviour, explicit field/menu/battle render modes, untouched DS logic/touch semantics and screenshot comparisons.

**Acceptance:** representative maps (Goldenrod/outdoors/indoors/caves), trainer approach and scripted entrance scenes, collision and NPC visibility, save/load transitions, text boxes and battle. **Stop** on event logic changes or content pop-in until a game-owned camera/visibility rule is defined.

## 11. Test gates and how to know a rendering improvement is real

### 11.1 Three independent correctness oracles

1. **Unit/fixture:** exact DS inputs and a known software render or reference, with structural assertions for mode priority, coordinate transforms, palette aliases, depth/stencil, UV orientation and resource lifetime. Use ASan/UBSan in host fixtures where possible.
2. **Same-state frame replay:** capture full engine state (IO regs, VRAM banks, BG/OBJ tile data, OAM, palettes, HBlank line-state, scene list and relevant timings) atomically at the documented snapshot boundary. Render both paths against *exactly the same capture*, compare relevant 2D pixels bit-for-bit and annotate any justified 3D precision tolerances.
3. **Real Old 3DS + New 3DS:** full screen captures/photos and logs at reproducible stopped input phases, long play loop, shutdown/return-to-hbmenu, no GPU hang/audio drops/missing Poképic and no texture corruption. Azahar is additional coverage, not a replacement.

### 11.2 Required benchmark set

| Scene | Stressors | Observations |
|---|---|---|
| Goldenrod outdoor scroll | Large 3D lists, moving field BG, many visible sprites, HBlank | G3 worker, BG reject reasons, texture upload, present count |
| Bedroom/indoors | Near-camera geometry, walls, static tile BG | Camera clipping, 3D/2D order, idle reuse |
| Wild battle entry→command→dialogue | Translucency, battle Pokémon, text, alpha/stencil, scene changes | Mirror veto, identical text orientation, sprite visibility, shadow/outline |
| Bag/party/Pokégear with input | Lower engine OAM, text, selection, short dirty bands | Dirty-band correctness, touch hitboxes, presentation latency |
| Fade/transition and weather | Master brightness, mosaic/HBlank/window, blends | Full fallback correctness, one-frame timing and no stale buffers |
| Menu idle 10 s | Mostly no new pixels | No redundant decode/conversion/upload; game clock still runs |
| Stereo on/off, N3DS smooth on/off | Multiple render passes / supersampling | PICA time, memory, eye symmetry; never make smooth mandatory on O3DS |

### 11.3 Per-frame timing ledger

At minimum report: `game_tick_us`, `G3_enqueue_us`, `G3_decode_us`, `G3_clip_us`, `G3_batch_us`, `2D_A_us` broken into text/OBJ/merge/setup, `2D_B_us`, `publish_convert_us`, `publish_flush_bytes`, `GSP_submit_us`, `GPU_P3D_us`, `GPU_PSC_us`, `GPU_PPF_us`, `wait_us`, `present_age_ticks`, `linear_free_min`, `VRAM_free_or_targets`, `draws`, `state_changes`, `cache_hit/miss`, and frame type. Samples must state whether stages can overlap so **don't simply add wall-clock durations from parallel threads**.

**Result table for every change:**

| Property | Baseline | Candidate | Rule |
|---|---|---|---|
| Game ticks/s wall | recorded | recorded | 60 when running in full-speed mode |
| Upper composes/s | recorded | recorded | Must increase or hold without correctness loss |
| Lower latency/input | recorded | recorded | Must not perceptibly regress |
| G3 worker CPU ms | recorded | recorded | Key for display-list work |
| 2D A/B CPU ms | recorded | recorded | Key for atlas/band work |
| GPU P3D and transfers | recorded | recorded | Ensure CPU savings are not only moved to new GPU bottleneck |
| Upload bytes and GSP flush calls | recorded | recorded | Include hidden bandwidth costs |
| High-water memory | recorded | recorded | No OOM or baseline alloc regression |
| Pixel and state diffs | reference | candidate | Zero for exact 2D classes; documented 3D tolerance only |
| Missing frames/hitches, audio drops, shutdown | recorded | recorded | No new regression |

When evaluating partial presentation, never treat **same image shown twice** as two newly composed frames; distinguish refresh rate, unique presents, completed scenes, and game ticks.

### 11.4 Project commands and integration requirements

Use repo-approved bootstrap/toolchain commands and the actual container outputs. Common checks are:

```bash
python3 tools/check_patches.py
make test
make port-tests
make game-pc
make game-3ds
make game-3ds-ss
make content-check
# Also run the existing 3DSX validator and HG/SS runtime smoke scripts.
# If modifying the decomp patch stack, rerun the project-specific DS matching gate.
```

Record **command, exit code, exact artifact hash, binary revision/feature flags, and observed output**. A successful compile is not proof that its framebuffer is correct. Do not mark unrun Old 3DS tests as passed.

Do not write fixes only into `build/upstream` (it is generated). Follow the repo's patched decomp + carefully pinned vendor design, including HG/SS DS matching obligations. Avoid god-files/functions: new material eligibility, invalidation and texture-cache modules should each have a narrow purpose and one owner.

## 12. Anti-patterns: things that have already wasted time

1. **Replacing all rendering with software to “eliminate GPU bugs.”** The Azahar experiment was slower; hardware rasterization is the advantage.
2. **Moving unrelated work to Old 3DS core 1.** Prior audio-thread move starved geometry/display and collapsed throughput under the CPU share limit. New 3DS has a different topology; measure separately.
3. **Batch merging across material/texture/stencil changes.** Already caused duplicate NPCs and halos.
4. **Automatically building index buffers for every scene.** The project has measured no-reuse cases where indices cost more work than they save.
5. **Hashing more than drawing.** Some reference-range hash experiments found snapshot races and palette/address alias bugs; a cache is only safe with full writer coverage, and useful only if it saves actual work.
6. **A huge per-frame “GPU optimization” when the GPU path is rejected.** First count reject reasons and actual fast-path takes. An elegant path that executes on 0 frames saves nothing.
7. **Confusing guest/emulator counters with real CPU time.** Azahar may change host scheduling and GPU acceleration behaviour.
8. **Using a fixed 30 Hz cap as game speed control.** The prior `fps=30` test halved the game clock; presentation cadence must be separate.
9. **Blind `-O3`, LTO, Thumb or fast-math.** S20 already tested/rejected these for its baseline; re-test only with a specific changed hot path and controlled measurement.
10. **Allocating large supersampled render targets on Old 3DS.** Use 1× texel and native 3D first, keep texture-cache headroom. Quality upgrades are conditional on available memory and GPU margin.
11. **Assuming a static source can skip LCD presentation.** Display double buffering means an unchanged source is not necessarily the content of the next target buffer.
12. **Optimizing only triangle throughput.** The G3 decoder/CPU transform, 2D scanline render and publication have documented costs before PICA is the limiting stage.
13. **Assuming the screen's 400×240 means the game's pixels are pixel-perfect.** The current remaster is predominantly a 1.25× layout; a true 1:1 field view changes coverage and needs game-aware visibility.
14. **Treating documentation plans as merged code.** Several features are in feature flags, local dirty work, or plan-only S21 sections.

## 13. Recommended delivery order

This guide is deliberately not a single monolithic “GPU rewrite” instruction. The least-risk high-value sequence is:

1. **Baseline, ownership and accurate GPU/CPU accounting** (Packet A).
2. **Close the existing battle text GPU-veto bug** before broadening UI takeovers (B).
3. **Make current GPU BG/OBJ acceleration reliable and cache content across scroll**; verify texture formats/generations and memory use (C).
4. **Turn lower-screen partial redrawing from a promising fixture into a hardware-verified optimization**; add a direct PICA path only if lower CPU raster remains the bottleneck (D).
5. **Finish safe G3 command-list fast paths and avoid wasted indexed compaction** (E).
6. **Prototype GPU-side 3D object-space transforms on one safe mesh class only if G3 CPU still dominates** (F).
7. **Refine field-native 1:1 and HGSS-specific widescreen UI** once render correctness and steady frame pace are established (G).

**Definition of success:** on Old 3DS, Goldenrod, battles and menus are visually correct, game ticks remain full speed, present cadence materially improves, and no large frame hitches or GPU hangs are introduced. On New 3DS, unused performance becomes genuine margin or an explicitly chosen optional quality improvement—not mandatory supersampling that steals the frame budget. Every claimed improvement must have a same-input, same-revision **hardware** A/B and a full correctness gate.

---

## 14. Source and code-reading index

### PICA200 and devkitPro primary references

- [3dbrew: PICA200 hardware](https://www.3dbrew.org/wiki/Hardware) — pipeline, shader processors, lighting LUTs and texture units.
- [3dbrew: GPU internal registers](https://www.3dbrew.org/wiki/GPU/Internal_Registers) — exact PICA state, TEV, draw command register interface, texture layout/formats and shader output semantics.
- [3dbrew: GPU programming guide](https://www.3dbrew.org/wiki/GPU/Programming_Guide) — attributes, command examples and index drawing.
- [3dbrew: GPU external registers](https://www.3dbrew.org/wiki/GPU/External_Registers) — GX hardware transfer/fill/format operations.
- [3dbrew: GPU pitfalls](https://www.3dbrew.org/wiki/GPU/Pitfalls) — shader and texture state hazards, including hangs.
- [3dbrew: Memory allocation](https://www.3dbrew.org/wiki/Memory_Allocation) — application/linear memory distinctions.
- [devkitPro citro3d](https://github.com/devkitPro/citro3d) — API, render queues, TEV/stencil/depth, textures and GPU profiling.
- [devkitPro citro2d](https://github.com/devkitPro/citro2d) — sprite atlas/2D batching architecture; use as a design reference, not a replacement for HGSS's timing-sensitive compositor.
- [devkitPro 3DS 2D example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/2d_shapes/source/main.c) — simple state-change advice.

### Developer-documented methods

- [CGWORLD: Pokémon Sun/Moon graphics production](https://cgworld.jp/feature/201707-cgw227GG-pokemon.html) — Game Freak specialized engine, fixed-function material authoring, Solgaleo/Lunala and billboard effects; Japanese original.
- [Jazz² Resurrection: 3DS PICA backend documentation](https://de4th.dev/jazz2/docs/consoles.html) — passthrough vertex shader, CPU transform, TEV, baked lightmaps/palettes, native texture pages, GSP flush batching.
- [P3P3DS: 3DS backend implementation notes](https://github.com/endercodezz/P3P3DS/blob/master/docs/3DS_PLATFORM.md) — GPU-native GE translation and texture caches; author-reported, not independent benchmark evidence.

### `hgss-3ds` entry points and current performance evidence

- [Current state / known pending validation](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/STATE.md)
- [HGSS main 3DS program / scheduling](https://github.com/estyseesghosts/hgss-3ds/blob/main/hgss/game/app/main_3ds.c)
- [DS G3 geometry decoder](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_gx3d.c)
- [PICA renderer, command and texture cache](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_gx3d_pica.c)
- [DS 2D renderer and GPU takeover rules](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/nitro/src/port_render2d.c)
- [PICA compositor and GPU BG/OBJ quads](https://github.com/estyseesghosts/hgss-3ds/blob/main/3ds_port/game/app/port_compose_pica.c)
- [S20 measured Old/New 3DS performance + regressions](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s20-performance-session.md)
- [Half A Nov 6 integration and current WIP](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/halfA-session-2026-11-06.md)
- [Azahar 30 fps campaign and rejected experiments](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/az30-campaign.md)
- [Battle mirror bug and safe veto](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md)
- [Phase A remaster audit and HGSS UI gaps](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/wiki/3DS-Remaster-Audit.md)
- [S21 GPU 2D concept, explicitly plan-only](https://github.com/estyseesghosts/hgss-3ds/blob/main/docs/progress/agent-notes/s21-goldenrod-2d-kill-plan.md)

**Final instruction to implementers:** prioritize proven elimination of CPU pixel work, per-scene list decoding and GPU transfer bytes; maintain the original renderer as an explicit correctness reference. A fast screen that mirrors text, changes transparent polygon IDs, or intermittently drops sprites is not an optimization.


---

## 15. Instrumentation: stop chasing ghosts

This companion section is a proposed instrumentation implementation specification, not a claim that the new tracing exists in the code already.

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
