# Phase 2 Slice C: GCC-fix patch series 1501-1508

Date: 2026-10-31. Worked in the dev container, in a scratch clone of `build/upstream` at
`4e7ca82`, exported with `git format-patch`. No patch touches `3ds_port/`, `build/upstream`,
`tools/bootstrap.py` or the game data.

## Patches

| Patch | Files | Change |
| --- | --- | --- |
| 1501 | `lib/src/nitrocrypto_rc4.c`, `lib/src/nitrocrypto_rc4_fast.c` | Plain-C RC4 under `SDK_PORT_3DS`, same context bytes as the MWCC `asm` bodies. |
| 1502 | `src/berry_pots_app_tasks.c`, `src/comm_state.c` | A file-scope macro expands to `static` on the DS and to nothing on the port, so GCC stops rejecting static definitions the internal headers declare extern. |
| 1503 | `src/field/weather_manager.c` | Give the two snow tables an `int` element type and `memcpy` the arrays into the locals (C arrays are not assignable). |
| 1504 | `include/field_very_tall_grass.h` | Include `billboard_internal.h` under the macro so the extern `BillboardSequenceRange[2]` has a complete element type. |
| 1505 | `src/to91_msl_array_new.cpp`, `src/to91_msl_scalar_new.cpp` | Empty `MSL_THROW_BAD_ALLOC` / `MSL_THROW_NOTHING` under the macro, replacing the C++17-removed dynamic exception specifications. |
| 1506 | `src/script_manager.c`, `src/frontier/frontier_system.c` | Read the word into a local before `ptr +=`; MWCC read `ptr` after `ReadWord` advanced it (verified with MWCC 2.0/sp2p2), GCC may read it before. |
| 1507 | `src/overlay_03/overlay_03_scrcmds.c` | Read the destination halfword first, then the index halfword, matching MWCC's left-then-right assignment evaluation and the script macro. |
| 1508 | `src/battle/battle_anim_funcs.c`, `src/battle/battle_util.c`, `src/battle/trainer_ai.c` | Initialize to 0 the `age`/`life`, `type` and `result` locals that the empty-particle / fog Weather Ball paths can read unset. |

The two order-sensitive assertions in 1506/1507 were checked by compiling a synthetic
function with MWCC 2.0/sp2p2 and reading the `.o` with `objdump`: `*p += read()` calls
`read` before loading `*p`, and `*pa(a()) = g(pa(b()))` evaluates the left side first.

## DS neutrality

For every touched file, the patched tree and `build/upstream` were preprocessed with
`gcc-10 -E -P` (or `g++-10` for the `.cpp` files) with
`-DHEARTGOLD -DGAME_REMASTER=0 -DENGLISH -DPM_KEEP_ASSERTS -DSDK_ARM9 -DSDK_CODE_ARM -DSDK_FINALROM`
and **without** `-DSDK_PORT_3DS`. All 14 files diffed to 0 lines. The same check was run on
the `/work/wt/apply` clone after `git am` of the eight patches.

## Verification

* `HGSS_UPSTREAM=/work/wt/apply bash tests/game/rc4/run.sh` ->
  `rc4: all known-answer tests passed` (Key/Plaintext, Wiki/pedia, Secret/Attack at dawn,
  RFC 6229 0102030405, both contexts, plus split-call continuation).
* `python3 -I -m unittest discover -s tests` -> exit 0.
* `gcc-10 -m32 -std=gnu11 -fsyntax-only` with the port include set:
  `lib/src/nitrocrypto_rc4.c`, `lib/src/nitrocrypto_rc4_fast.c`, `src/script_manager.c`,
  `src/frontier/frontier_system.c`, `src/overlay_03/overlay_03_scrcmds.c`,
  `src/berry_pots_app_tasks.c`, `src/battle/battle_util.c`, `src/battle/trainer_ai.c`,
  `src/battle/battle_anim_funcs.c` all exit 0.
