# Half A lane L wave 1

## Slice

L1–L3 are implemented behind `lower30`, `drawerAB`, `refHashB` and
`refHashDiag` in `port.ini`; all default off.  Input and the virtual DS tick
remain independent of the lower-screen offer cadence.

## Files changed by this slice

- `hgss/game/app/main_3ds.c` — lower offer cadence, A-before-B publication and
  bounded mixed-rate lower policy; option parsing.
- `3ds_port/nitro/src/port_render2d.c` — engine-B referenced-range signature
  and diagnostic full-hash change-decision cross-check.

The worktree already contained unrelated changes, including the texture
profile diagnostic region in `main_3ds.c`; that region was not edited by this
slice.

## Hash accounting

The legacy engine-B VRAM check reads 0x4c000 bytes (304 KiB) per full check.
The L3 path hashes only enabled BG maps and their referenced tile prefixes,
enabled B extended-palette banks, and graphics ranges for visible OAM entries.
It records `sRefHashBytes`; the diagnostic full path records `sFullHashBytes`.
Exact per-fixture byte totals require the fixture runner or a game log with
`refHashDiag=1`; no game-content fixtures were available in this session.

The OAM visibility test preserves the GBATEK rule used by the renderer:
attr0 bit 9 is hidden only when affine bit 8 is clear; in affine mode it is
the double-size flag.  Affine matrices remain in the small/register
signature.

## Ordering evidence

The drawer now renders and publishes A, signals the compose handoff, and only
then starts B.  With `drawerAB=1`, B may be deferred while the snapshot is
younger than three V-blanks; age three forces B.  Engine swaps and differing
snapshot master-brightness values force a coherent pair.  The lower image is
otherwise left at its last completed publication, and the pair is explicitly
mixed-rate.

## Commands and results

- `git diff --check` — pass.
- `python3 -m py_compile tests/test_publish_profile.py` — pass (existing test
  syntax check).
- `python3 tools/hgss_nitro.py make --target host32 --out /work/out/halfA-l1l2l3 lib -k -j8` — skipped by environment: `/work` is read-only on this macOS checkout.
- Same build with a repo-local output — blocked before compilation because the
  native Apple clang rejects the repository's GCC-only
  `-fno-aggressive-loop-optimizations` flag.  The host32/container gate is
  still required by the lead session.

## Open questions

- The referenced signature uses the taken OAM snapshot when one is active;
  the full host32 fixture/3DS build must still validate snapshot timing and
  promote any diagnostic mismatch to the fallback.
- The requested startup build-ID naming for each option combination belongs
  to the lead's packaging/build-ID policy; this slice logs each selected
  experiment but does not alter generated build IDs.
