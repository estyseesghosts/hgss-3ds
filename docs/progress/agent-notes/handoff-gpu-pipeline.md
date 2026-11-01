# Handoff: bring Platinum's GPU 3D pipeline into the HGSS 3DS shell

You are continuing the `pokeheartgold-3ds` port. Read this whole brief before
touching anything. It is self-contained: you start with fresh context.

## Environment

- Repo: `/Users/terezi/hgss/pokeheartgold-3ds` on macOS. All builds run inside
  a Docker dev container:

  ```bash
  cd /Users/terezi/hgss && devcontainer exec \
      --workspace-folder /Users/terezi/hgss/pokeheartgold-3ds \
      bash -c 'cd /workspaces/pokeheartgold-3ds && <command>'
  ```

- The container's `/work` is a Docker volume (not visible from macOS).
  `/Users/terezi/hgss-private` is mounted into the container at `/private`, and
  is how artifacts cross between the two.
- Real HeartGold data pack, for local runs only:
  `/work/final-heartgold3ds.pak` + `/work/final-hg-data/manifest.json`.
- A 3DS build takes ~12–15 minutes. Run them with `background=true` and keep
  working; never poll.
- The vendored port layer is `3ds_port/`, pinned byte-identical to
  `platinum.lock`. **Never edit it.** Compiling its files is fine.

## The problem (already diagnosed — do not re-derive it)

The 3DS shell `hgss/game/app/main_3ds.c` never renders 3D. Evidence:

```c
/* 3ds_port/nitro/src/port_gx3d_image.c */
const uint16_t *NitroPort_Gx3dAcquireImage(void)
{
    convert_pending();
#ifndef __3DS__
    render_host();        /* the PC rasterises the newest scene here */
#endif
    ...
    return index >= 0 ? sImages[index] : NULL;
}
```

- On the **PC**, acquiring an image rasterises the newest scene and publishes
  it, so `NitroPort_RenderScreens` always has a 3D layer. The PC draws the
  bedroom correctly.
- On the **3DS**, acquiring only converts an image already handed over with
  `NitroPort_Gx3dSubmitRGBA8`. That image comes from a **GPU worker**. Our
  shell has none, so `sPublished` stays `-1`, `AcquireImage` returns NULL, and
  `NitroPort_Render2D` draws the 2D layers only: dialogue box over black, no
  room. `NitroPort_Gx3dImageCount()` never advances.
- The `[gx3d] ... scenes: N vtx` lines in `log.txt` come from the **scene
  builder**, not from rasterising. They are not evidence that anything was
  drawn. Earlier work in this session mistook them for that — don't repeat it.

Everything else already works on hardware and must stay working: boot, intro
text, the scripted run, both screens' 2D, audio, APT close, frame logging.

## Goal

Bring **Platinum's GPU pipeline** into the HGSS shell so the 3D layer is
rendered by PICA and handed to the port's image API. This is Option A from the
handoff discussion: the proven architecture, not a CPU fallback.

Platinum's own shell is the reference implementation and is vendored in
`3ds_port/game/app/`:

| File | What it is |
| --- | --- |
| `main_3ds.c` (~2961 lines) | Platinum's shell: `StartGpu` (761), `GeoMain` (870), `StartGeometryOn`/`StartGeometry` (903/917), `StartDrawers`, `DrawerMain`, `RenderScreen`, the compose path, `NitroPort_Gx3dConvertPending` at 1780 |
| `port_gx3d_pica.c` (107 KB) | PICA command translation for the 3D scene |
| `port_compose_pica.c` (156 KB) | GPU composition of 2D + 3D |
| `port_pica_abi.c`, `port_pica.h` | The ABI and interface: `NitroPort_PicaInit`, `NitroPort_PicaRender`, `NitroPort_PicaFrame*` |

These are **not compiled into our program today** — they are Platinum's shell
files. The harness compiles app sources from `app_sources()` in
`tools/hgss_game.py`; that list, and possibly the link libraries, are what you
extend.

## Acceptance criteria

1. `NitroPort_Gx3dImageCount()` advances on hardware while a 3D scene is up
   (log it; it is exported by `nitro_port_gx3d.h`).
2. The player's house renders: room, player sprite, correct on the screen
   Pokemon HeartGold puts it on. The intro movie's 3D renders too.
3. 2D still composites correctly (dialogue boxes, the touch panel), no flicker,
   no log text on the screens.
4. Audio still plays; `log.txt` still ends with `audio: N dropped frames of M`.
5. APT close still clean (`the HOME menu asked to close (frame N)` →
   `stopping`), and the run still reaches Oak's speech and the bedroom.
6. `make test` passes; the packaged `.3dsx` passes `3dsxdump` (exit 0) and the
   harness's own `validate_3dsx` (it runs automatically and fails the build).
7. Frame times recorded with `tools/frametime_report.py` before and after.

## Suggested slices (delegate these; review and commit as lead)

