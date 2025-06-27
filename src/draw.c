#include "draw.h"
#include "font.h"

static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006,
                         *reg_bg0cnt  = (uint16_t volatile*)0x04000008;

struct DMA {
    void const *src;
    void       *dst;
    uint32_t    cnt;
};
static struct DMA volatile *dma = (struct DMA volatile*)0x040000B0;

struct rgb555 *palette      = (struct rgb555*)0x05000000;
struct rgb555 *obj_palette  = (struct rgb555*)0x05000200;

/* Step 4 sprite definitions */
static volatile struct oam_entry *const oam =
    (volatile struct oam_entry*)0x07000000;

void sprite_init(struct oam_entry shadow[128]) {
    /* Hide all sprites */
    for (int i = 0; i < 128; i++) {
        shadow[i].attr0 = 0x0200; /* disable */
        shadow[i].attr1 = 0;
        shadow[i].attr2 = 0;
        shadow[i].pad   = 0;
    }

    /* Two 8x8 tiles form an 8x16 paddle in OBJ VRAM (charblock 4) */
    uint16_t *obj_tiles = (uint16_t*)0x06010000;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 16; i++) {
            obj_tiles[t*16 + i] = 0x1111; /* palette index 1 */
        }
    }
}

void sprite_flush(struct oam_entry const shadow[128]) {
    for (int i = 0; i < 128; i++) {
        oam[i].attr0 = shadow[i].attr0;
        oam[i].attr1 = shadow[i].attr1;
        oam[i].attr2 = shadow[i].attr2;
        oam[i].pad   = shadow[i].pad;
    }
}

static void font_to_tile(uint16_t *dst, const uint8_t src[8]) {
    /*
       Convert an 8x8 1bpp glyph into a 4bpp tile.  VRAM must be written
       using halfword accesses, so we compose each row as two uint16_t words
       before storing them.

       Each tile row holds eight pixels.  The lower nibble of a word is the
       leftmost pixel and the upper nibble is the next pixel to the right.
    */
    for (int r = 0; r < 8; r++) {
        uint8_t bits = src[r];
        uint8_t n[8];
        for (int c = 0; c < 8; c++) {
            n[c] = (bits & (1 << (7 - c))) ? 1 : 0;
        }
        dst[r*2 + 0] = (uint16_t)(n[0] | (n[1] << 4) | (n[2] << 8) | (n[3] << 12));
        dst[r*2 + 1] = (uint16_t)(n[4] | (n[5] << 4) | (n[6] << 8) | (n[7] << 12));
    }
}

void draw_init(void) {
    /* Step 2: switch to tile background mode */
    uint16_t const mode0 = 0,
                     bg0  = 1<<8,
                     obj  = 1<<12,
                     obj_1d = 1<<6; /* use 1D sprite mapping */
    *reg_dispcnt = mode0 | bg0 | obj | obj_1d;

    /* BG0: charblock 0, screenblock 31, 4bpp */
    *reg_bg0cnt = (0<<2) | (0<<7) | (31<<8);

    uint16_t *tilemem  = (uint16_t*)0x06000000;
    uint16_t *mapmem   = (uint16_t*)0x0600F800;

    /* Step 2 demo tile filled with palette index 0 (white background) */
    for (int i = 0; i < 16; i++) {
        tilemem[i] = 0x0000;
    }
    for (int i = 0; i < 32*32; i++) {
        mapmem[i] = 0;
    }

    /* Step 3: convert font digits to 4bpp tiles */

    char digits[] = "0123456789";
    for (int i = 0; i < 10; i++) {
        font_to_tile(tilemem + (i+1)*16, font_get(digits[i]));
    }

    /* place digits on the first row of the map */
    for (int i = 0; i < 10; i++) {
        mapmem[i] = (uint16_t)(i+1);
    }
}

struct fb* vsync_swap(struct oam_entry const shadow[128]) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

    sprite_flush(shadow);

    return (struct fb*)0x06000000; /* unused in tile mode */
}

void set_pixel(struct fb *fb, uint8_t color, int x, int y) {
    if ((unsigned)x < W && (unsigned)y < H) {
        struct fb *px = fb + y * (W/2) + x/2;
        if (x & 1) {
            px->hi = color;
        } else {
            px->lo = color;
        }
    }
}

void fill_rect(struct fb *fb, uint8_t color, int l, int t, int w, int h) {
    int const r = l+w,
              b = t+h;
    for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) {
        set_pixel(fb, color, x,y);
    }
}

void clear(struct fb *fb, uint8_t color) {
    static struct fb src;
    src.lo = src.hi = color;
    dma[3].src = &src;
    dma[3].dst = fb;
    dma[3].cnt = (W*H/2) | (2<<23) | (1u<<31);
}

void draw_char(struct fb *fb, uint8_t color, int x, int y, char ch) {
    uint8_t const *glyph = font_get(ch);
    for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
        if (glyph[r] & (1 << (7 - c))) {
            set_pixel(fb, color, x+c,y+r);
        }
    }
}

void draw_str(struct fb *fb, uint8_t color, int x, int y, char const *s) {
    for (; *s; x += 8) {
        draw_char(fb, color, x,y, *s++);
    }
}


void draw_num(struct fb *fb, uint8_t color, int x, int y, int v) {
    if (v >= 100) __builtin_trap();
    if (v >= 10) {
        int const tens = (v * 103) >> 10;
        draw_char(fb, color, x,y, (char)('0' + tens));
        x += 8;
        v -= 10*tens;
    }
    draw_char(fb, color, x,y, (char)('0' + v));
}
