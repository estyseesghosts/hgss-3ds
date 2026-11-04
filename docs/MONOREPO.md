# Staging the work as one monorepo

This repository is published at **https://github.com/estyseesghosts/hgss-3ds**. The fork's
original Platinum `main` is preserved as the `platinum-upstream` branch, so the newer Platinum
history (web builder, CIA forwarder) is still recoverable from the fork.

This repository is the monorepo: the HGSS port layer, shell, patch stack,
builder, tools, tests, docs **and** the vendored Platinum port (`3ds_port/`,
with our one local fix). The decompilation, the ROM/data pack, the proprietary
DS toolchain and built game artifacts are deliberately not in it. This page is
the staging plan for a public GitHub repository; the repo itself is ready
apart from the remote and the final preflight.

## What is in the monorepo

```text
pokeheartgold-3ds/
  README.md, LICENSE, NOTICE.md, AI_DISCLOSURE.md
  3ds_port/            Platinum's hardware layer (GPL-3.0) + one local fix
                      see 3ds_port/LOCAL-CHANGES.md
  hgss/                HGSS shell, game-facing glue, compat headers/runtime
  patches/             84 patches against the pinned decomp (three stacks)
  builder/             ROM verify/extract/pack (the user's own cartridge)
  scripts/             player-facing Python scripts (toolchain, ROM prep, build + SD staging)
  tools/               harness, bootstrap, surveys, matching build, reports
  tests/               Python, SDK host suites, layout probe, input scripts
  docs/                wiki, progress records, decisions, upstream patches
  upstream.lock        decomp repository + exact commit
  platinum.lock        Platinum repository + exact base commit
  .github/workflows/   port.yml (public test gate), matching.yml (disabled)
```

The only third-party source tree copied into the repo is `3ds_port/`
(Platinum, GPL-3.0). Its license headers and READMEs are kept; the one local
change (the camera-plane triangle drop) is visible in
`3ds_port/LOCAL-CHANGES.md` and
`docs/upstream/platinum-camera-point.patch`.

## What is deliberately outside it

| Not in the repo | Why | Where it comes from |
| --- | --- | --- |
| The decompilation (`ctrl-cheeb-del/pokeheartgold`) | Separate project with its own terms; huge and frequently changing; the pin is the contract | `python3 tools/bootstrap.py` fetches `upstream.lock` into `build/upstream/` and applies `patches/` |
| The ROM, save, extracted files, data pack | Game content; never redistributed | The user's own cartridge; `builder/` |
| MWCC, Nitro SDK, `license.dat` | Proprietary | The user's `~/hgss-private`; git-ignored |
| `build/`, `.3dsx`, `.smdh`, `.elf`, `.map`, `.shbin` | Build artifacts, some derived from the game/SDK | Local build; `build/` is a Docker volume |
| `dist/` (the staged SD-card folder) | Contains the `.3dsx` and the game `.pak` | `scripts/build_3dsx.py`; git-ignored |
| Golden frames, screenshots, `tests/golden/` | Game art | The user's captures; git-ignored |
| CI secrets/private runner | No secret belongs in the repo | GitHub repository settings |

The decomp being fetched rather than vendored is what keeps this a code
monorepo instead of a source dump. `make test` and the 3DS build need network
once for the fetch; the matching-build gate additionally needs MWCC and the
Nitro SDK from the private folder. A public clone can build and test without
either proprietary piece.

## Creating the GitHub repository

The repository is already published at
`https://github.com/estyseesghosts/hgss-3ds` (public, `main` tracks
`origin/main`). The steps below are what was run; use them to recreate it
elsewhere or as the checklist before a push.

```sh
# 0. Preflight (see below)
make content-check
make test
git status --short            # only local scratch, never game content

# 1. Create the remote and push main
gh repo create <owner>/hgss-3ds --public --source=. --remote=origin --push
# or, with an existing empty repository:
git remote add origin git@github.com:<owner>/hgss-3ds.git
git push -u origin main

# 2. Repository settings
#    description: a 3ds build of hgss, based off a native port of platinum
#    topics: 3ds, pokemon, heartgold, soulsilver, decompilation, homebrew, devkitpro
#    license: GPL-3.0
#    Actions: enabled (port.yml); the matching job is disabled by design
```

This fork's original Platinum `main` was preserved before the force-push:

