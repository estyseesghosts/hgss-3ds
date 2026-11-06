/* Included after TexEntry and sTextures. A hint is never authoritative: an
 * eviction, slot reuse, filter reset or key collision falls back to the
 * original full scan. Texture entries and hints have the same GPU owner. */
static uint16_t sTextureHints[256]; /* index + 1; zero means no hint */
static unsigned texture_hint_key(uint32_t param, uint32_t palette)
{
    uint32_t x=param ^ (param >> 11) ^ (palette * 0x9e3779b1u);
    return (unsigned)((x ^ (x >> 16)) & 255u);
}
static TexEntry *find_texture_hint(uint32_t param,uint32_t palette,TexEntry **free_slot)
{
    unsigned key=texture_hint_key(param,palette), hint=sTextureHints[key];
    int i;
    *free_slot=NULL;
    if(hint && hint<=TEX_CACHE_ENTRIES){
        TexEntry *e=&sTextures[hint-1];
        if(e->used && e->param==param && e->palette==palette)return e;
    }
    for(i=0;i<TEX_CACHE_ENTRIES;i++){
        TexEntry *e=&sTextures[i];
        if(e->used && e->param==param && e->palette==palette){
            sTextureHints[key]=(uint16_t)(i+1);
            return e;
        }
        if(!e->used && !*free_slot)*free_slot=e;
    }
    return NULL;
}
