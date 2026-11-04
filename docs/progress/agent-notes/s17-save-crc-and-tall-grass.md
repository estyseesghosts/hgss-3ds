# S17: save CRCs, and the "communication error" at tall grass

This session fixed two save-CRC defects found in the same playthrough, then
rebuilt HeartGold and SoulSilver. The second one was not a communication error
at all: the game's generic assert screen uses the same message.

## 1. The port built the wrong CRC-16 table (patch 1906)

**Symptom.** A cartridge or emulator save that `tools/savetool.py validate`
called `IS_GOOD` did not load: the game showed New Game. Saves written by the
port itself still loaded, which is why it looked selective.

**Cause.** `src/math_util.c` had the pair

```c
GF_CRC16Init()  -> MATH_CRC16InitTable(sCRC16TablePtr)
GF_CalcCRC16()  -> MATH_CalcCRC16CCITT(sCRC16TablePtr, ...)
```

On the DS, `MATH_CRC16InitTable` builds the CCITT table, so the pair is
correct. The port's `hgss/compat/include/nitro/math/crc.h` remaps the name to
ntrtwl's reflected table (the one that pairs with `MATH_CalcCRC16`). The port
therefore built a reflected table and then updated it MSB-first: a
deterministic CRC that is not CRC-16/CCITT.

**Evidence.** For `heartgold-retail.sav`, slot 0's stored CRC is `0xa9ab`.
The old port's pair computes `0x2b01`; the port-written
`Downloads/heartgold-1.sav` stores exactly `0x2b01`. The old code made every
real save fail `ValidateSaveSectorFooter` in all four slots.

**Fix.** Patch 1906 defines `HGSS_MATH_KEEP_HGSS_CRC16_INIT` before
`math_util.c` includes anything, under `SDK_PORT_3DS`. `math_util.o` now
references `MATHi_CRC16InitTable` (the CCITT builder), not
`MATHi_CRC16InitTableRev`. The DS text is unchanged.

## 2. Legacy substruct CRCs asserted at the first wild encounter (patch 1907)

**Symptom.** Entering tall grass showed the game's screen that starts "A
communication error has occurred. You will be returned to the title screen."

**What it really was.** `PrintErrorMessageAndReset()` in
`src/error_message_reset.c` uses `msg_0041_00003`, the same text the
communication-error path uses. It is reached from `GF_AssertFail()`, and the
assert was `SaveSubstruct_AssertCRC()`.

The early port builds wrote each substruct's own CRC with the same wrong table.
The save loader only checks the slot footer, so such a save loaded; the first
`Save_*_Get()` of a substruct then asserted. The main menu's beacon watch
leaves a `CommState` alive, so `GF_AssertFail()` does not return silently as it
would on the DS: it draws the error screen. The wild encounter was simply the
first thing to read the Mystery Gift / Wi-Fi History chunks.

**Evidence.** With a temporary assert and CRC log:

```text
[assertdebug] GF_AssertFail caller=0x8579cd8 comm=0   (main menu; silent)
[assertdebug] GF_AssertFail caller=0x8579cd8 comm=1   (field; screen)
[crcdebug] idx=27 size=5760 calc=28e2 stored=f8c1
```

`0x28e2` is CRC-16/CCITT of 5,760 zero bytes, and `0xf8c1` is the old
reflected-table/CCITT-update CRC of the same bytes. The same check on index 26
(Wi-Fi History, 4,088 bytes) gives `0x213f` and `0xf8c1`. The save data was
zeroed; only the CRCs were from the old table.

**Fix.** Patch 1907 adds a port-only legacy CRC to `SaveSubstruct_AssertCRC`:
if the stored value is exactly the old table's CRC, replace it with the CCITT
value and continue. A value that matches neither still asserts, so real
corruption is not hidden. The next in-game save writes the corrected
substructs and footer.

**PC verification.** With a legacy field save, the encounter now reaches the
battle and logs `720 tris (720 opaque, 0 translucent)`, `lost: 0 clipped`, the
same result as the known-good Platinum-style battle run. Before the migration
the same save stopped at the error screen with no battle overlays.

## Investigation notes

- `gdb` could not attach: the container's amd64 image runs under emulation on
  the arm64 host, and `PTRACE_GETREGS` fails even with `--privileged`. The
  useful trail came from temporary `fprintf(stderr, ...)` instrumentation in
  `comm_state.c`, `error_handling.c`, `heap.c` and `save.c`, then rebuilding
  the PC program.
- The error screen is not always the communication-error screen. Before
  assuming a link failure, check `src/error_message_reset.c`; the message text
  is shared with `src/communication_error.c`.
- Instrumentation can change timing enough to hide a timing-sensitive path.
  The final diagnosis used the saved chunk bytes and two CRC implementations,
  not only the presence of the debug line.

## Builds

Both programs were rebuilt with 1906 + 1907:

| Program | Size | SHA-256 |
| --- | --- | --- |
| `pokeheartgold.3dsx` | 9,280,164 | `c87eaeb8f78533a12bb3787e4d4a87f3036921c80876a6b14b878a2d1103fa40` |
| `pokesoulsilver.3dsx` | 9,280,204 | `32b5f3f7389fdf1b6985dd0822931129c384102c9761745163a9e6e6eec42565` |

Checks: `tests/test_save_crc.py` (4), `tests/test_savetool.py` (18, including
`repair`), `tests/sdk/gcc_fixes/run.sh` (including group (l)), and the PC
encounter above.
