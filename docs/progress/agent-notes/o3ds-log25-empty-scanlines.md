# Run 25 and the empty-software-picture slice

## Hardware result — build 908f84c

The owner reports Goldenrod standing, movement south onto the adjacent route,
and return are now possible, unlike before. This is real functional progress,
not completion of the 30-fps playability target.

The supplied private `log-o3ds-soulsilver-25.txt` ends with a clean HOME close
at frame 14426. Audio finishes **0 dropped of 56169**. Core placement, 80%
share, display-first, 60-Hz cap, widescreen and lower-screen policy match run 24.
It still replays the frozen script through frame 4200 before manual play.

Means of logged interval rates/times (not delivery histograms or CPU times):

| Interval-end samples | Run 24 | Run 25 |
| --- | ---: | ---: |
| 2400–4200 display compositions/s | 7.800 | 7.925 |
| 2400–4200 geometry scenes/s | 27.825 | 27.125 |
| 2400–4200 drawer frames/s | 1.525 | 1.550 |
| 2400–4200 render wall ms/frame | 409.150 | 415.928 |
| 4800/5400 display compositions/s | 7.700 | 8.750 |
| 4800/5400 drawer frames/s | 2.100 | 2.700 |
| 4800/5400 render wall ms/frame | 257.075 | 181.790 |

**Verdict:** do not call R1 a moving-Goldenrod FPS win; that section is
essentially flat. The common post-script samples improve, and the owner can
actually traverse the area. Later manual/stereo/different-scene samples are
not mixed into the scripted comparison: stereo activates near 12054, geometry
changes around 13000, the 13800 interval reaches 19.5 composes/s, and the
14400 return interval is 10.5. The latter is not Goldenrod meeting 20 fps.

Remaining issues: 98–100% geometry busy in Goldenrod, roughly 8–9 composes/s,
2–3 drawer frames/s after the script, persistent late frames/lag 4 and slow
publish wall times. Memory reaches **4059 KB free linear** during the excursion
(near the 4-MiB floor); the new slice allocates nothing. Game-clock cadence,
full visual correctness and New 3DS regression are not certified by this log.

## Concrete remaining work — no new diagnostic build

Existing counters suffice to identify wasted scanlines: at frame 5400,
`region behind=27`, `taken=54`, and `lines=empty=5184` (27 × 192). Other
stationary intervals show the same pattern. The renderer is repeatedly walking
empty CPU scanlines after it committed the field's BGs to GPU quads. R1's
conservative VRAM signature is intentionally not a claim that unrelated memory
writes can be ignored, so it does not remove this work in all live frames.

The next small slice uses the authoritative **current** GPU commit, not looser
memory invalidation. For the existing all-text-GPU/zero-sorted-sprites gate,
with no windows/H-blank/circles, standard 192-row/top-zero canvas, and the
backdrop not a BLDCNT first target:

- Every CPU `under` pixel resolves to the current backdrop.
- Every `over` pixel is zero.
- `overAlpha` is untouched (SnapshotOver's empty-row contract).

The IfChanged path performs one whole-plane backdrop fill and one over clear
instead of 192 row setups, priority walks, clears, finishes and timer reads.
It **refills on every call**, including palette/tile/unused-VRAM changes or new
buffers. No cached-pixel validity proof or additional writer hooks are needed.
Existing setup/keys/validation/quad construction and conservative hashes remain.
Unsupported effects, software layers, sprites and affine takes retain normal
scanlines. Forced public Render2DSplit deliberately retains normal scanlines as
an independent executable reference. No new counters, GPU coverage, scheduler,
audio, game-clock, config, allocation or command/texture ownership changes.

This is a narrowly independent part of W3/R3; it does not claim W3/R2 writer
coverage, bypass its hashes, or complete the battle/W2 correctness gate.

## Local verification

Lead-authored implementation; project scoped-explorer independently checked
the plane-equivalence proof and forced-render reference. No review finding.
Actual-renderer fixture: **811 checks, 0 failed**, also ASan/UBSan. The same
expanded fixture fails 10 checks against build 908f84c's source, proving the
new content-change path is exercised. Fresh full reference renders still run
scanlines; test comparisons include pixels, alpha, quad ordering/UVs and keys.
No golden expectations were updated. One old affine fixture was strengthened:
DS mode 1 keeps BG2 text; it now uses mode 2 and explicitly asserts BG2 affine.
Backdrop-as-brightness-first-target fallback is also tested.

Full native Python suite: **461 tests, 0 assertion failures, 11 baseline
toolchain errors, 13 skips**. Native game builds/artifact hashes are recorded
in STATE after completion. Hardware gain and full golden/New 3DS verification
remain pending. Use existing frozen inputs and counters; this is an optimization
candidate, not a request for another attribution-only run.
