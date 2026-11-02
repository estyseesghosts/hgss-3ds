# Draft: message to the pokeplatinum-3ds maintainer (NOT SENT)

Subject: HeartGold/SoulSilver support on top of pokeplatinum-3ds

Hi,

I'm working on a native 3DS port of HeartGold/SoulSilver from
ctrl-cheeb-del/pokeheartgold, and your `3ds_port/` is exactly the hardware
layer I need. Your README lists HGSS as a long-term target, so before I go
further I wanted to ask how you'd like to handle it.

Two options:

1. **Inside your repo.** HGSS lives next to Platinum (`patches/pokeheartgold/`,
   a second `upstream.lock`, HGSS `game/app` files). One hardware layer, one CI.
2. **A sister repo** (what I'm doing for now) that vendors `3ds_port/` at a
   pinned commit and keeps HGSS code in new files and patches. GPL-3.0 and
   your notices stay intact.

Either way I'd like to send generic fixes back to you as small PRs rather than
fork the behavior: mosaic and display capture in the software 2D renderer
(HGSS uses both), and the cart SPI / infrared path (`CARD_SpiWait*`) for the
Pokéwalker. I'll keep the Platinum tests green and won't touch game-specific
code.

One concrete PR is ready to shape: a triangle with a vertex at clip
`(0,0,0,0)` has zero screen area (both edges to a rim vertex project to that
rim point), so `stage_triangle()` should drop it. HGSS's title quads/quad
strips hit that case, and drawing the stored screen `(0,0)` as a fan deformed
Ho-oh's wing. The test is `test_camera_plane_triangle` in
`nitro/tests/test_gx3d.c`. It is the one place our vendored copy currently
differs, and I would rather it live upstream.

I don't ship ROMs, assets or game builds, following your CONTRIBUTING.md.

Which would you prefer, and is there anything in `3ds_port/` you're planning to
restructure that I should know about?

Thanks,
Julie
