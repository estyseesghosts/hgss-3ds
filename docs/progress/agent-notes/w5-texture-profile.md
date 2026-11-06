# W5 texture attribution: diagnostic only, hardware pending

The W5 opt-in build (`HGSS_TEXTURE_PROFILE=1`) measures the existing texture
cache without changing its eviction, decoding, synchronization or rendering
policy. Normal builds do not enable the profiler. Use a fresh output directory;
do not deploy the diagnostic as a performance build. The source checkout is
based on `584305e` with the W5 diagnostic modifications uncommitted.

The counters are cumulative from boot and reported at the existing 600-frame
performance logs. They include cache lookups and hashes, new and changed
conversions, allocations, evictions, failures and the worst observed miss's
texture parameter/output bytes. Tick sums are quantized to 256 system ticks
per operation and wrap at 2^32 units. Snapshots use atomic individual fields,
not a transaction, so a sample may straddle an operation. All times are wall
time and include preemption. Allocation includes any eviction wait; conversion
includes decode. They must not be summed as independent stages. Conversion
includes decode, scratch, hole checks and packing/filtering; this first slice
does not separately time tiling, GPU submission, between-call gaps, or xBRZ
background work, and it does not record every texture or percentiles.

## Azahar: cold start and warmed Goldenrod

SoulSilver used the existing frozen benchmark save/script/profile, Old-model
emulation, serial execution and no concurrent builds. Diagnostic artifact:
`build/w5-texprof2-ss/pokesoulsilver.3dsx`, SHA-256
`1808d554981167275afde1e51de00aa2c278dbc2c3cc8b19084f1fdff86f7b6e`.
Log and capture: `build/w5-texprof2-field/`. Emulator save/config were restored.
The build-info sidecar identifies this as `texture_profile: yes`; future
sidecars record the build flag automatically. The original sidecar was amended
after the build to record the flag used for that artifact.

| Interval | Lookups | Hashes | Conversions / new | Search wall | Hash wall | Allocation wall | GPU wait wall | Convert wall (decode within) | Flush wall |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| boot–600 (cold) | 1,834 | 1,312 | 37 / 37 | 4.47 ms | 81.58 ms | 0.14 ms | 0.00 ms | 12.14 ms (3.73 ms) | 2.67 ms |
| 600–1200 | 11,275 | 5,880 | 22 / 22 | 28.90 ms | 98.83 ms | 0.09 ms | 0.00 ms | 2.38 ms (0.85 ms) | 1.36 ms |
| 2400–4200 moving | 33,956 | 17,810 | 0 / 0 | 79.93 ms | 292.22 ms | 0.00 ms | 0.00 ms | 0.00 ms | 0.00 ms |
| 4800–5400 idle | 11,230 | 5,874 | 0 / 0 | 24.88 ms | 103.09 ms | 0.00 ms | 0.00 ms | 0.00 ms | 0.00 ms |

No evictions or allocation/conversion failures were recorded through frame
5400. Worst miss was 2.22 ms wall, param `4e430000`, DS format 3, 128x128,
32,768 output bytes. Quantization can round very short waits to zero. The same
replay's frame-5400 publish A reused all 600 calls (0.05 ms/call); publish B
had no calls. That static sample does not characterize moving publish work.

The uninstrumented `584305e` emulator replay of the same script reported
21.6 composes/s and 6.73 ms build at frame 5400; this diagnostic reported
20.6 and 7.26 ms. These are noisy single wall-time samples, with different
animation/worker phases; they do not establish profiler overhead or hardware
performance. The instrumentation performs timing per lookup, so an on-device
overhead A/B is mandatory. Repeated hashes are visible, but replacing them with
generations would require proven coverage of all texture/palette writers.

## Outstanding W5 gates

Run the same diagnostic build and save on physical Old 3DS under the requested
default/display-first scheduling A/B, with cold and warmed scenes and a
matching uninstrumented build. Instrument remaining conversion sub-stages,
submission/gaps and distributions, and measure profiler overhead. Validate
resource lifetimes and high-water on hardware, cold loads, battle and scene
transitions. Do not claim zero >100 ms hardware stalls or an optimization from
these Azahar data. The current evidence does not justify prewarming or bypassing
cache hashes. No screenshot goldens, audio, HOME/sleep or physical-device tests
are claimed by this run.

