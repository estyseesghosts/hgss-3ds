#!/usr/bin/env bash
# Host test for the SDK_PORT_3DS ARM7 sound driver (patch 1401): the sound-register
# redirect, the SND_Init threading skip, the command-queue pop, the alarm routing, the
# channel-start transitions, the full pointer setup and the shared-work lock in
# sub/lib/src/sound_{main,command,alarm,channel,driver,ex_channel}.c and
# lib/NitroSDK/src/snd/SND_work.c (plus the static_init.h half of 1401, proved by the
# .sinit section check in the patch message rather than here).
# Also builds the ARM9 command sender (SND_command.c and snd_flush_command.c) as a
# second binary and checks patch 1404: a blocking flush reaches PXI and its finished
# tag advances once the reply is reaped.
# Usage: tests/sdk/arm7_sound/run.sh [ROOT]   (patched decomp tree, default build/upstream;
# run inside the dev container). Uses the survey tool's defines/flags/includes, with
# -DSDK_ARM7 for the sub/ files and -DSDK_ARM9 for SND_work.c.
# Exits non-zero if a build fails or any check fails.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ROOT=$(cd "${1:-$REPO/build/upstream}" && pwd)
GCC=${GCC:-gcc}
read -r -a FLAGS <<< "$(python3 -I -c '
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import sdk_compile_survey as s
print(" ".join([f for f in s.DEFINES if f != "-DSDK_ARM9"] + [f for f in s.CFLAGS if f != "-fgnu89-inline"] + s.includes(sys.argv[2])))
' "$REPO/tools" "$ROOT")"
read -r -a FLAGS9 <<< "$(python3 -I -c '
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import sdk_compile_survey as s
print(" ".join(s.DEFINES + [f for f in s.CFLAGS if f != "-fgnu89-inline"] + s.includes(sys.argv[2])))
' "$REPO/tools" "$ROOT")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/arm7_sound_test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

cc7() { "$GCC" -m32 -malign-double -DSDK_ARM7 "${FLAGS[@]}" -c "$1" -o "$2"; }

cc7 "$ROOT/sub/lib/src/sound_main.c" "$OUT/sound_main.o"
cc7 "$ROOT/sub/lib/src/sound_command.c" "$OUT/sound_command.o"
cc7 "$ROOT/sub/lib/src/sound_alarm.c" "$OUT/sound_alarm.o"
cc7 "$ROOT/sub/lib/src/sound_channel.c" "$OUT/sound_channel.o"
cc7 "$ROOT/sub/lib/src/sound_driver.c" "$OUT/sound_driver.o"
cc7 "$ROOT/sub/lib/src/sound_ex_channel.c" "$OUT/sound_ex_channel.o"
cc7 "$ROOT/sub/lib/src/sound_shared_work.c" "$OUT/sound_shared_work.o"
cc7 "$ROOT/sub/lib/src/sound_seq_player.c" "$OUT/sound_seq_player.o"
"$GCC" -m32 -malign-double "${FLAGS9[@]}" -c "$ROOT/lib/NitroSDK/src/snd/SND_work.c" \
    -o "$OUT/SND_work.o"
"$GCC" -m32 -malign-double -DSDK_ARM7 "${FLAGS[@]}" -c "$HERE/test_arm7_sound.c" \
    -o "$OUT/test_arm7_sound.o"
"$GCC" -m32 "$OUT/test_arm7_sound.o" "$OUT/sound_main.o" "$OUT/sound_command.o" \
    "$OUT/sound_alarm.o" "$OUT/sound_channel.o" "$OUT/sound_driver.o" \
    "$OUT/sound_ex_channel.o" "$OUT/sound_shared_work.o" "$OUT/sound_seq_player.o" \
    "$OUT/SND_work.o" -o "$OUT/test_arm7_sound"

"$OUT/test_arm7_sound"

# ARM9 command sender (patch 1404). Separate binary: its SND_CommandInit has the
# same name as the ARM7 driver's, which the objects above compile with -DSDK_ARM7.
"$GCC" -m32 -malign-double "${FLAGS9[@]}" -c "$ROOT/lib/NitroSDK/src/snd/SND_command.c" \
    -o "$OUT/SND_command_arm9.o"
"$GCC" -m32 -malign-double "${FLAGS9[@]}" -c "$ROOT/lib/NitroSDK/src/snd/snd_flush_command.c" \
    -o "$OUT/snd_flush_command.o"
"$GCC" -m32 -malign-double "${FLAGS9[@]}" -c "$HERE/test_arm9_flush.c" \
    -o "$OUT/test_arm9_flush.o"
"$GCC" -m32 "$OUT/test_arm9_flush.o" "$OUT/SND_command_arm9.o" \
    "$OUT/snd_flush_command.o" "$OUT/SND_work.o" -o "$OUT/test_arm9_flush"

"$OUT/test_arm9_flush"
