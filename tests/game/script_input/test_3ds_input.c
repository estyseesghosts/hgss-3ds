/* Host test for hgss/game/app/3ds_input.c (the 3DS scripted-input parser). */
#include <stdio.h>
#include <string.h>

#include "3ds_input.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_parse(void)
{
    static const char text[] =
        "# a comment\n"
        "every: 5\n"
        "end: 20000\n"
        "0 -\n"
        "6550 START\n"
        "7000 UP+A\n"
        "8050 touch 128 176\n";
    HgssScript script;

    CHECK(HgssScript_Parse(text, &script) == 0);
    CHECK(script.every == 5);
    CHECK(script.end == 20000);
    CHECK(script.count == 3);
    CHECK(script.entries[0].frame == 6550);
    CHECK(script.entries[0].buttons == HGSS_BUTTON_START);
    CHECK(script.entries[1].buttons == (HGSS_KEY_UP | HGSS_BUTTON_A));
    CHECK(script.entries[2].touch == 1);
    CHECK(script.entries[2].x == 128 && script.entries[2].y == 176);
}

static void test_holds(void)
{
    static const char text[] = "every: 1\nend: 10\n8 A\n2 touch 10 20\n";
    HgssScript script;
    int x = -1, y = -1;

    CHECK(HgssScript_Parse(text, &script) == 0);
    CHECK(HgssScript_HeldButtons(&script, 7) == 0);
    CHECK(HgssScript_HeldButtons(&script, 8) == HGSS_BUTTON_A);
    CHECK(HgssScript_HeldButtons(&script, 13) == HGSS_BUTTON_A);
    CHECK(HgssScript_HeldButtons(&script, 14) == 0);
    CHECK(HgssScript_TouchAt(&script, 1, &x, &y) == 0);
    CHECK(HgssScript_TouchAt(&script, 3, &x, &y) == 1);
    CHECK(x == 10 && y == 20);
    CHECK(HgssScript_TouchAt(&script, 8, &x, &y) == 0);
}

static void test_errors(void)
{
    HgssScript script;

    CHECK(HgssScript_Parse("every: 1\nend: 10\n5 NOPE\n", &script) < 0);
    CHECK(HgssScript_Parse("every: 1\nend: 10\n5 touch 300 0\n", &script) < 0);
    CHECK(HgssScript_Parse("every: 0\n", &script) < 0);
    CHECK(HgssScript_Parse("end: 10\n7 A 8 B\n", &script) < 0);
}

static void test_empty(void)
{
    HgssScript script;

    CHECK(HgssScript_Parse("", &script) == 0);
    CHECK(script.count == 0);
    CHECK(HgssScript_HeldButtons(&script, 0) == 0);
}

int main(void)
{
    test_parse();
    test_holds();
    test_errors();
    test_empty();
    if (failures != 0) {
        fprintf(stderr, "3ds_input: %d failure(s)\n", failures);
        return 1;
    }
    printf("3ds_input: all ok\n");
    return 0;
}
