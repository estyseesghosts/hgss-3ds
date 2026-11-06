#include <stdio.h>
#include <string.h>
#include <nitro_port_gx3d_vertex.h>

int main(void)
{
    NitroGx3dGpuVertex v[9] = {{{0}}};
    NitroGx3dGpuVertex tmp[9] = {{{0}}};
    NitroGx3dBatch b[2] = {{0, 6, 0, 0, 0, 0}, {6, 3, 0, 0, 0, 0}};
    uint16_t indices[9] = {0};
    size_t n;
    {
        NitroGx3dGpuVertex unique[9] = {{{0}}};
        for (int i = 0; i < 9; i++) unique[i].position[0] = (float)i;
        if (NitroPort_Gx3dHasLocalReuse(unique, 9, b, 2)) return 1;
    }
    if (!NitroPort_Gx3dIndexedEligible(0, 0, 1, 1) ||
        !NitroPort_Gx3dIndexedEligible(0, 1u << 4, 1, 1) ||
        !NitroPort_Gx3dIndexedEligible(0, 1u << 5, 0, 1) ||
        NitroPort_Gx3dIndexedEligible(1, 0, 0, 1) ||
        NitroPort_Gx3dIndexedEligible(0, 1u << 5, 1, 1) ||
        NitroPort_Gx3dIndexedEligible(0, 0, 0, 0)) {
        fprintf(stderr, "indexed scene eligibility mismatch\n");
        return 1;
    }
    v[0].position[0] = 1.0f;
    v[1].position[0] = 2.0f;
    v[2].position[0] = 3.0f;
    v[3] = v[1];
    v[4] = v[2];
    v[5] = v[0];
    v[6] = v[0];
    v[7] = v[1];
    v[8] = v[2];
    if (!NitroPort_Gx3dHasLocalReuse(v, 9, b, 2)) return 1;
    n = NitroPort_Gx3dCompactBatches(v, 9, b, 2, indices, 9, tmp);
    if (n != 6 || b[0].first != 0 || b[0].count != 6 ||
        b[1].first != 6 || b[1].count != 3 ||
        memcmp(indices, (uint16_t[]){0, 1, 2, 1, 2, 0, 3, 4, 5}, sizeof(indices)) != 0) {
        fprintf(stderr, "indexed compaction mismatch\n");
        return 1;
    }
    /* Failure protocol: an out-of-range batch packs nothing and mutates
     * nothing, so the caller can keep the plain arrays path. */
    {
        NitroGx3dGpuVertex w[3] = {{{0}}};
        NitroGx3dBatch bad[1] = {{2, 2, 0, 0, 0, 0}};
        uint16_t idx[3] = {9, 9, 9};
        w[0].position[0] = 1.0f;
        n = NitroPort_Gx3dCompactBatches(w, 3, bad, 1, idx, 3, tmp);
        if (n != 0 || bad[0].first != 2 || bad[0].count != 2 ||
            idx[0] != 9 || w[0].position[0] != 1.0f) {
            fprintf(stderr, "indexed failure protocol mismatch\n");
            return 1;
        }
    }
    return 0;
}
