#include <stdio.h>
#include <nitro_port_upscale.h>
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
    /* All possible alpha bytes are immaterial for a transparent over pixel;
     * use the production premultiplier, not an approximation of its rule. */
    for (int a = 0; a < 256; a++) {
        uint16_t over = 0;
        uint8_t alpha = (uint8_t)a, alpha16[17];
        uint32_t out = 0xffffffffu;
        for (int i = 0; i < 17; i++) alpha16[i] = (uint8_t)(i * 255 / 16);
        if (NitroPort_PremultiplyOver(&over, &alpha, alpha16, &out, 0, 1) != 0 || out != 0) failures++;
    }
    printf("flat publish guard: %d failed\n", failures);
    return failures != 0;
}
