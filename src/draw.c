#include "draw.h"
#include "font.h"

static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006;

struct DMA {
    void const *src;
    void       *dst;
    uint32_t    cnt;
};
static struct DMA volatile *dma = (struct DMA volatile*)0x040000B0;

struct rgb555 *palette = (struct rgb555*)0x05000000;

void draw_init(void) {
    uint16_t const mode4 = 4,
                     bg2 = 1<<10;
    *reg_dispcnt = mode4 | bg2;
}

struct fb* vsync_swap(void) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

    *reg_dispcnt ^= (1<<4);
    if (*reg_dispcnt & (1<<4)) {
        return (struct fb*)0x06000000;
    } else {
        return (struct fb*)0x0600A000;
    }
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
    struct fb const src = {.lo=color, .hi=color};
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
