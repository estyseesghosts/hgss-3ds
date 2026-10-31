/*
 * Boot-time data-pack check, against the port's real code path:
 *
 *   Heartgold_OpenPak (hgss/game/app/heartgold.c)
 *     -> NitroPort_OpenPak (3ds_port/nitro/src/port_fs.c)
 *     -> pak_open (3ds_port/pak/src/pak.c)
 *
 * No game data: the packs are synthetic, written by make_packs.py with
 * builder.pack.write_pak. Each case checks the result code and the words
 * the program shows the player.
 */
#include <nitro.h>
#include <nitro_port.h>
#include <nitro_port_fs.h>
#include "pak.h"
#include "heartgold.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int sChecks, sFailures;

#define CHECK(test) do { \
    ++sChecks; \
    if (!(test)) { \
        ++sFailures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #test); \
    } \
} while (0)

/* The program's hooks (heartgold.c only needs NitroPort_Log; port_fs.c also
 * asks for the clock and the DS-time model). */
void NitroPort_Log(const char *fmt, ...)
{
    va_list args;

    fputs("[log] ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

void NitroPort_SpendDsTime(u64 cycles)
{
    (void)cycles;
}

uint64_t NitroPortPlatform_Nanoseconds(void)
{
    return 0;
}

static int file(char *buffer, size_t size, const char *dir, const char *name)
{
    return snprintf(buffer, size, "%s/%s", dir, name) < (int)size;
}

static const char *open_error(const char *dir, const char *pak, const char *manifest)
{
    static char pak_path[512];
    static char manifest_path[512];
    if (!file(pak_path, sizeof(pak_path), dir, pak) ||
        !file(manifest_path, sizeof(manifest_path), dir, manifest)) {
        return "(path too long)";
    }
    return Heartgold_OpenPak(pak_path, manifest_path);
}

int main(int argc, char **argv)
{
    const char *dir;
    const char *error;

    if (argc != 2) {
        fprintf(stderr, "usage: test_pak_boot <synthetic fixture folder>\n");
        return 2;
    }
    dir = argv[1];

    /* The build ID and the pack agree: the program starts. */
    error = open_error(dir, "ok.pak", "manifest.json");
    CHECK(error == NULL);
    CHECK(NitroPort_IsPakOpen());
    NitroPort_ClosePak();
    CHECK(!NitroPort_IsPakOpen());

    /* A pack for another build: the player is told how to re-pack it. */
    error = open_error(dir, "wrong-build.pak", "manifest.json");
    CHECK(error != NULL);
    CHECK(error != NULL && strstr(error, "wrong build ID") != NULL);
    CHECK(error != NULL && strstr(error, "--build-id heartgold-nitrofs-1") != NULL);
    CHECK(!NitroPort_IsPakOpen());

    /* A newer on-disk version. */
    error = open_error(dir, "wrong-version.pak", "manifest.json");
    CHECK(error != NULL && strstr(error, "different version of the builder") != NULL);

    /* Header present, index cut: damaged, not a crash. */
    error = open_error(dir, "truncated.pak", "manifest.json");
    CHECK(error != NULL && strstr(error, "damaged") != NULL);

    error = open_error(dir, "corrupt.pak", "manifest.json");
    CHECK(error != NULL && strstr(error, "damaged") != NULL);

    /* A pack from a ROM the pinned reader does not know (HeartGold's own
     * digest): the ROM-SHA-1 message, not "damaged" or a crash. */
    error = open_error(dir, "unsupported-rom.pak", "manifest.json");
    CHECK(error != NULL && strstr(error, "ROM this version doesn't support") != NULL);

    /* Missing pack or manifest: the I/O message. */
    error = open_error(dir, "absent.pak", "manifest.json");
    CHECK(error != NULL && strstr(error, "can't read") != NULL);

    error = open_error(dir, "ok.pak", "absent.json");
    CHECK(error != NULL && strstr(error, "can't read") != NULL);

    /* A manifest whose sizes do not match the pack: format error. */
    error = open_error(dir, "ok.pak", "manifest-bad-size.json");
    CHECK(error != NULL && strstr(error, "damaged") != NULL);

    /* The same pack through libpak directly: the builder's bytes are valid
     * for the reader the program links. */
    {
        Pak *pak = NULL;
        uint32_t size = 0;
        char bytes[10 + 1];
        char pak_path[512];

        CHECK(file(pak_path, sizeof(pak_path), dir, "ok.pak"));
        CHECK(pak_open(pak_path, "heartgold-nitrofs-1", &pak) == PAK_OK);
        CHECK(pak != NULL);
        CHECK(pak_find(pak, 0, &size) == PAK_OK && size == 10);
        CHECK(pak_read(pak, 0, 0, bytes, 10) == PAK_OK);
        bytes[10] = '\0';
        CHECK(memcmp(bytes, "hello.bin!", 10) == 0);
        CHECK(pak_find(pak, 99, &size) == PAK_ERR_NOT_FOUND);
        pak_close(pak);
    }

    printf("pak boot check: %d checks, %d failed\n", sChecks, sFailures);
    printf("(the OK pack carries a ROM SHA-1 the pinned reader knows; a real\n"
           " HeartGold pack currently stops at the same ROM-SHA-1 check —\n"
           " see docs/decisions.md and the open item there)\n");
    return sFailures ? 1 : 0;
}
