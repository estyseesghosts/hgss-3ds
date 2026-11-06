#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HGSS_PALETTE_HASH 1
#include "../../3ds_port/nitro/src/port_gx3d_texture.c"
unsigned char NitroPort_IoReg[0x2000];
unsigned char NitroPort_LcdcVram[0xa4000];
static unsigned char palettes[6][0x4000];
static uint32_t rng=0x9e3779b9u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint32_t reference(uint32_t h,const NitroGx3dTexBanks *b,uint32_t off,uint32_t len)
{
    uint32_t i; for(i=0;i<len;i++)h=(h^palette_u8(b,off+i))*16777619u; return h;
}
int main(void)
{
    NitroGx3dTexBanks banks; unsigned i,j;
    static const uint32_t edges[]={0,1,0x3ffe,0x3fff,0x4000,0x17fff,0x18000,0xfffffff0u,0xffffffffu};
    memset(&banks,0,sizeof(banks));
    for(i=0;i<6;i++)for(j=0;j<0x4000;j++)palettes[i][j]=(unsigned char)next();
    for(i=0;i<20000;i++){
        uint32_t h=next(),off,len=next()%2049;
        for(j=0;j<6;j++)banks.palette[j]=(next()&1)?palettes[j]:NULL;
        off=(i%3==0)?edges[next()%(sizeof(edges)/sizeof(edges[0]))]:next()%0x1c000;
        if(mix_palette(h,&banks,off,len)!=reference(h,&banks,off,len)){
            fprintf(stderr,"palette hash mismatch: %08x length %u\n",off,len);return 1;
        }
    }
    puts("palette hash: 20000 reference comparisons passed"); return 0;
}
