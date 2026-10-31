# P2 Slice B: `s_to_gas.py` — MWAS `.s` → GAS

Date: 2026-10-31 (container `hgss-p1`, repo at `0b81803` + concurrent
uncommitted work from other slices; my files only: `tools/s_to_gas.py`,
`tests/test_s_to_gas.py`, this note).

## What was built

`tools/s_to_gas.py`: converts the 150 ARM9 MWAS data files (`asm/*.s`
80, `lib/asm/*.s` 18, `lib/NitroSDK/asm/*.s` 3, `lib/MSL_C/asm/*.s` 28,
`lib/NitroDWC/asm/*.s` 21) to GAS. CLI:
`--root <checkout> <input.s> -o <out.s|out.o|-> [--cc C] [--cflags ...]`
(`--cflags` is trailing remainder args, must come last — plain
`--cflags -m32` does not survive argparse's option parsing otherwise).

Pipeline: recursive `.include` inlining (brief's MWASFLAGS order) →
blank unresolvable C `#include`s with a stderr warning → `cc -E -x
assembler-with-cpp -DSDK_ASM -DPM_ASM` (expands `FS_EXTERN_OVERLAY` and
`#define`s; C decls/typedefs/enums stripped by keeping only
`<stdin>`/`.s`/`.inc`-attributed lines via cpp linemarkers) → GAS
mapping. `-o *.o` assembles the GAS text with `--cc` + `--cflags`.

Translation decisions (design basis, all verified by assembly + ELF diff):

- `;` comments stripped escape-aware (`\"`/`\\` handled); `;` inside
  strings (e.g. `dwc_lobby_chat_data.s:2427`) preserved. `//` comments go
  away in cpp; `http://…` inside strings survives it.
- `.public` → `.global`; `.extern` kept (accepted no-op on both GASes,
  verified).
- Bare `.rodata` → `.section .rodata,"a",%progbits` + `.balign 4`.
- MWAS `.section n,a,t,f`: middle field is section type (8 = NOBITS, only
  on `.bss*`; 1 = PROGBITS), last field is ELF `sh_flags` (3 = WA,
  2 = A) → `.bss*` gets `"aw",%nobits` + `.balign <align>`; align value
  is bytes (address-comment arithmetic in sources advances by exact data
  widths; same `.balign 4` convention as the Platinum port).
- `.type s,@f` → `.type s,%f` (works on x86 and ARM GAS).
- `cw.inc exception` macro (`?exc$\what` is unrepresentable in GAS)
  replaced with a macro emitting unique local `.Lexc_\@` per invocation;
  `.type/.global/.size` dropped (pure `.exceptix` data; avoids cross-file
  global collisions). No `what` repeats across files (checked).
- `.word` → `.4byte` — the critical width fix: ARM `.word` = 4 B, x86
  `.word` = 2 B (truncation warning). Without this every pointer table
  silently corrupts in `pokeheartgold-pc`. `.4byte` is 4 B on both
  (`R_ARM_ABS32`/`R_386_32`).
- `lib/MSL_C/asm/*.s` (28 files): all pure data tables + `exception`
  entries, zero ARM instructions. Proof: they assemble under x86 GAS,
  which would reject ARM opcodes. So they convert with this tool; no
  newlib substitution needed for asm.

## Measured results (container)

- 150/150 assemble with 0 errors, 0 warnings under both
  `gcc-10 -m32 -c` and `arm-none-eabi-gcc -march=armv6k -mfpu=vfp
  -mfloat-abi=hard -c` (outputs in `/work/out/acc/{gas,x86,arm}`).
- ARM-vs-x86 ELF compare over all 150: 4949 defined symbols, 0
  name/type/size mismatches; 0 section name/size/flag mismatches; 0
  alignment mismatches. Only delta: ARM `$d` mapping symbols (standard
  ARM ELF, also emitted by MWAS; x86 has no equivalent).
- Spot checks: `.bss.unified_*` → NOBITS/WA/Al=4; `.rodata.*` → A/Al=4;
  `.exceptix` present with MWAS-identical padding (10 + 2 pad + 12…);
  `.rel.exceptix` relocations intact.
- `python3 -I -m unittest discover -s tests -p "test_s_to_gas.py"`:
  24 tests OK. Full `discover -s tests`: 128 tests OK (5 consecutive
  runs; see flake note below).

## The 6 `msgdata/msg/*.h` files

`bag_app`, `field_effect_renderers`, `field_emote_mark`,
`overlay_01_sprite`, `pokemon_summary` include msg headers but use none
of their defines → skip-with-warning is correct. Only
`vs_recorder_screen_data.s` uses `msg_0013_NNNNN` constants (would become
170 silent undefined refs). Fix used here: built the tree's own
`tools/msgenc` (native C++, unmodified sources) and generated the 5 real
headers into `/work/out/msggen`, passed via `--cflags -I/work/out/msggen`
→ 0 warnings, 0 msg undefined symbols, real message IDs in the tables
(e.g. `msg_0013_00130` → 130). The Makefile must generate msg headers
before assembling (as `msg.mk` already does for the DS build); build
artifacts were removed from `build/upstream/tools/msgenc` afterwards.
`event_D24R0204.h` (scr_seq) already exists in-tree.

## Open items / follow-ups for the lead

- MWAS-reference `.o` comparison (`/work/pokeheartgold/...`) was
  impossible here (absent in this container); ARM-vs-x86 equivalence is
  proven instead. When reference exists, compare `nm` globals (expect
  `?exc$*`/`$d` naming deltas only) and `sh_addralign`.
- Bare `.text`/`.data`/`.bss` keep GAS defaults (Al=1 in our objects);
  MWAS's bare-section alignment is unverified — the reference comparison
  should confirm; if MWAS says 4, add explicit `.balign 4` (one-line
  change + test).
- Missing-header skip is warn-only: if a future `.s` ever *uses* a
  missing header's define it becomes an undefined symbol, not an error.
  The Makefile path (headers generated) is unaffected.
- Not for this slice: wiring `s_to_gas.py` into the game build
  (`game_sources.py`/Makefile) — the `--cc/--cflags` interface is ready
  for it.
- Flake note: one full-suite run showed `Ran 127 … FAILED (failures=1)`
  while another slice was concurrently writing `tests/test_game_stubs.py`
  into the same tree; 5 follow-up full runs are 128 OK and my file is
  10/10 OK in isolation. No action; just do not trust a red run taken
  during concurrent slice landings.
- Prototype credit: `/tmp/swave_prep2.py` + `/tmp/swave_full.sh`
  (explorer) established the pipeline shape and the 144/150 baseline
  (6 msg-header failures); this slice productized it, fixed `.word`
  width, `--cflags` parsing, and the `vs_recorder` constants.
