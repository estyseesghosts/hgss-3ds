#ifndef PORT_QUAD_UV_H
#define PORT_QUAD_UV_H

/* Front BG metadata counts map rows from the top, while PICA samples row
 * zero at v=1. OBJ atlas UVs already use PICA coordinates; leave the
 * established behind-3D field path unchanged until separately verified. */
static inline float NitroPort_QuadV(int kind, int behind, float v)
{
    return kind == 0 && !behind ? 1.0f - v : v;
}

#endif
