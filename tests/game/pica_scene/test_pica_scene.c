/*
 * Host test for hgss/game/app/pica_scene.c.
 *
 * The workaround drops the scene triangles whose vertices sit on the camera
 * plane (w == 0). A w = 0 vertex survives the port's ±x/y/z clip, then the
 * PICA's divide by w maps it to the centre of the render target: the black
 * fan the title screen showed.
 */
#include <nitro_port_gx3d.h>
#include <stdio.h>
#include <string.h>

unsigned Hgss_PicaDropBehindCamera(NitroGx3dScene *scene);

static NitroGx3dScene scene;
static int failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void make_triangle(unsigned i, int32_t w0, int32_t w1, int32_t w2)
{
    NitroGx3dTriangle *tri = &scene.triangles[i];
    unsigned k;

    memset(tri, 0, sizeof(*tri));
    for (k = 0; k < 3; k++) {
        tri->v[k].clip[0] = (int32_t)(100 + k);
        tri->v[k].clip[1] = (int32_t)(200 + k);
        tri->v[k].clip[2] = (int32_t)(300 + k);
        tri->v[k].x = (int32_t)(1000 * (i + 1) + k);
        tri->v[k].y = (int32_t)(2000 * (i + 1) + k);
    }
    tri->v[0].clip[3] = w0;
    tri->v[1].clip[3] = w1;
    tri->v[2].clip[3] = w2;
}

static int untouched(unsigned i, unsigned k)
{
    const NitroGx3dTriangle *tri = &scene.triangles[i];

    return tri->v[k].clip[0] == (int32_t)(100 + k) &&
           tri->v[k].clip[1] == (int32_t)(200 + k) &&
           tri->v[k].clip[2] == (int32_t)(300 + k) &&
           tri->v[k].x == (int32_t)(1000 * (i + 1) + k) &&
           tri->v[k].y == (int32_t)(2000 * (i + 1) + k);
}

int main(void)
{
    unsigned dropped;

    check(Hgss_PicaDropBehindCamera(NULL) == 0, "a NULL scene is a no-op");

    memset(&scene, 0, sizeof(scene));
    scene.count = 5;
    make_triangle(0, 4096, 4096, 0);    /* one vertex on the camera plane */
    make_triangle(1, 4096, -1, 4096);   /* one behind it */
    make_triangle(2, 4096, 4096, 4096); /* untouched */
    make_triangle(3, 1, 4096, 4096);    /* tiny but positive: kept */
    make_triangle(4, 0, 0, 0);          /* no vertex the GPU can divide by */

    dropped = Hgss_PicaDropBehindCamera(&scene);

    check(dropped == 3, "three triangles with a w <= 0 vertex are dropped");
    check(memcmp(&scene.triangles[0].v[0], &scene.triangles[0].v[1],
                 sizeof(scene.triangles[0].v[0])) == 0,
          "a dropped triangle is degenerate (v0 == v1)");
    check(memcmp(&scene.triangles[1].v[0], &scene.triangles[1].v[1],
                 sizeof(scene.triangles[1].v[0])) == 0,
          "a behind-camera triangle is degenerate too");
    check(scene.triangles[4].v[0].clip[3] == 4096 &&
          scene.triangles[4].v[1].clip[3] == 4096 &&
          scene.triangles[4].v[2].clip[3] == 4096,
          "a triangle with no vertex in front becomes a finite point");
    check(untouched(2, 0) && untouched(2, 1) && untouched(2, 2),
          "a normal triangle is not changed");
    check(untouched(3, 0) && untouched(3, 1) && untouched(3, 2),
          "a tiny positive w is not treated as the camera plane");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("pica_scene: all checks passed\n");
    return 0;
}
