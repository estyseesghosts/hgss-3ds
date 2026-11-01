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

Steps: clone `upstream.lock` -> `tools/install-ds-toolchain.sh` ->
`tools/matching-build.sh <dir> HEARTGOLD` (plain `make` does not match in the container: the script
keeps five sections the linker strips, see `docs/progress/phase-0.md`) -> `sha1sum -c` ->
the same for `SOULSILVER`.

## `port.yml` (Phase 1)

Public, GitHub-hosted, synthetic data only. `.github/workflows/port.yml` builds the dev container image,
then runs `python3 tools/bootstrap.py && make test` (tool tests + the `tests/sdk` host suites). It has not run on
GitHub yet (no remote); a local dry run of the same steps in a fresh clone passes. The layout probe needs MWCC
and is not part of it. The DS gate (`matching.yml`) was run locally at **78 patches** and passed for both
games; the workflow remains disabled until the private runner and variable exist.
