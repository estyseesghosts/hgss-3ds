# Battle UI mirror bug (2026-11-04) — open, critical

## Symptom
In battle, the message-box text renders fully H-mirrored (every glyph flipped
left-right). Status boxes (player/enemy name, level, HP) and the bottom-screen
menu render perfectly. Seen on N3DS hardware (log-n3ds-soulsilver-5 frame,
build `9434629-dirty`) and reproduced live in Azahar on build `6aca216-dirty`
(wild battle, top screen). Pre-S8 builds show it too, so S8 is not the cause.

## What the logs prove
- Takes fire in battle (~1.2 quad/frame, all reject columns zero) even though
  battles must stay software. Root enabler found: the nominate veto calls
  `NitroPort_OverlayLoadedNamed("battle")`, but **no overlay named `battle`
  (or `overlay5`) exists** — `build/game-3ds/gen/overlay_table.c` has 129
  entries (`OVY_N` + named apps like `field`, `pokegear`). The name check is
  dead code; nomination is unconditional everywhere. Same dead names break
  `NitroPort_RepeatingBgs` (always returns 0) and `NitroPort_PixelField`
  (never engages). Field rendering only works because the wide default path
  (clip+origin) doesn't need them.
- Battle BGs validate cleanly (no blend/hblank/mode rejects on the taken BG),
  `affine 0.0%` in battle 2d lines, `flipV` is vertical-only.

## Ruled out (read, not guessed)
- PICA tiling (`port_upscale.c` Morton tables, `tile[m[x]] = s[x]`) — correct.
- Text-quad UVs (`q->u0=(hofs-origin)*texel/512`, `u1=u0+width*texel/512`) —
  pure offset, cannot mirror.
- BG picture decode (`NitroPort_Render2DBgPicture` honors per-tile hflip/vflip
  exactly like software is expected to).
- `Double2xTiled16` 256-map path (Morton4 ordering verified left-to-right).
- Affine path (0% in battle), flipV, compositor `quad4` arg order.

## Open hypotheses (in order)
1. The taken battle BG is validated through a path whose scroll/origin
   accounting mirrors (e.g. negative hofs + origin interplay, or a
   `takeHofs`/`takeVofs` sign issue specific to battle scroll values).
2. The message box is drawn by a mechanism outside plain text BGs
   (window-shifted runs, band fills, tall-clip paths) that a quad cannot
   reproduce — i.e. the take *validation* (not the quad math) wrongly passes
   in battle. Re-check `GpuBgValidBg`/`GpuBgOrderOk` against battle DISPCNT/
   WININ/BLDCNT values from a live battle.
3. The veto fix alone resolves it: if battles go all-software per plan, the
   mirror disappears regardless of mechanism. But the mechanism still matters
   (a validation hole could mis-take elsewhere).

## Next steps
1. Fix the veto with a real battle identity (overlay ID set from main.lsf
   objects like `overlay_12_battle_command`, or a game-state signal — NOT a
   name string). Add a boot-time assertion or a unit test over
   `overlay_table.c` for every `NitroPort_OverlayLoadedNamed` literal so dead
   names can never ship silently again.
2. If the mirror persists anywhere after the veto, bisect quad-vs-software by
   scene: the take only fires when `frameOk` passes; force `split->gpuBgs = 0`
   for one run to confirm all-software battle UI is exact.
3. Repro without hardware: boot any staged SS build in Azahar (free play, no
   script files in `sdmc/3ds/pokeheartgold-ss/` or the bench replays), walk
   into grass with the frozen save (`~/hgss-private/soulsilver.sav`); RPC
   `save_screenshot` shows the message box. Deterministic scripted alternative:
   extend `goldenrod-bench.txt` with walk-into-grass inputs (not yet done).

## Collateral findings (keep)
- S6 has no field candidates: OAM-A is empty by game design (game disables
  all 128 entries; the OAM mirror is fine), OAM-B's 44 sprites are the
  touchscreen menu UI, field actors are BG-map tiles (already in the quads).
- `region front=0` on the field is vacuous (nothing is in front there); front
  takes engage in menu scenes (log-5: 355/71), so the counter works.
- S10's "conversions 0 after first frame" already holds in steady state; the
  o3DS wall is first-seen texture bursts (28 tiny textures ≈ 7 s on the
  display thread) plus scene build — needs amortization (pop-in tradeoff, owner
  decision) or per-texture cost profiling, not cache work.
