# Contributing

## The one rule that can't be broken

**Never commit game content or proprietary tools.** No ROMs, files extracted
from a ROM, golden screenshots (they show game art), save files, MWCC or Nitro
SDK files, and no built game binaries. Check the list of changed files before
every commit. If a `.nds`, `.sav`, `.narc`, `.exe`, `.dll` or a game image shows
up, stop.

Builds that contain game code (`.3dsx`, `.cia`, PC binaries linked with game
code) stay local. CI uploads only what `tools/check_ci_artifacts.py` allows.
No Docker image layer may contain proprietary files; they enter only through
the read-only `/private` mount.

## How changes get in

1. Branch per change (`docs/...`, `port/...`, `tools/...`).
2. Small, reviewable commits: one logical change each.
3. Pull request into `main`, describing what changed and how it was tested.
4. CI must be green.

## Changing the decompilation or the SDK

The decomp is never edited in place. `tools/bootstrap.py` clones it into
`build/upstream/` and applies `patches/NitroSDK/`, `patches/NitroSystem/` and
`patches/pokeheartgold/` with `git am`. To change one, edit inside
`build/upstream/`, commit, and `git format-patch` into `patches/`. Each patch
message must explain the MWCC/DS behavior it reproduces and cite the source
(file and line). Port-only behavior is guarded by `SDK_PORT_3DS`.

The DS matching build must stay byte-identical (HG US
`4fcded0e2713dc03929845de631d0932ea2b5a37`, SS US
`f8dc38ea20c17541a43b58c5e6d18c1732c7e582`), and the save format must never
change.

`3ds_port/` stays byte-identical to the pinned Platinum commit.

## Code style

- **C:** match the surrounding code; comments explain *why*.
- **Python:** 3.10+, standard library only unless there's a good reason.

## Commit messages

Imperative summary line, blank line, then what and why. Commits written with
AI help end with `Co-Authored-By: Claude <noreply@anthropic.com>`.
