#include <stdint.h>
#include "font.h"

/* Former draw.h contents */
static int const W = 240,
                 H = 160;

struct rgb555 {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t x : 1;
};

struct fb {
    uint16_t lo : 8;
    uint16_t hi : 8;
};

/* Hardware registers and helpers formerly in draw.c */
static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006,
                         *reg_bg0cnt  = (uint16_t volatile*)0x04000008;

struct DMA {
    void const *src;
    void       *dst;
    uint32_t    cnt;
};
static struct DMA volatile *dma = (struct DMA volatile*)0x040000B0;

static struct rgb555 *palette     = (struct rgb555*)0x05000000;
static struct rgb555 *obj_palette = (struct rgb555*)0x05000200;

/* BG0 tile and map memory pointers */
static uint16_t *bg_tiles;
static uint16_t *bg_map;

/* Step 4 sprite definitions */
struct oam_entry {
    uint16_t attr0;
    uint16_t attr1;
    uint16_t attr2;
    uint16_t pad;
};

static volatile struct oam_entry *const oam =
    (volatile struct oam_entry*)0x07000000;

static void sprite_init(struct oam_entry shadow[128]) {
    /* Hide all sprites */
    for (int i = 0; i < 128; i++) {
        shadow[i].attr0 = 0x0200; /* disable */
        shadow[i].attr1 = 0;
        shadow[i].attr2 = 0;
        shadow[i].pad   = 0;
    }

    /* Two 8x8 tiles form an 8x16 paddle in OBJ VRAM (charblock 4).
       A third tile is used for the ball. */
    uint16_t *obj_tiles = (uint16_t*)0x06010000;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 16; i++) {
            obj_tiles[t*16 + i] = 0x1111; /* palette index 1 */
        }
    }
    static const uint16_t ball_tile[16] = {
        0x2200,0x0022,
        0x2220,0x0222,
        0x2222,0x2222,
        0x2222,0x2222,
        0x2222,0x2222,
        0x2222,0x2222,
        0x2220,0x0222,
        0x2200,0x0022,
    };
    for (int i = 0; i < 16; i++) {
        obj_tiles[2*16 + i] = ball_tile[i];
    }
}

static void sprite_flush(struct oam_entry const shadow[128]) {
    for (int i = 0; i < 128; i++) {
        oam[i].attr0 = shadow[i].attr0;
        oam[i].attr1 = shadow[i].attr1;
        oam[i].attr2 = shadow[i].attr2;
        oam[i].pad   = shadow[i].pad;
    }
}

static void font_to_tile(uint16_t *dst, const uint8_t src[8]) {
    /* Convert an 8x8 1bpp glyph into a 4bpp tile. */
    for (int r = 0; r < 8; r++) {
        uint8_t bits = src[r];
        uint8_t n[8];
        for (int c = 0; c < 8; c++) {
            n[c] = (bits & (1 << (7 - c))) ? 1 : 0;
        }
        dst[r*2 + 0] =
            (uint16_t)(n[0] | (n[1] << 4) | (n[2] << 8) | (n[3] << 12));
        dst[r*2 + 1] =
            (uint16_t)(n[4] | (n[5] << 4) | (n[6] << 8) | (n[7] << 12));
    }
}

static void draw_init(void) {
    uint16_t const mode0   = 0,
                     bg0    = 1<<8,
                     obj    = 1<<12,
                     obj_1d = 1<<6;
    *reg_dispcnt = mode0 | bg0 | obj | obj_1d;

    /* BG0: charblock 0, screenblock 31, 4bpp */
    *reg_bg0cnt = (0<<2) | (0<<7) | (31<<8);

    bg_tiles = (uint16_t*)0x06000000;
    bg_map   = (uint16_t*)0x0600F800;

    /* Step 2 demo tile filled with palette index 0 (white background) */
    for (int i = 0; i < 16; i++) {
        bg_tiles[i] = 0x0000; /* blank tile */
    }
    for (int i = 0; i < 32*32; i++) {
        bg_map[i] = 0;        /* clear map */
    }

    /* Convert basic ASCII characters to tiles */
    for (int ch = 32; ch <= 126; ch++) {
        font_to_tile(bg_tiles + (ch-32+1)*16, font_get((char)ch));
    }
}

static inline uint16_t tile_for_char(char ch) {
    if (ch < 32 || ch > 126) return 0;
    return (uint16_t)(ch - 32 + 1);
}

static void bg_draw_char(int x, int y, char ch) {
    bg_map[y*32 + x] = tile_for_char(ch);
}

static void bg_draw_str(int x, int y, char const *s) {
    for (; *s; s++, x++) {
        bg_draw_char(x, y, *s);
    }
}

static void bg_draw_num(int x, int y, int v) {
    int tens = (v * 103) >> 10; /* approximate v/10 for v < 100 */
    int ones = v - tens*10;
    if (tens) {
        bg_draw_char(x, y, (char)('0' + tens));
    } else {
        bg_draw_char(x, y, ' ');
    }
    bg_draw_char(x+1, y, (char)('0' + ones));
}

static struct fb* vsync_swap(struct oam_entry const shadow[128]) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

    sprite_flush(shadow);

    return (struct fb*)0x06000000; /* unused in tile mode */
}

static void set_pixel(struct fb *fb, uint8_t color, int x, int y) {
    if ((unsigned)x < W && (unsigned)y < H) {
        struct fb *px = fb + y * (W/2) + x/2;
        if (x & 1) {
            px->hi = color;
        } else {
            px->lo = color;
        }
    }
}

