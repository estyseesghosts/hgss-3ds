/* Shared helpers for the SDK_PORT_3DS hook tests (tests/sdk/port_hooks). Synthetic only: no game data.
 *
 * Under SDK_PORT_3DS the SDK's register macros point into the port's arrays (patch 1301), which
 * run.sh links from tests/sdk/port_memory.c. Nothing has to be mapped at 0x04000000 any more;
 * hw_test_map_io() stays as the call the three tests make and checks the arrays are present.
 */
#ifndef PORT_HOOKS_HW_RAM_H
#define PORT_HOOKS_HW_RAM_H

#include <nitro.h>

/* lib/include shadows the system headers, so declare what is used here (as tests/sdk/fx does). */
int printf(const char *, ...);
void exit(int);

/* Returns 1: the arrays linked from tests/sdk/port_memory.c back HW_IOREG and the registers,
 * so there is no page to map (patch 1301). */
static int hw_test_map_io(void) {
    return 1;
}

static int g_checks, g_fails;

#define CHECK(c)                                                                 \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(c)) {                                                              \
            g_fails++;                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                  \
        }                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        unsigned long a_ = (unsigned long)(actual);                              \
        unsigned long e_ = (unsigned long)(expected);                            \
        g_checks++;                                                              \
        if (a_ != e_) {                                                          \
            g_fails++;                                                           \
            printf("FAIL %s:%d: %s = 0x%lx, expected 0x%lx\n", __FILE__, __LINE__, #actual, a_, e_); \
        }                                                                        \
    } while (0)

/* Prints the summary and returns the process exit status. */
static int hw_test_finish(const char *name) {
    printf("%s: %d checks, %d failures\n", name, g_checks, g_fails);
    return g_fails ? 1 : 0;
}

#endif