```sh
git push origin <old-main-sha>:refs/heads/platinum-upstream
git push --force-with-lease=refs/heads/main:<old-main-sha> origin main
```

`main` is this monorepo; `platinum-upstream` is the unmodified fork state. Do
not delete it until the newer Platinum changes have been reviewed for the
port layer.

No release asset is needed to share the source. If a `.3dsx` is ever attached
to a release, it is a build artifact made from the user's own cartridge and
data pack; do not attach a pack, save, ROM or golden capture.

## The Platinum port change

`3ds_port/` is not a submodule and is not byte-identical any more. Keeping it
in the monorepo was the deliberate choice:

- A submodule would make the GitHub clone incomplete without a second repo,
  which defeats "one monorepo".
- The local change is one hunk in `nitro/src/port_gx3d.c` (`stage_triangle()`
  drops zero-area camera-plane triangles) plus its test. It is copied from
  `platinum.lock` and carries `3ds_port/LOCAL-CHANGES.md`.
- The same patch is in `docs/upstream/platinum-camera-point.patch`, ready to
  apply from Platinum's root. If the upstream PR lands, drop the local hunk
  and the patch, then re-vendor at the new pin.

To update Platinum later:

1. Change `platinum.lock`, re-vendor `3ds_port/`, and apply
   `docs/upstream/platinum-camera-point.patch` if it is still needed.
2. Update `3ds_port/LOCAL-CHANGES.md` and `docs/decisions.md`.
3. Run `make port-tests` and `make test`; the camera-plane test must pass.

To update the decomp later:

1. Change `upstream.lock` deliberately.
2. `python3 tools/bootstrap.py --clean`; fix any patch that no longer applies,
   using the patch-authoring workflow.
3. Run `make test`, `make game-pc`, `make game-3ds`, and the DS gate for both
   games (`tools/matching-build.sh` from `/work/pokeheartgold`).
4. Update `docs/progress/STATE.md` and the phase record.

## Preflight and content rules

`make content-check` (and `tests/test_repo_content.py` under `make test`) reads
`git ls-files` and fails if the index contains a ROM, save, pak, `.3dsx`,
`.smdh`, CIA, dump, proprietary archive, build directory, golden capture, or a
tracked file over the size limit. It allowlists the three original Platinum /
project images/chime that are already in the tree. It is a safety net, not a
license to add content.

Before the first push:

```sh
git status --short                    # no untracked game content that could be added later
git ls-files | grep -Ei '\.(nds|sav|pak|3dsx|cia|smdh)$'   # must be empty
git log --all --oneline --stat | grep -Ei '\.(nds|sav|pak|3dsx|cia|smdh)$'  # history audit
git count-objects -vH                 # about 2 MB, no LFS needed
make test                             # Python tests + 20 SDK suites
make content-check
```

If a content file was ever committed, do not rewrite it with a normal commit:
remove it and purge history (for example `git filter-repo`) before the first
public push. The current history has none; keep it that way.

## CI and branches

- `port.yml` is the public gate: it builds the dev container image and runs
  `bootstrap.py` + `make test` on synthetic data. It does not need a ROM, MWCC
  or the Nitro SDK. The first push at `main` passed it in **3m3s**
  ([run 41353964864](https://github.com/estyseesghosts/hgss-3ds/actions/runs/41353964864)).
- `matching.yml` is disabled: it needs the proprietary toolchain and a
  self-hosted private runner. The gate can be run locally; the result is
  recorded in `docs/progress/STATE.md`.
- Keep `main` green. One branch per change, a pull request, `make test` and
  `make content-check` green before merge. Patch changes additionally need the
  DS gate for both HG and SS.

## Current pins and state

- `upstream.lock`: `8cbb4a19ab781611ff1767035dcaea6b47baeded`
- `platinum.lock`: `359fbec80a5c0664d4fa72caaae87465b640f9d6`
  plus `docs/upstream/platinum-camera-point.patch`
- Patches: **82** (NitroSDK 16, NitroSystem 4, pokeheartgold 62)
- DS gate at 78: HG and SS retail SHA1s pass (1904-1907, 0304 and 0605 are port-only)
- Latest `.3dsx`: HeartGold 9,279,628 bytes (`sha256 92381334…5285`); SoulSilver
  9,279,668 bytes (`sha256 c2e9a50e…e4cf`); not in git
