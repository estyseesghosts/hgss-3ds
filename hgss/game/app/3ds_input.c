/* See 3ds_input.h. */

#include "3ds_input.h"

#include <ctype.h>
#include <string.h>

static int button_bit(const char *name, size_t length)
{
    static const struct {
        const char *name;
        uint16_t bit;
    } names[] = {
        { "A", HGSS_BUTTON_A }, { "B", HGSS_BUTTON_B },
        { "X", HGSS_BUTTON_X }, { "Y", HGSS_BUTTON_Y },
        { "L", HGSS_BUTTON_L }, { "R", HGSS_BUTTON_R },
        { "START", HGSS_BUTTON_START }, { "SELECT", HGSS_BUTTON_SELECT },
        { "UP", HGSS_KEY_UP }, { "DOWN", HGSS_KEY_DOWN },
        { "LEFT", HGSS_KEY_LEFT }, { "RIGHT", HGSS_KEY_RIGHT },
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strlen(names[i].name) == length && strncmp(name, names[i].name, length) == 0) {
            return names[i].bit;
        }
    }
    return 0;
}

/* "A+UP" -> bits, or 0 if any name is unknown. */
static int button_list(const char *text, size_t length)
{
    size_t offset = 0;
    int bits = 0;

    while (offset < length) {
        size_t start = offset;
        while (offset < length && text[offset] != '+') {
            offset++;
        }
        if (offset == start) {
            return 0;
        }
        {
            int bit = button_bit(text + start, offset - start);
            if (bit == 0) {
                return 0;
            }
            bits |= bit;
        }
        if (offset < length) {
            offset++;   /* '+' */
        }
    }
    return bits;
}

static const char *skip_space(const char *p)
{
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/* A signed decimal number at *p; advances *p past it. -1 if none. */
static int read_number(const char **p, long *value)
{
    const char *start = *p;
    long sign = 1;
    long result = 0;

    if (*start == '-') {
        sign = -1;
        start++;
    }
    if (*start < '0' || *start > '9') {
        return -1;
    }
    while (*start >= '0' && *start <= '9') {
        result = result * 10 + (*start - '0');
        start++;
    }
    *value = sign * result;
    *p = start;
    return 0;
}

int HgssScript_Parse(const char *text, HgssScript *script)
{
    const char *p = text;
    int line = 0;

    memset(script, 0, sizeof(*script));
    script->every = 1;
    while (*p != '\0') {
        const char *end;
        const char *cursor;
        long frame;

        line++;
        end = strchr(p, '\n');
        if (end == NULL) {
            end = p + strlen(p);
        }
        cursor = skip_space(p);
        if (cursor == end || *cursor == '#') {
            p = *end != '\0' ? end + 1 : end;
            continue;
        }
        if ((size_t)(end - cursor) > 6 && strncmp(cursor, "every:", 6) == 0) {
            const char *number = skip_space(cursor + 6);
            if (read_number(&number, &script->every) != 0 || script->every <= 0) {
                return -line;
            }
        } else if ((size_t)(end - cursor) > 4 && strncmp(cursor, "end:", 4) == 0) {
            const char *number = skip_space(cursor + 4);
            if (read_number(&number, &script->end) != 0 || script->end <= 0) {
                return -line;
            }
        } else {
            const char *input;
            HgssScriptEntry entry;

            if (read_number(&cursor, &frame) != 0 || frame < 0) {
                return -line;
            }
            cursor = skip_space(cursor);
            if (cursor == end) {
                return -line;
            }
            memset(&entry, 0, sizeof(entry));
            entry.frame = frame;
            if ((size_t)(end - cursor) >= 5 && strncmp(cursor, "touch", 5) == 0) {
                long x, y;

                input = skip_space(cursor + 5);
                if (read_number(&input, &x) != 0) {
                    return -line;
                }
                input = skip_space(input);
                if (read_number(&input, &y) != 0 || x < 0 || x > 255 || y < 0 || y > 191) {
                    return -line;
                }
                entry.touch = 1;
                entry.x = (int)x;
                entry.y = (int)y;
            } else if (*cursor == '-') {
                /* "FRAME -": no input; nothing to add. */
                p = *end != '\0' ? end + 1 : end;
                continue;
            } else {
                size_t length = (size_t)(end - cursor);
                while (length > 0 && isspace((unsigned char)cursor[length - 1])) {
                    length--;
                }
                {
                    int bits = button_list(cursor, length);
                    if (bits == 0) {
                        return -line;
                    }
                    entry.buttons = (uint16_t)bits;
                }
            }
            if (script->count >= HGSS_SCRIPT_MAX_ENTRIES) {
                return -line;
            }
            script->entries[script->count++] = entry;
        }
        p = *end != '\0' ? end + 1 : end;
    }
    if (script->end == 0) {
        /* A script without end: runs to the end of the frames the caller
         * asks for; the PC runner does the same with --frames. */
        script->end = -1;
    }
    return 0;
}

uint16_t HgssScript_HeldButtons(const HgssScript *script, long frame)
{
    uint16_t held = 0;
    int i;

    for (i = 0; i < script->count; i++) {
        if (!script->entries[i].touch
            && frame >= script->entries[i].frame
            && frame < script->entries[i].frame + HGSS_SCRIPT_HOLD_FRAMES) {
            held |= script->entries[i].buttons;
        }
    }
    return held;
}

int HgssScript_TouchAt(const HgssScript *script, long frame, int *x, int *y)
{
    int i;

    for (i = 0; i < script->count; i++) {
        if (script->entries[i].touch
            && frame >= script->entries[i].frame
            && frame < script->entries[i].frame + HGSS_SCRIPT_HOLD_FRAMES) {
            if (x != NULL) {
                *x = script->entries[i].x;
            }
            if (y != NULL) {
                *y = script->entries[i].y;
            }
            return 1;
        }
    }
    return 0;
}
