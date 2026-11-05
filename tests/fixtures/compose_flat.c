#include <stdio.h>
#include "port_compose_flat.h"

int main(void)
{
    int failures = 0;
    /* Every dependency is necessary; mutate each one independently. */
    if (!NitroPort_FlatShadowMatches(1, 1, 123, 123, 320, 192, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(0, 1, 123, 123, 320, 192, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 0, 123, 123, 320, 192, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 1, 122, 123, 320, 192, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 1, 123, 123, 256, 192, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 1, 123, 123, 320, 191, 320, 192, 1, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 1, 123, 123, 320, 192, 320, 192, 0, 1)) failures++;
    if (NitroPort_FlatShadowMatches(1, 1, 123, 123, 320, 192, 320, 192, 1, 0)) failures++;
    printf("flat publish guard: %d failed\n", failures);
    return failures != 0;
}
