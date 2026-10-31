/* Host test: hgss/compat/include/nitro/dgt/dgt.h forwards to the flat dgt.h.
 * The compat header must give the port's bare DGTHash1Context/DGTHash2Context
 * names (the flat header only declares the struct tags) over the same
 * layout 3ds_port/nitro/src/dgt.c is written against, with the six DGT_Hash*
 * functions declared exactly as dgt.c defines them. A bare reference to each
 * declaration checks the signature; the objects are defined by dgt.c, so this
 * test only compiles (it never links).
 * Compile with -Ihgss/compat/include first on the path (see run.sh). */
#include <nitro/dgt/dgt.h>

/* Compile-time: the bare names are the flat structs. */
typedef char DGTHash1IsFlatStruct[(sizeof(DGTHash1Context) == sizeof(struct DGTHash1Context)) ? 1 : -1];
typedef char DGTHash2IsFlatStruct[(sizeof(DGTHash2Context) == sizeof(struct DGTHash2Context)) ? 1 : -1];

typedef void (*Hash1SetSourceFn)(DGTHash1Context *, const void *, unsigned long);
typedef void (*Hash1DigestFn)(void *, DGTHash1Context *);
typedef void (*Hash2SetSourceFn)(DGTHash2Context *, const unsigned char *, unsigned long);
typedef void (*Hash2DigestFn)(DGTHash2Context *, unsigned char *);

static Hash1SetSourceFn sSet1 = DGT_Hash1SetSource;
static Hash1DigestFn sDigest1 = DGT_Hash1GetDigest_R;
static Hash2SetSourceFn sSet2 = DGT_Hash2SetSource;
static Hash2DigestFn sDigest2 = DGT_Hash2GetDigest;

int check_sizes(void);

int check_sizes(void)
{
    DGTHash1Context h1;
    DGTHash2Context h2;
    struct DGTHash1Context *as_tag1 = (struct DGTHash1Context *)&h1;
    struct DGTHash2Context *as_tag2 = (struct DGTHash2Context *)&h2;

    /* The typedefs name the flat structs, and both field sets are live. */
    if (sizeof h1 != sizeof(struct DGTHash1Context)) {
        return 1;
    }
    if (sizeof h2 != sizeof(struct DGTHash2Context)) {
        return 2;
    }
    h2.h0 = 7;
    if (h2.Intermediate_Hash[0] != 7) {
        return 3;
    }
    h1.a = 9;
    if (h1.state[0] != 9) {
        return 4;
    }
    (void)as_tag1;
    (void)as_tag2;
    return 0;
}

int main(void)
{
    (void)sSet1;
    (void)sDigest1;
    (void)sSet2;
    (void)sDigest2;
    return check_sizes();
}
