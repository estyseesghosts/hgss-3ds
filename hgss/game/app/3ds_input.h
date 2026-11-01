/*
 * The PC run's scripted input, replayed on the 3DS.
 *
 * The file format is the one tests/inputs/SCENE.txt uses (see
 * tools/golden/capture.lua):
 *
 *   every: 5
 *   end: 20000
 *   0 -
 *   6550 START
 *   8050 touch 128 176
 *
 * A button line holds its buttons for six frames, exactly like the PC
 * runner's --press. A touch line holds the lower-screen position for six
 * frames, like --touch. `every` is ignored on the 3DS (it only controls how
 * often the PC writes screenshots) but is kept so the same file works on
 * both.
 *
 * Plain C with no libctru and no NitroSDK types, so the host test can build
 * it directly. The button bits are the DS PAD bits the port's
 * NitroPort_SetButtons takes.
 */

#ifndef HGSS_3DS_INPUT_H
#define HGSS_3DS_INPUT_H

#include <stdint.h>

#define HGSS_SCRIPT_MAX_ENTRIES 4096
#define HGSS_SCRIPT_HOLD_FRAMES 6

#define HGSS_BUTTON_A      0x0001
#define HGSS_BUTTON_B      0x0002
#define HGSS_BUTTON_SELECT 0x0004
#define HGSS_BUTTON_START  0x0008
#define HGSS_KEY_RIGHT     0x0010
#define HGSS_KEY_LEFT      0x0020
#define HGSS_KEY_UP        0x0040
#define HGSS_KEY_DOWN      0x0080
#define HGSS_BUTTON_R      0x0100
#define HGSS_BUTTON_L      0x0200
#define HGSS_BUTTON_X      0x0400
#define HGSS_BUTTON_Y      0x0800

typedef struct HgssScriptEntry {
    long frame;
    uint16_t buttons;
    int touch;      /* 1 for a touch entry */
    int x, y;       /* lower screen, 0-255 x 0-191 */
} HgssScriptEntry;

typedef struct HgssScript {
    long every;
    long end;
    int count;
    HgssScriptEntry entries[HGSS_SCRIPT_MAX_ENTRIES];
} HgssScript;

/* Parse `text` into `script`. Returns 0, or a negative value for a malformed
 * line (the script is then left with the entries before the bad line). */
int HgssScript_Parse(const char *text, HgssScript *script);

/* The buttons held at `frame` (a line's six-frame hold), 0 if none. */
uint16_t HgssScript_HeldButtons(const HgssScript *script, long frame);

/* Whether a touch is down at `frame`, and where. Returns 1 if so. */
int HgssScript_TouchAt(const HgssScript *script, long frame, int *x, int *y);

#endif /* HGSS_3DS_INPUT_H */
