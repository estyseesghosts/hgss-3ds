# Base commits

Both pins were taken on **2026-10-30** (the date Phase 0 began).

| Role | Repository | Commit | Commit date | Lock file |
| --- | --- | --- | --- | --- |
| Decompilation (built, patched at build time) | https://github.com/ctrl-cheeb-del/pokeheartgold (`master`) | `8cbb4a19ab781611ff1767035dcaea6b47baeded` | 2026-10-30 12:40:22 UTC | `upstream.lock` |
| Hardware layer (`3ds_port/`, tools; vendored) | https://github.com/GabeSud0/pokeplatinum-3ds (`main`) | `359fbec80a5c0664d4fa72caaae87465b640f9d6` | 2026-10-30 14:41:19 -0700 | `platinum.lock` |

`pret/pokeheartgold` is added as a second remote in the decomp checkout as a
fallback upstream; it is not pinned.

## Matching targets (must stay byte-identical)

| Version | SHA1 |
| --- | --- |
| HeartGold US | `4fcded0e2713dc03929845de631d0932ea2b5a37` |
| SoulSilver US | `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` |

## Bumping a pin

Only at a phase boundary: rebase the patches, re-run every test, update the
lock file and this table in the same commit.
