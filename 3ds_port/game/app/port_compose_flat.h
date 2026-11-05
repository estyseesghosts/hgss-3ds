#ifndef PORT_COMPOSE_FLAT_H
#define PORT_COMPOSE_FLAT_H

/* A certificate describes actual CPU planes, not source VRAM generations.
 * The renderer freshly proves under=RGB555 backdrop and over=0. Alpha is
 * immaterial for zero over pixels (PremultiplyOver checks bit 15 first).
 * A certificate is valid only for the shadow actually written by the 1x
 * band publisher, and only while the caller has not modified the planes. */
static inline int NitroPort_FlatShadowMatches(int candidate, int shadow,
                                               int candidateColor, int shadowColor,
                                               int width, int height, int shadowWidth, int shadowHeight,
                                               int has3d, int overValid)
{
    return candidate && shadow && candidateColor == shadowColor &&
           width == shadowWidth && height == shadowHeight && has3d && overValid;
}

#endif
