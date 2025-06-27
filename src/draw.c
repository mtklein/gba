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

struct rgb555 *palette = (struct rgb555*)0x05000000;

static void font_to_tile(uint8_t *dst, const uint8_t src[8]) {
    for (int r = 0; r < 8; r++) {
        uint8_t bits = src[r];
        for (int c = 0; c < 8; c += 2) {
            uint8_t hi = (bits & (1 << (7 - c))) ? 1 : 0;
            uint8_t lo = (bits & (1 << (7 - (c+1)))) ? 1 : 0;
            *dst++ = (uint8_t)((hi << 4) | lo);
        }
    }
}

void draw_init(void) {
    /* Step 2: switch to tile background mode */
    uint16_t const mode0 = 0,
                     bg0   = 1<<8;
    *reg_dispcnt = mode0 | bg0;

    /* BG0: charblock 0, screenblock 31, 4bpp */
    *reg_bg0cnt = (0<<2) | (0<<7) | (31<<8);

    uint16_t *tilemem  = (uint16_t*)0x06000000;
    uint16_t *mapmem   = (uint16_t*)0x0600F800;

    /* Step 2 demo tile filled with palette index 1 */
    for (int i = 0; i < 16; i++) {
        tilemem[i] = 0x1111;
    }
    for (int i = 0; i < 32*32; i++) {
        mapmem[i] = 0;
    }

    /* Step 3: convert font digits to 4bpp tiles */

    char digits[] = "0123456789";
    for (int i = 0; i < 10; i++) {
        font_to_tile((uint8_t*)tilemem + (i+1)*32, font_get(digits[i]));
    }

    /* place digits on the first row of the map */
    for (int i = 0; i < 10; i++) {
        mapmem[i] = (uint16_t)(i+1);
    }
}

struct fb* vsync_swap(void) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

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
