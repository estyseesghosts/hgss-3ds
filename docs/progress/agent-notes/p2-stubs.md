# P2 slice E — Wi-Fi/WM/dsprot stubs

## What was built

- `tools/make_game_wifi_stubs.py`: adapted from
  pokeplatinum-3ds's script of the same name. Same CLI (`--nm --headers
  --sources --output objects...`) and same `undefined - defined` selection,
  plus: `int64_t`/`uint64_t` in the 64-bit-return scan; an `OVERRIDES` table
  of audited non-zero results; and exact WVR pair bodies (`cb(arg, 0)` +
  `return 1`). Mach-O `nm` prefixes symbols with `_`; names are matched and
  emitted in C spelling so one output links on ELF and Mach-O.
- `hgss/game/stubs/wm_stubs.c`: hand-written "wireless off" WM_* API layer
  (53 public `WM_*` + `WMi_GetStatusAddress`). No SDK headers; `NitroPort_*`
  only declared.
- `hgss/game/stubs/dsprot_stubs.c`: genuine-cart dsprot (0 for
  Flashcart/Emulator/Dummy, 1 for the three Nots; callback invoked exactly
  when the check passes, per `dsprot_main.c`).
- `tests/test_game_stubs.py`: 9 tests, all synthetic (temp-dir .o files,
  fake headers, shim `nitro_port.h`).

## Audit results (all callers read in build/upstream/src)

- Real `wm_*.c` is compiled into libnitro by `hgss_nitro.py` (only `wvr.c`
  and `mb/` are excluded), and pokeplatinum-3ds does the same — but real WM
  returns `OPERATING` and waits for an ARM7 reply that never comes, so every
  wireless state machine would stall. Hence API-level stubs in game code:
  with all `WM_*` satisfied by `wm_stubs.o`, the linker never pulls the
  SDK's `wm_*.o` out of `libnitro.a` (only `mb/`, also excluded, references
  `WM_*` otherwise).
- Returns that matter (everything else: default 0 / DISABLE):
  - `WM_SetPortCallback`/`WM_SetIndCallback` → SUCCESS (local-only in real
    WM; `Wireless_SetReceiveCallback` spins `for(;;){}` otherwise).
  - `WM_GetAllowedChannel` → 0x8000 (the game's own no-hardware sentinel).
  - `SOC_Socket`/`SOC_Startup` → -1 (callers test `== -1`/`< 0`).
  - `DWC_CheckInet` → 1 (then `GetInetStatus`=NOT_INITIALIZED → error; 0
    would spin in WAIT_LINK). `DWC_NASLoginProcess`/`DWC_SVLProcess` → 4
    (ERROR; 0 spins, SVL hard in `for(;;)`). `DWC_AC_Process` → -1,
    `DWC_CleanupInetAsync` → 1.
  - `WM_ReadStatus` leaves the struct untouched: all three
    `do/while(state != POWER_ON)` loops are guarded by OPERATING checks that
    fail first. `WMi_GetStatusAddress` → NULL (only reached from a callback
    that never fires).
- dsprot: `0xDCE6A1 % 3433 == 0xDCE6A1 % 4217 == 0x2AAACF % 2221 == 0` and
  `2441*4073` exact, verified numerically, so genuine values trip nothing.
- 64-bit returns in scope: `DWC_CreateFriendKey`, `DWC_GetFriendKey`,
  `DWC_Acc_CreateFriendKey`, `DWCi_Acc_GetUserId`, `DWCi_Acc_GetFriendKey`,
  `DWCi_Util_WiFiId_scrambleUid` (confirmed against real headers).

## Verification

- `python3 -I -m unittest discover -s tests -p "test_game_stubs.py"`: 9/9
  OK (macOS, Apple clang). Full `discover -s tests`: 128 OK, 14 skipped
  (pre-existing skips).
- Stubs compile under `-Wall -Wextra -Werror` (host and `-m32`); 32-bit
  layout of `WMOtherElementResults` proven 132 bytes (element@4, body@4) via
  static asserts under `-m32`.
- Generator idempotent; `wide_returns` spot-checked on real upstream headers.
- Skipped (no dev container here): `hgss_nitro.py` host32 lib build,
  devkitARM build, DS-side gate. No game data used anywhere.

## Follow-ups for the lead / Phase 2

- `comm_wireless.c` declares `WVR_TerminateAsync` with a 1-arg callback
  while `wvr.c` defines it with 2 (`WvrAsyncCallback`); the stub uses the
  canonical 2-arg form. A GCC `-Wincompatible-pointer-types` note at the
  game call site belongs to the Phase 2 GCC-fix patch series.
- `docs/wiki/Tools-Reference.md` still lists `make_game_wifi_stubs.py` as
  "not in the repo"; needs a one-line update (outside this slice's paths).
- When the game link is wired, confirm no SDK `wm_*.o` is pulled (e.g. `nm`
  the link for duplicate `WM_Initialize`); if some other TU references an
  unstubbed `WM_*`, add it to `wm_stubs.c` rather than stubbing the symbol.
