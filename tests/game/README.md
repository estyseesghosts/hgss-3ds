# Game 3DS type-contract regression tests

NitroSDK defines `s32`/`u32`/`fx32` as `signed long`/`unsigned long`/`signed
long`. Metrowerks and the host32 build (gcc-10) accept an `int *` where a
`s32 *` is wanted because both are 32-bit, but devkitARM 16.1 (GCC 14) turns
`-Wincompatible-pointer-types` into an error. The `17xx` patches make the
affected game call sites type-correct under `SDK_PORT_3DS` and leave the
original DS text under `#else`.

`tests/test_game_3ds_types.py` is a synthetic check of the two fix patterns
used by those patches:

1. retype the local (or table/struct field) to the callee's exact type;
2. give a .c-local prototype the real `s32`/`fx32` signature instead of the
   MWCC-compatible `int` one.

The test compiles small stand-alone translation units with
`-Wincompatible-pointer-types -Werror` and checks that the corrected pattern
builds while the pre-fix `int *` pattern is rejected. It needs no game
data and no devkitARM.