* Three touched files still fail for problems outside this slice; verbatim errors are in the
  slice report. `weather_manager.c` has label-before-declaration errors, `comm_state.c` has
  the pre-existing `MATHRandContext32` typedef conflict plus a `global.h` read-only
  assignment and one label, and the two MSL `.cpp` files still have the `operator new`
  `size_t` signature errors (the exception-specification errors are gone).

## Evaluation-order audit

Method: a script stripped comments and string literals, split each file into statements at
`;`, `{` and `}`, and counted calls to the readers that advance a script cursor:
`ScriptReadHalfword`, `ScriptReadWord`, `ScriptReadByte`, `ScriptGetVar`,
`ScriptGetVarPointer`, `FrontierScriptContext_ReadHalfWord`, `FrontierScriptContext_ReadWord`
and `FrontierScriptContext_ReadByte`. It flagged a statement with two or more such reads, or
one read plus `script_ptr`/`scriptPtr` increment/compound assignment. The audit covered 24
`src/scrcmd*.c`, `src/script_manager.c`, all `src/frontier/*.c` (47 files) and
`src/overlay_03/overlay_03_scrcmds.c`: 73 files and 1,126 reader calls.

Only three statements were flagged, and all three are fixed by 1506/1507:

* `src/script_manager.c:343` `ctx->script_ptr += ScriptReadWord(ctx);`
* `src/frontier/frontier_system.c:158` `ctx->scriptPtr += FrontierScriptContext_ReadWord(ctx);`
* `src/overlay_03/overlay_03_scrcmds.c:40` two `ScriptReadHalfword` calls in one assignment.

Reader-call counts for the files with at least one call (all other `src/frontier/*.c` files
had zero):

```
5 scrcmd_12.c            18 scrcmd_17.c             13 scrcmd_8.c
24 scrcmd_9.c             5 scrcmd_apricorn.c       29 scrcmd_battle.c
7 scrcmd_battle_arcade.c  7 scrcmd_battle_castle.c  19 scrcmd_battle_hall.c
588 scrcmd_c.c            17 scrcmd_daycare.c        3 scrcmd_dppl_prizes.c
6 scrcmd_fossils.c       18 scrcmd_items.c           8 scrcmd_link_sync.c
7 scrcmd_lottery.c        6 scrcmd_mart.c             7 scrcmd_money.c
8 scrcmd_move_relearner.c 8 scrcmd_mystery_gift.c   90 scrcmd_party.c
9 scrcmd_size_record.c   24 scrcmd_sound.c          85 scrcmd_strbuf.c
2 script_manager.c        1 frontier_cmd_arcade.c    6 frontier_cmd_castle.c
1 frontier_cmd_create_sprites.c  4 frontier_cmd_factory.c  4 frontier_cmd_hall.c
4 frontier_cmd_link_lobby.c  2 frontier_cmd_sound.c   53 frontier_script_commands.c
13 frontier_script_commands_ext.c  5 frontier_script_commands_misc.c
9 frontier_script_context.c  2 frontier_system.c      9 overlay_03_scrcmds.c
```

Direct cursor mutations were also searched (`script_ptr++`, `scriptPtr++`,
`script_ptr +=`, `scriptPtr +=`): the only multi-read or read-plus-mutation statements are
the three above. Pure accessors (`GetVarPointer`, `FieldSystem_VarGet`,
`FrontierScript_GetVar`, `FrontierScript_GetVarPtr`) have no side effects and were not
treated as reads; a statement that nests one of them around one `ScriptReadHalfword` is
sequenced correctly and was not changed.

## Deliberately not changed

* `src/field/weather_manager.c` label-before-declaration errors: a separate GCC fix, not
  part of the typed-table work.
* `src/comm_state.c` `MATHRandContext32` typedef conflict, the `global.h` read-only
  `linkedStates` assignment and the label at line 1820: separate pre-existing GCC problems.
* `src/to91_msl_*_new.cpp` `operator new` first-parameter type: a separate `size_t`/
  declaration fix.
* No 3DS (devkitARM) build was attempted; the port's game build target does not exist yet
  for HGSS.