## Build and smoke checks

The SS diagnostic package passed `validate_3dsx`. A fresh normal HeartGold
build, `build/w5-normal-hg/pokeheartgold.3dsx`, SHA-256
`b3c0d3f876d54002aacead8ef113d703c61c4a4bb1a7bab0e7234df7a672ca0c`,
also passed validation and reached frame 1200 in Azahar. Its ELF contains no
`NitroPort_PicaTextureProfile` symbol; no diagnostic was requested. Its sidecar
was amended to record `texture_profile: no`. Both emulator runs restored their
save, scripts, log and config. Eleven targeted build-flag/build-ID tests passed;
patch verification was 87/87. Full Linux/host32 and device gates remain open.

`make test` with the project Python ran 473 tests with 11 errors and 13 skips;
the errors include missing `gcc-10` and host32 toolchain limitations. The
system Python 3.9 invocation also cannot import tests using newer type syntax.
`make port-tests` failed linking `test_pak` with the macOS linker targeting
armv4t. Logs: `build/w5-make-test-venv.log` and `build/w5-port-tests.log`.

## Lookup experiment

`HGSS_TEXTURE_HINT=1` enables a 512-byte, direct-mapped cache lookup hint.
Each hint is checked against the full texture key and current slot before use;
stale entries, collisions, resets and evictions fall back to the original
linear scan. The content hash, allocation, frame pinning and GPU wait paths
are unchanged. A native fixture using the actual helper passed 500,000
comparisons with the original scan under ASan/UBSan, including deliberate
collisions and stale slots. This remains off by default pending device evidence.

An initial Azahar A/B with the same additional experimental palette-hash code
in both builds reported search wall in frames 2400–4200 of 79.74 ms without
the hint and 48.82 ms with it (about 34,000 lookups each). This is roughly
31 ms over the entire interval, not 31 ms per frame; it is emulator wall time
with profiling overhead, not an Old 3DS speed claim. The palette experiment
itself showed no convincing gain in that emulator run. It is now retained as
the separate `HGSS_PALETTE_HASH=1` build experiment, not enabled by default.
Changing that flag invalidates the generated SDK texture object; build-info
records the flag. Its bytewise-equivalence fixture covers 20,000 cases.

The clean Azahar rerun with original palette hashing produced **79.93 ms**
search wall without the hint (33,956 lookups), versus **51.10 ms** with the
hint (33,849 lookups), over frames 2400–4200. Hash wall was 292.22 vs 291.07 ms;
each run had 59 conversions by frame 1200, no later conversions, evictions or
failures through frame 5400. The candidate artifact is
`build/w5-hint-final-ss/pokesoulsilver.3dsx`, SHA-256
`60cfd64961e692174016218e0d7224974e116698818bb02d911b96a9478bbc26`;
logs are in `build/w5-hint-final-field`. This is roughly 29 ms saved over
the entire moving interval in the emulator, not a demonstrated device or FPS
improvement. Hardware A/B and profiler-overhead measurements remain open.

### Uninstrumented device candidates

The independent baseline, hint and palette SoulSilver builds and their exact
hashes and test instructions are staged in `dist/w5-device-ab/README.md`.
The variants use the same source and frozen benchmark, and log distinct build
IDs. Each validated as a 3DSX and reached Goldenrod at frame 1200 in serial
Azahar smokes. Both candidates reached a wild battle and its menu. A black
palette-battle capture was investigated with a longer replay, which progressed
into another battle; that does not exclude intermittent issues. The emulator
save, settings and recent-files configuration were restored after the runs.
These are not physical timing, audio, or complete regression results.

The restored `HGSS_PALETTE_HASH=1` build produced the same SHA-256 as the
earlier palette-only Azahar artifact, confirming the experiment is reproducible.
The earlier no-clear-gain result is not a verdict on 268 MHz Old 3DS hardware.
