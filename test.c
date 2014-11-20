#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zhban.h"

int main(int argc, char *argv[]) {
    FILE *tfp;
    
    //tfp = fopen(argv[1], "r");
    tfp = fopen("/usr/share/fonts/truetype/droid/DroidSans.ttf", "r");
    if (!tfp)
        return 1;

    fseek(tfp, 0, SEEK_END);
    uint32_t fsize = ftell(tfp);
    fseek(tfp, 0, SEEK_SET);
    
    void *fbuf = malloc(fsize);
    fread(fbuf, 1, fsize, tfp);
    fclose(tfp);
    
    zhban_t *zhban = zhban_open(fbuf, fsize, 18, 1, 1<<20, 1<<16, 1<<24, 5, NULL);
    if (!zhban)
        return 1;
    
    //tfp = fopen(argv[2], "r");
    tfp = fopen("rr", "r");
    fseek(tfp, 0, SEEK_END);
    fsize = ftell(tfp);
    fseek(tfp, 0, SEEK_SET);
    void *tbuf = malloc(fsize);
    fread(tbuf, 1, fsize, tfp);
    fclose(tfp);
    
    /* testing strategy: ? */
    uint16_t *p, *ep, *et = (uint16_t*)tbuf + fsize/2 - 1;
    *et = 10;

    zhban_shape_t **zsa;
    zhban_bitmap_t *zb;
    int sindex;
    zsa = malloc(sizeof(zhban_shape_t *) * fsize/2);
    for (int i=0; i< 1<<12 ; i++) {
        memset(zsa, 0, sizeof(zhban_shape_t *) * fsize/2);
        ep = p = (uint16_t *)tbuf;
        sindex = 0;
        if (*p == 0xFEFF)
            p++;
        do {
            while ((*ep != 10) && (et-ep > 0))
                ep++; 
            //printf("str %p->%p: #%s#\n\n", p, ep, utf16to8(p, (ep-p)));
            zsa[sindex] = zhban_shape(zhban, p, 2*( ep - p));
            sindex ++;
            ep = p = ep + 1;
        } while(et - ep > 0);
        
        for (int j=0; j < sindex ; j++) {
            zb = zhban_render(zhban, zsa[j]);
            zhban_release_shape(zhban, zsa[j]);
            if (!zb)
                return 1;
        }
        break;
    }

    printf("glyph misses %d/%d; size %d;\n%d spans %d glyphs; %.2f spans/glyph\nshaper misses %d/%d size %d\nbitmap misses %d/%d size %d\n", 
        zhban->glyph_gets - zhban->glyph_hits, zhban->glyph_gets, zhban->glyph_size,
        zhban->glyph_spans_seen, zhban->glyph_rendered, ((float)zhban->glyph_spans_seen)/zhban->glyph_rendered,
        zhban->shaper_gets - zhban->shaper_hits, zhban->shaper_gets, zhban->shaper_size,
        zhban->bitmap_gets - zhban->bitmap_hits, zhban->bitmap_gets, zhban->bitmap_size);
    
    zhban_drop(zhban);
    free(fbuf);
    free(tbuf);
    free(zsa);
    return 1;
}
    