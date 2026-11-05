#include <math.h>
#include <stdio.h>
#include "port_quad_uv.h"

int main(void)
{
    int checks = 0, failed = 0;
    const int offsets[] = { 0, 1, 127, 255, 511 };
    for (int scale = 1; scale <= 2; scale++) {
        for (unsigned j = 0; j < sizeof(offsets) / sizeof(offsets[0]); j++) {
            for (int y = 0; y < 192; y++) {
                /* Pixel centres, as sampled from a nearest-filtered 512-row
                 * PICA texture with repeat. 2x pictures duplicate each row. */
                float v = (offsets[j] + y + 0.5f) * scale / 512.0f;
                float picaV = NitroPort_QuadV(0, 0, v);
                int row = (int)floorf((1.0f - picaV) * 512.0f) & 511;
                int expected = ((offsets[j] + y) * scale) & 511;
                checks++;
                if (row / scale != expected / scale) failed++;
                /* OBJ UVs and behind-3D BGs must not change. */
                checks += 2;
                if (NitroPort_QuadV(1, 0, v) != v) failed++;
                if (NitroPort_QuadV(0, 1, v) != v) failed++;
            }
        }
    }
    /* Independent affine corners, including a negative coordinate. */
    checks += 3;
    if (NitroPort_QuadV(0, 0, -0.25f) != 1.25f) failed++;
    if (NitroPort_QuadV(0, 0, 0.375f) != 0.625f) failed++;
    if (NitroPort_QuadV(0, 0, 1.25f) != -0.25f) failed++;
    printf("quad-uv: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
