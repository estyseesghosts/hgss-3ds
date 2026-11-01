/*
 * The scene's triangles that sit exactly on the camera plane.
 *
 * The port clips a polygon to the DS's six planes ±x/y/z <= w. A vertex whose
 * w is exactly 0 and whose x, y and z are 0 satisfies all six (`w + v < 0` is
 * false for 0), so it survives the clip. The PC rasteriser can survive that by
 * drawing the resulting triangle from screen (0,0); the PICA cannot: the GPU
 * vertex data is (ndc * w, w), so a w = 0 vertex becomes (0, 0, z, 0), and the
 * hardware's divide by w puts it at the centre of the render target. Every
 * triangle touching it is then drawn as a fan from the centre -- the black
 * shards across the title screen's Ho-oh.
 *
 * HGSS's title/intro scenes produce such vertices (polygons crossing the
 * camera plane). Until the clipper gets a w > 0 near plane (an upstream port
 * change: 3ds_port/ is byte-identical to platinum.lock), this drops the
 * affected triangles from the acquired scene before the GPU draws it. A
 * triangle crossing the eye plane is a sliver at most; the rest of the scene
 * is unchanged.
 *
 * The GPU thread owns the acquired scene until it releases it, so changing
 * its triangles there is safe. The count is logged so a hardware run can say
 * how often it happens.
 */
#include <nitro_port_gx3d.h>
#include <string.h>

unsigned Hgss_PicaDropBehindCamera(NitroGx3dScene *scene)
{
    unsigned i, dropped = 0;

    if (scene == NULL) {
        return 0;
    }
    for (i = 0; i < scene->count; i++) {
        NitroGx3dTriangle *tri = &scene->triangles[i];
        unsigned g;

        if (tri->v[0].clip[3] > 0 && tri->v[1].clip[3] > 0 &&
            tri->v[2].clip[3] > 0) {
            continue;
        }
        /* Collapse it onto a vertex the GPU can divide by. If none is left,
         * a point at w = 1 draws nothing (all three vertices the same). */
        for (g = 0; g < 3; g++) {
            if (tri->v[g].clip[3] > 0) {
                break;
            }
        }
        if (g == 3) {
            memset(&tri->v[0], 0, sizeof(tri->v[0]));
            tri->v[0].clip[3] = 4096;
            tri->v[1] = tri->v[0];
            tri->v[2] = tri->v[0];
        } else {
            tri->v[0] = tri->v[g];
            tri->v[1] = tri->v[g];
            tri->v[2] = tri->v[g];
        }
        dropped++;
    }
    return dropped;
}
