/* Shared helpers for the SDK_PORT_3DS hook tests (tests/sdk/port_hooks). Synthetic only: no game data.
 *
 * The SDK addresses its hardware registers directly (REG_IF_ADDR, REG_DMA0SAD_ADDR, the G3X registers
 * at 0x04000000 and up), so the tests back that range with a page of ordinary memory at the same
 * address. Linux host, -m32. */
#ifndef PORT_HOOKS_HW_RAM_H
#define PORT_HOOKS_HW_RAM_H

#include <nitro.h>

/* lib/include shadows the system headers, so declare what is used here (as tests/sdk/fx does). */
int printf(const char *, ...);
void exit(int);
void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);

#define HW_TEST_PROT_RW   3                       /* PROT_READ | PROT_WRITE */
#define HW_TEST_MAP_FLAGS (0x02 | 0x20 | 0x100000) /* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE */
#define HW_TEST_IO_BASE   0x04000000u
#define HW_TEST_IO_SIZE   0x10000u

/* Returns 1 when the page is mapped at the DS I/O address. */
static int hw_test_map_io(void) {
    void *p = mmap((void *)HW_TEST_IO_BASE, HW_TEST_IO_SIZE, HW_TEST_PROT_RW, HW_TEST_MAP_FLAGS, -1, 0);
    return p == (void *)HW_TEST_IO_BASE;
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
