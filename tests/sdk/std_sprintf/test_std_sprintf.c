/*
 * Host regression for the SDK_PORT_3DS argument reads in std_sprintf.c (patch 1701).
 *
 * The formatter's MWCC version advances a byte-pointer va_list; the patched
 * version uses va_arg and must produce the same DS output for every read path:
 * %d/%i (32-bit signed), %u/%o/%x/%X (32-bit unsigned), h/hh (the promoted int
 * truncated to the format's width), ll (64-bit), %c, %s, %p and the %n writes.
 * Values are synthetic. No <stdio.h>: the project include path shadows
 * <stdarg.h> with the MSL header, and this driver only needs the system one.
 */
#include <stdarg.h>
#include <string.h>

/* Match the NitroSDK spelling: u32 is unsigned long on every 32-bit target. */
typedef unsigned long u32;

int STD_TVSNPrintf(char *dest, u32 len, const char *format, va_list va);
int printf(const char *, ...);

static int checks;
static int fails;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int fmt(char *buf, u32 cap, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = STD_TVSNPrintf(buf, cap, format, ap);
    va_end(ap);
    return n;
}

static void check_str(const char *what, int line, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        fails++;
        printf("FAIL line %d: %s: got \"%s\" want \"%s\"\n", line, what, got, want);
    }
}

#define CHECK_STR(got, want) check_str(#got, __LINE__, (got), (want))

int main(void)
{
    char buf[128];
    int n;
    int written;
    short hwritten;
    long long ll;

    /* 32-bit signed and unsigned */
    n = fmt(buf, sizeof(buf), "%d", -42);
    CHECK_STR(buf, "-42");
    CHECK(n == 3, "%%d return %d", n);

    fmt(buf, sizeof(buf), "%d", 0);
    CHECK_STR(buf, "0");

    fmt(buf, sizeof(buf), "%u", (unsigned)42);
    CHECK_STR(buf, "42");

    fmt(buf, sizeof(buf), "%o", (unsigned)042);
    CHECK_STR(buf, "42");

    fmt(buf, sizeof(buf), "%x", (unsigned)0x1f);
    CHECK_STR(buf, "1f");

    fmt(buf, sizeof(buf), "%X", (unsigned)0xbeef);
    CHECK_STR(buf, "BEEF");

    fmt(buf, sizeof(buf), "%08x", (unsigned)0x1f);
    CHECK_STR(buf, "0000001f");

    /* char, short and signed/unsigned h/hh truncation */
    fmt(buf, sizeof(buf), "%c", 'Z');
    CHECK_STR(buf, "Z");

    fmt(buf, sizeof(buf), "%hd", -2);
    CHECK_STR(buf, "-2");

    fmt(buf, sizeof(buf), "%hhd", -1);
    CHECK_STR(buf, "-1");

    fmt(buf, sizeof(buf), "%hu", (unsigned)0x12345);
    CHECK_STR(buf, "9029");

    fmt(buf, sizeof(buf), "%hhu", (unsigned)0x1ff);
    CHECK_STR(buf, "255");

    /* strings, with and without precision */
    fmt(buf, sizeof(buf), "%s", "abc");
    CHECK_STR(buf, "abc");

    fmt(buf, sizeof(buf), "%.2s", "abc");
    CHECK_STR(buf, "ab");

    /* width and precision from the arguments */
    fmt(buf, sizeof(buf), "%*d", 5, 42);
    CHECK_STR(buf, "   42");

    fmt(buf, sizeof(buf), "%.*d", 3, 42);
    CHECK_STR(buf, "042");

    /* 64-bit values: 8-byte slots on ARM and under -m32 -malign-double */
    ll = 1234567890123LL;
    fmt(buf, sizeof(buf), "%lld", ll);
    CHECK_STR(buf, "1234567890123");

    ll = -1234567890123LL;
    fmt(buf, sizeof(buf), "%lld", ll);
    CHECK_STR(buf, "-1234567890123");

    fmt(buf, sizeof(buf), "%llu", 18446744073709551615ULL);
    CHECK_STR(buf, "18446744073709551615");

    fmt(buf, sizeof(buf), "%llx", 0x123456789abcdefULL);
    CHECK_STR(buf, "123456789abcdef");

    /* pointers go through the port's pointer read; the DS formatter zero-pads to 8 */
    fmt(buf, sizeof(buf), "%p", (void *)0x1234);
    CHECK_STR(buf, "0x00001234");

    /* %n writes the count through h, default and ll pointer types */
    written = -1;
    n = fmt(buf, sizeof(buf), "abc%n", &written);
    CHECK_STR(buf, "abc");
    CHECK(n == 3, "%%n return %d", n);
    CHECK(written == 3, "%%n wrote %d", written);

    hwritten = -1;
    fmt(buf, sizeof(buf), "xy%hn", &hwritten);
    CHECK_STR(buf, "xy");
    CHECK(hwritten == 2, "%%hn wrote %d", (int)hwritten);

    ll = -1;
    fmt(buf, sizeof(buf), "wxyz%lln", (long long *)&ll);
    CHECK_STR(buf, "wxyz");
    CHECK(ll == 4, "%%lln wrote %lld", ll);

    /* truncation still returns the full length and terminates the buffer */
    memset(buf, '?', sizeof(buf));
    n = fmt(buf, 4, "abcdef");
    CHECK(n == 6, "truncated return %d", n);
    CHECK(strcmp(buf, "abc") == 0, "truncated buffer \"%s\"", buf);

    if (fails) {
        printf("sdk std_sprintf: %d/%d checks FAILED\n", fails, checks);
        return 1;
    }
    printf("sdk std_sprintf: %d checks, 0 failed\n", checks);
    return 0;
}
