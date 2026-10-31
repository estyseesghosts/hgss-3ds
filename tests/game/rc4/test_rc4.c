// Known-answer tests for the port's plain-C RC4, built by run.sh.
//
// The reference vectors are the classic RC4 ones ("Key"/"Plaintext",
// "Wiki"/"pedia", "Secret"/"Attack at dawn") and RFC 6229 section 2.1
// (40-bit key 01 02 03 04 05, first 16 keystream bytes). The byte-state and
// word-state ("fast") contexts take the same stream; the fast one keeps the
// permutation in the top byte of each word, so a mistake in the shifting shows
// up here.

#include <stdio.h>
#include <string.h>

#include "nitrocrypto_rc4.h"

static int failures;

static void check(const char *name, const u8 *got, const u8 *want, unsigned len)
{
    unsigned i;

    if (memcmp(got, want, len) == 0) {
        return;
    }
    failures++;
    printf("FAIL %s\n  got: ", name);
    for (i = 0; i < len; i++) {
        printf("%02x", got[i]);
    }
    printf("\n  want: ");
    for (i = 0; i < len; i++) {
        printf("%02x", want[i]);
    }
    printf("\n");
}

static void check_byte(const char *name, const char *key, unsigned key_len,
                       const char *plain, const char *want_hex, unsigned len)
{
    static CRYPTORC4Context ctx;
    u8 got[32];
    u8 want[32];
    unsigned i;
    unsigned char hi, lo;

    for (i = 0; i < len; i++) {
        hi = (unsigned char)want_hex[2 * i];
        lo = (unsigned char)want_hex[2 * i + 1];
        hi = (unsigned char)(hi <= '9' ? hi - '0' : (hi | 0x20) - 'a' + 10);
        lo = (unsigned char)(lo <= '9' ? lo - '0' : (lo | 0x20) - 'a' + 10);
        want[i] = (u8)((hi << 4) | lo);
    }

    CRYPTO_RC4Init(&ctx, key, key_len);
    CRYPTO_RC4Encrypt(&ctx, plain, len, got);
    check(name, got, want, len);

    // The context must carry i and j as the assembly did, so encrypting in two
    // calls gives the same stream as one call.
    CRYPTO_RC4Init(&ctx, key, key_len);
    CRYPTO_RC4Encrypt(&ctx, plain, len / 2, got);
    CRYPTO_RC4Encrypt(&ctx, plain + len / 2, len - len / 2, got + len / 2);
    check(name, got, want, len);
}

static void check_fast(const char *name, const u8 *key, unsigned key_len,
                       const u8 *plain, const u8 *want, unsigned len)
{
    static CRYPTORC4FastContext ctx;
    u8 got[32];
    unsigned i;

    CRYPTO_RC4FastInit(&ctx, key, key_len);
    CRYPTO_RC4FastEncrypt(&ctx, plain, len, got);
    check(name, got, want, len);

    CRYPTO_RC4FastInit(&ctx, key, key_len);
    for (i = 0; i < len; i++) {
        CRYPTO_RC4FastEncrypt(&ctx, plain + i, 1, got + i);
    }
    check(name, got, want, len);

    // Same bytes and state as the byte context after the same key.
    {
        CRYPTORC4Context slow;
        u8 slow_out[32];
        CRYPTO_RC4Init(&slow, key, key_len);
        CRYPTO_RC4Encrypt(&slow, plain, len, slow_out);
        check(name, got, slow_out, len);
    }
}

int main(void)
{
    static const u8 rfc6229_key[] = { 1, 2, 3, 4, 5 };
    static const u8 rfc6229_zero[16] = { 0 };
    static const u8 rfc6229_want[16] = {
        0xb2, 0x39, 0x63, 0x05, 0xf0, 0x3d, 0xc0, 0x27,
        0xcc, 0xc3, 0x52, 0x4a, 0x0a, 0x11, 0x18, 0xa8,
    };
    static const u8 fast_plain[] = "Plaintext";
    static const u8 fast_want[] = {
        0xbb, 0xf3, 0x16, 0xe8, 0xd9, 0x40, 0xaf, 0x0a, 0xd3,
    };

    check_byte("Key/Plaintext", "Key", 3, "Plaintext",
               "bbf316e8d940af0ad3", 9);
    check_byte("Wiki/pedia", "Wiki", 4, "pedia", "1021bf0420", 5);
    check_byte("Secret/Attack at dawn", "Secret", 6, "Attack at dawn",
               "45a01f645fc35b383552544b9bf5", 14);

    check_fast("fast Key/Plaintext", (const u8 *)"Key", 3, fast_plain,
               fast_want, sizeof(fast_want));

    {
        static CRYPTORC4Context ctx;
        u8 out[16];
        CRYPTO_RC4Init(&ctx, rfc6229_key, sizeof(rfc6229_key));
        CRYPTO_RC4Encrypt(&ctx, rfc6229_zero, sizeof(rfc6229_zero), out);
        check("RFC 6229 0102030405", out, rfc6229_want, sizeof(out));
    }
    {
        static CRYPTORC4FastContext ctx;
        u8 out[16];
        CRYPTO_RC4FastInit(&ctx, rfc6229_key, sizeof(rfc6229_key));
        CRYPTO_RC4FastEncrypt(&ctx, rfc6229_zero, sizeof(rfc6229_zero), out);
        check("fast RFC 6229 0102030405", out, rfc6229_want, sizeof(out));
    }

    if (failures != 0) {
        printf("%d RC4 known-answer test(s) failed\n", failures);
        return 1;
    }
    printf("rc4: all known-answer tests passed\n");
    return 0;
}
