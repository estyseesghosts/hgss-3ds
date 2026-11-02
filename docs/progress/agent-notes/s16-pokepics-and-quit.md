# S16: the battle Poképics, and the close that hung

Log-12 came from the first build that finished a wild battle (after the
raw-address fixes). It showed two new defects: both battle Pokémon were
invisible between attacks, and choosing **Close** in the HOME menu hung the
console. The owner also supplied `heartgold.sav`, which finally made the
battle reproducible on the PC.

## The invisible Pokémon

**Symptom.** The battle runs — the UI, HP bars and menu work — but the
opponent appears for an instant and vanishes, and the player's Pokémon never
appears. They flicker during an attack. The PC's `gx3d` log agrees: after the
battle overlays load every scene is `0 vtx, 0 polys` (120 scenes in a row),
with a few polygons only during attacks.

**Cause — two layers.**

1. HGSS draws its battle Pokémon as **Poképics**: a `PokepicManager` makes
   80×80 3D quads with `NNS_G2dDrawSpriteFast`, under a one-time screen-space
   ortho camera (`NNS_G2dSetupSoftwareSpriteCamera`), and rotates them with
   `G3_RotX/Y/Z`. The port's GXFIFO is ordinary memory: only writes through
   `NitroPort_GxDirectWrite`/`NitroPort_GxFifoWord` reach the geometry
   decoder. The G3D path was converted, but the G2D software-sprite path
   (`g2d_softsprite.c`, `g2d_renderer_core.c`, `g2d_oam_soft_emu.c`) still
   wrote the raw command registers, so the quads never entered a scene.
2. Converting the sprite path made the quads reach the scene, but every one
   was clipped (`lost: 360 clipped`). `g3_util.c` still wrote
   `reg_G3_MTX_MODE = PROJECTION` and the nine `MTX_MULT_3x3` rotation words
   by hand. The mode write was lost, so `G3_LoadMtx44` (already converted)
   loaded the ortho matrix into the **position** stack, and the projection
   stayed at the field camera's. The ortho pixel coordinates were then
   transformed by a perspective matrix and clipped.

**Fix.** NitroSystem **0304** converts the G2D writes; NitroSDK **0605**
converts `G3i_OrthoW_`/`G3i_LookAt_`'s mode writes and `G3_RotX/Y/Z`. The
declaration comes from `nitro/gx/g3imm.h`; the first version added a local
`extern` with `unsigned int`, which clashed with the header's `uint32_t` on
the 3DS (`unsigned long` there) — the library build logged a conflicting type
and the link reused stale objects. The corrected patch relies on the header.

**Checked on the PC.** With the owner's save:

```
cd /work/fix-pc
./pokeheartgold-pc /work/final-heartgold3ds.pak /work/final-hg-data/manifest.json \
  --save /work/user2.sav --frames 14000 --press "$(cat /work/zig.press)" \
  --screenshots /work/g3-shots --screenshot-every 1000
```

The save is at `/Users/terezi/hgss-private/heartgold.sav`; copy it in and make
it writable (the container user cannot open the original 0644 file for
`r+b`). The `zig.press` script presses START/A to continue, then walks up and
down in the grass; the battle triggers around frame 10,000. The fixed run logs

```
[ 13756] [gx3d] 120 scenes: 1440 vtx, 360 polys -> 720 tris (720 opaque, ...); lost: 0 ...
[ 13756] [gx3d] projection: ... now 32768 0 0 0 / 0 -43691 0 0 / ...   (the ortho)
```

and the screenshots show the Pidgey and the player's Cyndaquil.

## The close that hung

**Symptom.** HOME menu → Close: `log.txt` ends at `closing at frame 15296`
and the console has to be power-cycled.

**Cause.** `GpuStopGeometry` stopped and joined the geometry thread and only
then called `NitroPort_Gx3dStopThread()`. Its `drain()` waits for the command
ring to empty — it publishes the producer's buffer and waits for the worker.
On a close, `NitroPort_OnFrame` sees `aptMainLoop()` false **before** its
`NitroPort_Gx3dFlush()`, so the frame's commands are still unpublished; with
the worker already joined and gone there is nobody to consume them, and the
wait never ends. (The log's last line is from `WaitForGame`, before this.)

**Fix.** Drain **first**, while the worker is alive: `GpuStopGeometry` calls
`NitroPort_Gx3dStopThread()` before signalling/joining the thread. The shell
also logs each shutdown step (`[quit] closing`, `APT unhooked`, `geometry
stopped`, `display stopped`, `sound stopped`, `stopping`), checks the audio
join, and ends the process at once (`svcExitProcess`) if a thread will not
stop, rather than tearing libctru/citro3d down around it.

## Verification

- PC battle: both Pokémon render, `720 tris/scene`, `lost: 0 clipped`.
- `bootstrap`: 82 patches; `make test`: 345 tests.
- 3DS build and artifact: see `docs/progress/STATE.md`.
- The hardware run is the end-to-end check for both: finish a battle, then
  close the app from the HOME menu; the log should end with the `[quit]`
  steps.

## Notes

- The owner's save is the first field save the PC harness can continue from.
  Keep a copy at `/work/user2.sav` for battle/summary/trade reproduction.
- The save loads only if the file is writable by the container user: the
  save-chip emulation opens it `r+b`, and a `docker cp`ed 0644 file owned by
  another uid produces "The save data could not be accessed."
