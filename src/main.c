#include "font.h"
#include <stdint.h>

#define len(x) (int)(sizeof x / sizeof *x)

static int const W = 240,
                 H = 160;

struct rgb555 {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t x : 1;
};

static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006,
                         *reg_bg0cnt  = (uint16_t volatile*)0x04000008,
                         *reg_keys    = (uint16_t volatile*)0x04000130;

static volatile struct rgb555 * bg_palette = (volatile struct rgb555*)0x05000000;
static volatile struct rgb555 *obj_palette = (volatile struct rgb555*)0x05000200;

static volatile uint16_t *bg_tiles  = (uint16_t*)0x06000000,
                         *bg_map    = (uint16_t*)0x0600F800,
                         *obj_tiles = (uint16_t*)0x06010000;

struct sprite {
    uint16_t attr0, attr1, attr2;
};
struct oam {
    struct sprite sprite;
    uint16_t      pad;
};
static struct oam volatile *oam = (struct oam volatile*)0x07000000;

static void font_to_tile(volatile uint16_t *tile, const uint8_t glyph[8]) {
    for (int r = 0; r < 8; r++) {
        uint8_t const bits = glyph[r];
        uint8_t nib[8];
        for (int c = 0; c < 8; c++) {
            nib[c] = (bits & (1 << (7 - c))) ? 0x1 : 0x0;
        }
        tile[r*2 + 0] = (uint16_t)(nib[0] | (nib[1] << 4) | (nib[2] << 8) | (nib[3] << 12));
        tile[r*2 + 1] = (uint16_t)(nib[4] | (nib[5] << 4) | (nib[6] << 8) | (nib[7] << 12));
    }
}

static void bg_draw_char(int x, int y, char ch) {
    bg_map[y*32 + x] = (uint16_t)(ch - 32 + 1);
}

static void bg_draw_str(int x, int y, char const *s) {
    while (*s) {
        bg_draw_char(x++, y, *s++);
    }
}

static void bg_draw_num(int x, int y, int v) {
    int const tens = (v * 103) >> 10, // v/10 for v < 100
              ones = v - tens*10;
    bg_draw_char(x+0,y, tens ? (char)('0' + tens) : ' ');
    bg_draw_char(x+1,y,        (char)('0' + ones)      );
}

static void vsync(void) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);
}

__attribute__((noreturn))
void main(void) {
    *reg_dispcnt = 1<< 6 | 1<<8 | 1<<12;
    *reg_bg0cnt  = 31<<8;

    bg_palette[0] = (struct rgb555){.r=31, .g=31, .b=31};
    bg_palette[1] = (struct rgb555){.r= 0, .g= 0, .b= 0};

    obj_palette[ 0] = (struct rgb555){.r=31, .g=31, .b=31};
    obj_palette[ 1] = (struct rgb555){.r=31, .g= 0, .b= 0};
    obj_palette[ 2] = (struct rgb555){.r= 0, .g=31, .b= 0};
    obj_palette[17] = (struct rgb555){.r= 0, .g= 0, .b=31};

    for (int ch = 32; ch < 127; ch++) {
        font_to_tile(bg_tiles + (ch-32+1)*16, font_get((char)ch));
    }
    for (int i = 0; i < 32*32; i++) {
        bg_map[i] = 0;
    }

    uint16_t const disable = 0x0200;
    for (int i = 0; i < 128; i++) {
        oam[i].sprite.attr0 = disable;
    }

    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 16; i++) {
            obj_tiles[t*16 + i] = 0x1111;
        }
    }

    static const uint16_t ball_tile[16] = {
        0x2200,0x0022, 0x2220,0x0222,
        0x2222,0x2222, 0x2222,0x2222,
        0x2222,0x2222, 0x2222,0x2222,
        0x2220,0x0222, 0x2200,0x0022,
    };
    for (int i = 0; i < 16; i++) {
        obj_tiles[2*16 + i] = ball_tile[i];
    }

    int left_y  = (H-16)/2;
    int right_y = (H-16)/2;
    int ball_x  = (W-8)/2;
    int ball_y  = (H-8)/2;
    int ball_vx = 2;
    int ball_vy = 1;
    int score_l = 0;
    int score_r = 0;
    int winner  = 0;

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
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = 2;
                ball_vy = 1;
            } else if (ball_x > W-8) {
                score_l++;
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = -2;
                ball_vy = 1;
            }

            int const diff = score_l - score_r;
            if ((score_l>=11 || score_r>=11) && (diff>=2 || diff<=-2)) {
                winner = diff>0 ? 1 : 2;
            }
        }

        struct sprite sprite[] = {
            {
                .attr0 = (left_y & 0xFF) | 0x8000, /* tall */
                .attr1 = 10 & 0x1FF,               /* x */
                .attr2 = 0 | (0<<12),              /* tile 0, palbank 0 */
            },
            {
                .attr0 = (right_y & 0xFF) | 0x8000,
                .attr1 = (W-10-8) & 0x1FF,
                .attr2 = 0 | (1<<12),              /* palbank 1 */
            },
            {
                .attr0 = (ball_y & 0xFF) | (winner ? disable : 0),
                .attr1 = ball_x & 0x1FF,
                .attr2 = 2 | (0<<12),             /* tile 2, palbank 0 */
            },
        };

        vsync();

        for (int i = 0; i < len(sprite); i++) {
            oam[i].sprite.attr0 = sprite[i].attr0;
            oam[i].sprite.attr1 = sprite[i].attr1;
            oam[i].sprite.attr2 = sprite[i].attr2;
        }
        bg_draw_num( 3,1, score_l);
        bg_draw_num(27,1, score_r);
        if (winner) {
            bg_draw_str(12,10, winner==1 ? "P1 WINS!" : "P2 WINS!");
        }
    }
}
