/*
 * The two profiler hooks the classic 3D path never reaches.
 *
 * port_gx3d_pica.c calls these from its remaster/compose functions only
 * (NitroPort_PicaEdgeCopies and NitroPort_PicaDrawScene). The program links
 * the classic path, whose frames are one C3D_FrameBegin/FrameEnd pair, so
 * there is no split command list to cut and nothing to time. Their real
 * definitions live in Platinum's port_compose_pica.c, which is not part of
 * this program: it owns the remaster's whole display and its GPU composition.
 *
 * They are here rather than as no-op macros because the calls are in the
 * vendored object, which must stay byte-identical to platinum.lock.
 */
void NitroPort_PicaCut(const char *part)
{
    (void)part;
}

void NitroPort_PicaProfileMark(const char *part)
{
    (void)part;
}
