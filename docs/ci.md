# CI

## `matching.yml` (disabled)

Builds the **pinned, unpatched** decomp with MWCC and checks both SHA1s
(`docs/BASE.md`). It needs the proprietary compiler and Nitro SDK, so:

- It runs only on a **self-hosted** runner labelled `hgss-matching`, on a
  **private** repository, with the user's private folder at `/private`
  (`mwccarm/`, `NitroSDK/`).
- The SDK and MWCC are never in a public image, on a public runner, or in an
  artifact. The job has no `upload-artifact` step: ROMs and ELFs contain game
  code.
- It is gated by the repository variable `MATCHING_ENABLED == 'true'`, which is
  not set. To enable: create the runner, mount `/private` read-only into it,
  set the variable, and confirm the repository is private.
- It triggers on changes to `upstream.lock`, the installer script, or the
  workflow itself, and manually (`workflow_dispatch`).

Steps: clone `upstream.lock` -> `tools/install-ds-toolchain.sh` -> `make` ->
`sha1sum -c` for HeartGold -> `make GAME_VERSION=SOULSILVER` -> `sha1sum -c`.

## `port.yml` (Phase 1)

Public, GitHub-hosted, synthetic data only. See the file when present.
