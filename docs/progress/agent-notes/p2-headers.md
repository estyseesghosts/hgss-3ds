# P2 Slice A2: prepare_game_headers.py

## Done (container `pokeheartgold-3ds`, repo at `0b81803`)

- `tools/prepare_game_headers.py` (new, ~1000 lines): generates every
  build-time header the HGSS C/C++/asm build needs into any checkout
  (`--root`, default `build/upstream`) with no ROM, MWCC, wine, or network.
  Pure-Python byte ports, each citing the exact upstream source it mirrors:
  - `lib/include/nitro/fx/fx_const.h` from `tools/gen_fx_consts/fx_const.csv`
    (`MakeFix`/`GetFxWidth`/`main.c` formatting; guard from the
    checkout-relative path).
  - `files/msgdata/msg/msg_0729.gmm` from `trainers.json` + `trname.json.txt`
    (purpose-built renderer for that template's `{% set %}`/`## for`/
    `fmtInt(i, 5, 2)`/`{{ }}`, then the Makefile's `&` → `&amp;` sed step).
  - All 829 `files/msgdata/msg/msg_*.h` from `*.gmm` (`GMM::WriteGmmHeader`:
    guard from the `-H` path, `//` comments from byte-based `SplitMessage`
    300-chunks, garbage rows blanked from Japanese bytes, quirky
    `rowname_pref` fallback for id-less rows).
  - 38 nitroarc `--index-namespace` `.naix` (explicit `.narcorder` first, then
    C-`strcmp`-sorted implicit tail minus `.narcignore`/`--exclude`/default
    excludes; generated outputs derived per `files/**/*.mk`: `.bin` per
    `.gmm`/`.s`/`.json`, graphics per `.png`/`.pal`, `build-pokegra` palette
    fallback incl. the `0000` copy-overwrite dedup, `otherpoke.txt` outputs).
  - 14 o2narc `.naix` (`-n` counts from JSON: trainers 738, baseStats 508,
    `NUM_SPECIES + 1` = 508 via `SPECIES_ROTOM_MOW`, encounters 142, photos
    93, tables 540, `gDungeons`+`gOverworlds`+pairs 3962, single-symbol
    `gData` = 1; `-N` names = 11 `mon_stats_*` + 91 `sort_order_*`).
  - 3 csv2bin `--naix` `.naix` (counts from CSV rows: 8/514/566).
  - `zukan_enc.naix` / `zukan_hw_data.naix` aliases via case-sensitive
    `s/_<shortname>//g` (`--shortname`, default `silver` — see below).
  - Overlay ID ld-script/header via `game_overlays.render_ids` /
    `render_ids_header` (`--ids-output` / `--header`; 129 overlays).
- `tests/test_prepare_game_headers.py` (new): 22 tests, synthetic inputs
  only, exit 0 via `python3 -I -m unittest discover -s tests -p
  "test_prepare_game_headers.py"`.

## Measured (container)

- Scratch clone `/work/wt/p2_headers` (`git clone -s /work/pokeheartgold`,
  `checkout -f 8cbb4a19ab78`): `python3 tools/prepare_game_headers.py --root
  /work/wt/p2_headers` → 888 files, rerun 0 updated (idempotent).
- Against `/work/pokeheartgold` (built tree): **886/886** ground-truth files
  (56 `.naix` + 829 `msg_*.h` + `fx_const.h`) **byte-identical** (`cmp`);
  `msg_0729.gmm` identical too. Ground-truth list came from `git status
  --ignored -s` filtered to `.naix`/`msg_*.h`/`fx_const.h`.
- One deliberate extra: `files/arc/headbutt.heartgold.us.naix` (same 540
  entries, heartgold stem). The observed tree has only the soulsilver
  variant; the `.mk` builds one per `$(buildname)` and the script emits both.
- `zukan_enc.naix` staleness note: in the observed tree the alias equals
  `sed 's/_silver//g'` of the silver variant (guard keeps uppercase
  `_SILVER`; only lowercase names strip). The script reproduces exactly that
  for `--shortname silver` (default, matching the observed tree); use
  `--shortname gold` after a heartgold-last build. Verified gold-alias
  self-consistency the same way.
- Deliberately NOT generated (no index rule fires in the observed build):
  checked-in narcs with empty rules (`font`, `wotbl`, `trtbl`, `trtblofs`,
  `touch_subwindow`, `kowaza_list`, `johtozukan`, `arealight`, …),
  `pms_aikotoba` (`nitroarc` without `--index`), `safari_enc` (`o2narc`
  without `-n`), musashi `.bin`s (csv2bin without `--naix`, not narcs).
  `include/**/*.naix`: nothing references any — all includes resolve under
  `files/`.

## Pitfalls hit (all fixed, none worked around)

- `SplitMessage` is byte-based: multibyte messages split mid-character, so
  headers are raw bytes, not UTF-8 (first run crashed on decode; output is
  now bytes, matching msgenc's raw `ofstream` writes).
- pkegra `0000-04/-05` palettes: generated names collide with the recipe's
  `cp` targets — dedupe, because the directory walk cannot see doubles.
- `poke_icon` globs the narc's *parent* dir, `pokefoot` the stem dir
  (narc name == dir name there); one wrong guess each way before the diff
  caught both.
- `fmtInt`/`rowname_pref`/sed-case quirks ported literally and pinned by
  tests (no id-less rows exist; the fallback is near-dead but exact).