Start with a read-only explorer pass, then implement one slice at a time.

- **S1 (explorer, no edits).** Map Platinum's GPU pipeline: the exact call
  order in its `main()` and drawer loop, what `StartGpu`/`StartGeometry`/
  `StartDrawers` create (threads, priorities, cores, signals), what
  `DrawerMain`/`RenderScreen` do per frame, which render targets and frame
  buffers `port_compose_pica.c` expects, and whether `NitroPort_PicaRender`
  submits the image itself. Deliver: a dependency list, the minimal call
  sequence, the files that must be compiled, and any build-tool requirement
  (e.g. a shader compiler for the `.v.pica` files). **Stop and report before
  writing code if any new tool dependency is needed.**
- **S2 (implementer).** Compile the needed vendored modules into the 3DS app
  and link them, with no behaviour change yet. Verify: it links, packages,
  `NitroPort_PicaInit`/`NitroPort_PicaRender` are present in the ELF, and
  `3dsxdump` is happy.
- **S3.** Start the pipeline (`PicaInit`, geometry thread with
  `NitroPort_Gx3dStartThread` + `NitroPort_Gx3dThreadMain`, `NitroPort_Gx3dFlush`
  once a frame) and log the image count. Keep a **safe fallback**: if PICA init
  or a thread fails, log it and behave exactly as today rather than crashing.
- **S4.** Wire the drawer/compose so a rendered image is published, and 2D
  composition still lands on the right screens. This is where the citro3d
  render targets meet `gfxSwapBuffers` — if they fight, use
  `port_compose_pica.c` (Platinum's proven path) instead of inventing a third
  way.
- **S5.** Docs and hand-off artifact.

## Local verification (every slice, before asking for hardware)

```bash
# build
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/final-3dsN -j6

# the vendor's own reader: exit 0 and page counts is what you want
/opt/devkitpro/tools/bin/3dsxdump /work/final-3dsN/pokeheartgold.3dsx /tmp/v.bin

# symbols you expect in the image
arm-none-eabi-nm /work/final-3dsN/pokeheartgold-3ds.elf | grep -E 'NitroPort_Pica|Gx3dThread'

# no regressions
make test          # currently 324 tests, all passing
make port-tests    # currently 17,122 checks, 0 failed
```

Stage a build for the owner only when a slice is worth a hardware run:

```bash
docker cp dreamy_elion:/work/final-3dsN/pokeheartgold.3dsx \
    /Users/terezi/hgss-private/sd-card/3ds/pokeheartgold.3dsx
shasum -a 256 /Users/terezi/hgss-private/sd-card/3ds/pokeheartgold.3dsx
```

Hardware runs are the **owner's**: never report them as done. Ask for
`sdmc:/3ds/pokeheartgold/log.txt` back, and state exactly what to look for.

## Hard constraints (the project's rules)

- **No game content in git**: no ROM, save, pak, `.3dsx`, `.smdh` from a cart,
  golden captures or screenshots of game art. Artifacts live in `/work`.
- **Never edit the decomp in place.** Every game/SDK change is a numbered
  `SDK_PORT_3DS`-guarded patch under `patches/`, with a `gcc -E -P` identity
  proof without the guard. If you add a patch, re-run the DS gate
  (`tools/matching-build.sh build/upstream HEARTGOLD`, and `SOULSILVER`); the
  tree is at **77 patches** and the gate's last pass was at 76.
- **`3ds_port/` stays byte-identical.** If a fix genuinely belongs there, stop
  and ask the owner about an upstream PR instead.
- **Ask before adding a tool dependency** (the `.v.pica` shaders may need one).
- **Do not weaken tests**; add tests for anything you fix. Shell behaviour is
  covered by text assertions in `tests/test_hgss_game.py`.
- Never trust `threadJoin` for liveness, never skip the blit, never skip
  engine A's composite, never put a console on the game's screens — every one
  of those cost a hardware run. All are explained in
  `docs/wiki/3DS-Pitfalls.md`; read it before touching the shell.
- Commit style: imperative subject, body explaining the why, `Co-Authored-By:
  Claude <noreply@anthropic.com>`. Do not commit build artifacts.

## Do not regress (each has a test or a log line)

- Port hooks are **strong** references (a weak one binds to NULL).
- Both screens are **blitted every frame**; only the composite may skip.
- Engine A is composed **every frame**; only engine B may skip.
- APT is owned by the game thread only.
- The New 3DS speedup is requested before `gfxInitDefault`.
- `NitroPort_Log` holds a `LightLock`.
- No console unless the pak fails to open.
- Audio: four 512-frame NDSP blocks fed from `NitroPort_SoundRender`.

## Report back with

- What you changed, file by file, and why.
- The exact build/test commands and their tails (`3dsxdump` rc, `make test`
  count, symbol grep).
- The artifact path, size and sha256, and the commit hashes.
- What is **pending the owner's hardware run**, phrased as such.
- Anything you could not verify locally, called out explicitly.