static void fill_rect(struct fb *fb, uint8_t color,
                      int l, int t, int w, int h)
    __attribute__((unused));
static void fill_rect(struct fb *fb, uint8_t color,
                      int l, int t, int w, int h) {
    int const r = l+w,
              b = t+h;
    for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) {
        set_pixel(fb, color, x,y);
    }
}

static void clear(struct fb *fb, uint8_t color) __attribute__((unused));
static void clear(struct fb *fb, uint8_t color) {
    static struct fb src;
    src.lo = src.hi = color;
    dma[3].src = &src;
    dma[3].dst = fb;
    dma[3].cnt = (W*H/2) | (2<<23) | (1u<<31);
}

static void draw_char(struct fb *fb, uint8_t color,
                      int x, int y, char ch) {
    const uint8_t *glyph = font_get(ch);
    for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
        if (glyph[r] & (1 << (7 - c))) {
            set_pixel(fb, color, x+c,y+r);
        }
    }
}

static void draw_str(struct fb *fb, uint8_t color,
                     int x, int y, const char *s) __attribute__((unused));
static void draw_str(struct fb *fb, uint8_t color,
                     int x, int y, const char *s) {
    for (; *s; x += 8) {
        draw_char(fb, color, x,y, *s++);
    }
}

static void draw_num(struct fb *fb, uint8_t color,
                     int x, int y, int v) __attribute__((unused));
static void draw_num(struct fb *fb, uint8_t color,
                     int x, int y, int v) {
    if (v >= 100) __builtin_trap();
    if (v >= 10) {
        int const tens = (v * 103) >> 10;
        draw_char(fb, color, x,y, (char)('0' + tens));
        x += 8;
        v -= 10*tens;
    }
    draw_char(fb, color, x,y, (char)('0' + v));
}

static uint16_t volatile *reg_keys = (uint16_t volatile*)0x04000130;

void main(void) __attribute__((noreturn));
void main(void) {
    struct oam_entry shadow_oam[128];
    draw_init();

    /* Background and sprite palettes */
    palette[0] = (struct rgb555){31,31,31,0}; /* white */
    palette[1] = (struct rgb555){0,0,0,0};   /* black */

    obj_palette[0]  = palette[0];
    obj_palette[1]  = (struct rgb555){31,0,0,0};  /* left paddle */
    obj_palette[2]  = (struct rgb555){0,31,0,0};  /* ball */
    obj_palette[17] = (struct rgb555){0,0,31,0};  /* right paddle (bank 1 index 1) */

    sprite_init(shadow_oam);

    int left_y  = (H-16)/2;
    int right_y = (H-16)/2;
    int ball_x  = (W-8)/2;
    int ball_y  = (H-8)/2;
    int ball_vx = 2;
    int ball_vy = 1;
    int score_l = 0;
    int score_r = 0;
    int winner  = 0;

    bg_draw_num(3,1, score_l);
    bg_draw_num(27,1, score_r);

    for (;;) {
        uint16_t keys = ~*reg_keys;
        if (!winner) {
            if (keys & (1<<6)) { left_y  -= 2; }
            if (keys & (1<<7)) { left_y  += 2; }
            if (keys & (1<<0)) { right_y -= 2; }
            if (keys & (1<<1)) { right_y += 2; }
        }

        if (left_y  < 0)     left_y  = 0;
        if (left_y  > H-16)  left_y  = H-16;
        if (right_y < 0)     right_y = 0;
        if (right_y > H-16)  right_y = H-16;

        if (!winner) {
            ball_x += ball_vx;
            ball_y += ball_vy;
            if (ball_y <= 0 || ball_y >= H-8) ball_vy = -ball_vy;
        }

        if (!winner) {
            if (ball_x <= 10+8 && ball_x+8 >= 10 &&
                ball_y+8 >= left_y && ball_y <= left_y+16 && ball_vx < 0) {
                ball_vx = -ball_vx;
            }

            if (ball_x+8 >= W-10-8 && ball_x <= W-10-8+8 &&
                ball_y+8 >= right_y && ball_y <= right_y+16 && ball_vx > 0) {
                ball_vx = -ball_vx;
            }

            if (ball_x < 0) {
                score_r++;
                bg_draw_num(27,1, score_r);
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = 2;
                ball_vy = 1;
                left_y  = right_y = (H-16)/2;
            } else if (ball_x > W-8) {
                score_l++;
                bg_draw_num(3,1, score_l);
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = -2;
                ball_vy = 1;
                left_y  = right_y = (H-16)/2;
            }

            int diff = score_l - score_r;
            if ((score_l>=11 || score_r>=11) && (diff>=2 || diff<=-2)) {
                winner = diff>0 ? 1 : 2;
                bg_draw_str(12,10, winner==1 ? "P1 WINS!" : "P2 WINS!");
            }
        }

        shadow_oam[0].attr0 = (left_y & 0xFF) | 0x8000; /* tall */
        shadow_oam[0].attr1 = 10 & 0x1FF;               /* x */
        shadow_oam[0].attr2 = 0 | (0<<12);              /* tile 0, palbank 0 */

        shadow_oam[1].attr0 = (right_y & 0xFF) | 0x8000;
        shadow_oam[1].attr1 = (W-10-8) & 0x1FF;
        shadow_oam[1].attr2 = 0 | (1<<12);              /* palbank 1 */

        shadow_oam[2].attr0 = ball_y & 0xFF;
        shadow_oam[2].attr1 = ball_x & 0x1FF;
        shadow_oam[2].attr2 = 2 | (0<<12);             /* tile 2, palbank 0 */

        vsync_swap(shadow_oam);
    }
}
