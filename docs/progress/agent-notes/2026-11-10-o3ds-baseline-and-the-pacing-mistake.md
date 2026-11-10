# Old 3DS baseline and the pacing mistake (2026-11-10)

## What the hardware logs established

Two matched fight-free Goldenrod runs (`hgss-private/log-0`, builds
`b3024a0` and `b3024a0-profoff`, same save/script/ini, both closed from the
HOME menu) measured, over frames 9000+:

| | PROFILE | OFF |
|---|---|---|
| game clock (600 frames per VBlank count) | ≤55 Hz | ≤57 Hz |
| composes/s | 9.8 | 8.6 |
| publishes/s | 5.5 | 4.0 |
| drawer | 92 ms [47..146] | 179 ms [126..271] |
| geometry | 36.5 ms/scene, 99% busy | 35.3 ms/scene, 99% busy |
| GPU | 12.1 ms (clears 6.1) | 12.1 ms (clears 6.0) |
| scene-draws per 600 frames | 67 | 62 |

The GPU is busy ~12% of the time; the drawer is the wall. The two runs are
not a matched A/B (OFF is 2x faster on the title screen and 2x slower in the
field, while every cost the instrumentation gate cannot touch matches within
3%), so the PROFILE-vs-OFF claim sheet stays open. The OFF log also carried
impossible phase values (`stage 16102.78 ms/draw`, `end 274113216854.52`).

## The mistake

Acting on "geometry builds 27 scenes/s, display takes 6": I made scene
production follow the display's take rate. Two designs were built and both
were wrong for the same reason.

1. **Reactive skip** (`NitroPort_MailboxUnread() != 0` → skip the next
   scene). It serialised production behind consumption: Azahar composes fell
   **23.75 → 11.50/s** (drawer 10.4 → 4.6 ms, publishes 30 → 60/s). Reverted
   the same hour.
2. **Rate throttle** (publish at most 1.5 scenes per measured take interval,
   capped at 3 consecutive skips). Self-tuning and harmless in Azahar:
   composes **21.00 → 22.55/s** and geometry 30.70 → 27.61 ms/scene (one
   trial each; an emulator sample, and it still cannot raise hardware
   presentation). Reverted after review.

### Why it was wrong

- **It does not touch the binding constraint.** Presentation is capped by the
  drawer's 92 ms frame. Cutting geometry work cannot raise presented frames
  when the drawer decides the rate; it only makes an idle stage idler.
- **The ratio it was built on is partly an artifact.** `STATE.md` (2026-11-06)
  already records that `sSwapsDone` double-counts skipped scenes and that the
  pipeline is 1:1:1 (worker-bound), not oversupplied. The "27 built vs 6
  shown" figure came from those same counters and was taken at face value.
- **A work reduction is not an optimization** unless it is on the path that
  bounds the metric being improved. The user's target is 30 fps on Old 3DS;
  the drawer has to fall from 92 ms to under 33 ms for that, and nothing in
  this change moved it.
- **It read as rendering less.** It did not (the display took 6/s before and
  after), but it looked like it, and a change whose defence is "the frames were
  thrown away anyway" is not the change to spend a hardware run on.

Kept from the session: the compose-phase counter fix only
(`port_compose_pica.c`; stage/end no longer publish a wrapped `u64`).

## Rule

Before cutting work, name the stage that bounds the user-visible metric and
show the cut is on that stage's path. Verify any derived ratio against the
counter corrections already recorded in `STATE.md` and the campaign notes.

## Next

The drawer's 92 ms is the target: the top screen costs 50 ms and the bottom
7 ms, while the 2D rasteriser reports 0 lines drawn, the GPU 12 ms and
publish 1-6 ms. The per-frame `setup` block is where the top pass goes
(12.1% of a core on hardware), and the `pal`/`key` split in the `[perf] 2d:`
line names the per-BG key walk inside it. Attribute before changing anything.

The rest of that session's issues (git identity, the unmatched hardware A/B,
artifacts built dirty, the Windows test baseline, the emulator driver) are in
[the issues log](2026-11-10-session-issues-and-fixes.md).
