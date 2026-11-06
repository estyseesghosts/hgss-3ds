#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TEX_CACHE_ENTRIES 256
typedef struct { int used; uint32_t param,palette; } TexEntry;
static TexEntry sTextures[TEX_CACHE_ENTRIES];
#include "../../3ds_port/game/app/port_texture_hint.h"
static uint32_t rng=0x81234567u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static TexEntry *reference(uint32_t param,uint32_t palette,TexEntry **free_slot)
{
    int i; *free_slot=NULL;
    for(i=0;i<TEX_CACHE_ENTRIES;i++){
        TexEntry *e=&sTextures[i];
        if(e->used && e->param==param && e->palette==palette)return e;
        if(!e->used && !*free_slot)*free_slot=e;
    }
    return NULL;
}
int main(void)
{
    unsigned i;
    TexEntry *f;
    uint32_t other=2;
    sTextures[0].used=1;sTextures[0].param=1;sTextures[0].palette=0;
    if(find_texture_hint(1,0,&f)!=&sTextures[0] ||
       find_texture_hint(1,0,&f)!=&sTextures[0])return 2;
    while(texture_hint_key(other,0)!=texture_hint_key(1,0))other++;
    sTextures[1].used=1;sTextures[1].param=other;sTextures[1].palette=0;
    if(find_texture_hint(other,0,&f)!=&sTextures[1] ||
       find_texture_hint(1,0,&f)!=&sTextures[0])return 3;
    sTextures[0].used=0;
    if(find_texture_hint(1,0,&f)!=NULL || f!=&sTextures[0])return 4;
    for(i=0;i<500000;i++){
        TexEntry *a,*b,*af,*bf;
        unsigned slot=next()%TEX_CACHE_ENTRIES;
        uint32_t param,palette;
        /* Repeated key lookup, arbitrary frees and slot reuse induce stale
         * hints; random keys and a 256-slot hint table force collisions. */
        if((i%50000)==0)memset(sTextures,0,sizeof(sTextures));
        else if((i%3)==0)sTextures[slot].used=0;
        if((i%2)==0 && sTextures[slot].used){param=sTextures[slot].param;palette=sTextures[slot].palette;}
        else {param=next();palette=next()%32;}
        a=reference(param,palette,&af);b=find_texture_hint(param,palette,&bf);
        if(a!=b || (!a && af!=bf)){fprintf(stderr,"hint mismatch at %u\n",i);return 1;}
        if(!a && af){af->used=1;af->param=param;af->palette=palette;}
    }
    puts("texture cache hints: 500000 reference comparisons passed");
    return 0;
}
