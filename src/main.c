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

struct dispcnt {
    uint16_t mode              : 3;
    uint16_t cgb_mode          : 1;
    uint16_t display_frame     : 1;
    uint16_t hblank_free       : 1;
    uint16_t obj_vram_mapping  : 1; /* bit 6 */
    uint16_t forced_blank      : 1;
    uint16_t bg0_enable        : 1; /* bit 8 */
    uint16_t bg1_enable        : 1;
    uint16_t bg2_enable        : 1;
    uint16_t bg3_enable        : 1;
    uint16_t obj_enable        : 1; /* bit 12 */
    uint16_t win0_enable       : 1;
    uint16_t win1_enable       : 1;
    uint16_t objwin_enable     : 1;
};

struct bgcnt {
    uint16_t priority          : 2;
    uint16_t char_base_block   : 2;
    uint16_t mosaic            : 1;
    uint16_t color_mode        : 1;
    uint16_t _unused0          : 2; /* bits 6-7 */
    uint16_t screen_base_block : 5; /* bits 8-12 */
    uint16_t area_overflow     : 1;
    uint16_t screen_size       : 2;
};

static struct dispcnt volatile *const reg_dispcnt = (struct dispcnt volatile*)0x04000000;
static uint16_t   volatile *const reg_vcount  = (uint16_t   volatile*)0x04000006;
static struct bgcnt  volatile *const reg_bg0cnt  = (struct bgcnt  volatile*)0x04000008;
static uint16_t   volatile *const reg_keys    = (uint16_t   volatile*)0x04000130;

static struct rgb555 volatile *const  bg_palette = (struct rgb555 volatile*)0x05000000;
static struct rgb555 volatile *const obj_palette = (struct rgb555 volatile*)0x05000200;

static uint16_t volatile *const bg_tiles  = (uint16_t*)0x06000000,
                         *const bg_map    = (uint16_t*)0x0600F800,
                         *const obj_tiles = (uint16_t*)0x06010000;

struct sprite {
    uint16_t attr0, attr1, attr2;
};
struct oam {
    struct sprite sprite;
    uint16_t      pad;
};
static struct oam volatile *const oam = (struct oam volatile*)0x07000000;

static void font_to_tile(uint16_t volatile *tile, const uint8_t glyph[8]) {
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
    *reg_dispcnt = (struct dispcnt){
        .obj_vram_mapping = 1,
        .bg0_enable       = 1,
        .obj_enable       = 1,
    };
    *reg_bg0cnt  = (struct bgcnt){
        .screen_base_block = 31,
    };

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

    for (int t = 0; t < 4; t++) {
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
        obj_tiles[4*16 + i] = ball_tile[i];
    }

    int    left_y  = (H-32)/2;
    int    right_y = (H-32)/2;
    _Accum ball_x  = (W-8)/2;
    _Accum ball_y  = (H-8)/2;
    _Accum ball_vx = 1.5K;
    _Accum ball_vy = 0;
    _Accum ball_ay = 1/256.0K;
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
        if (left_y  > H-32)  left_y  = H-32;
        if (right_y < 0)     right_y = 0;
        if (right_y > H-32)  right_y = H-32;

        if (!winner) {
            ball_vy += ball_ay;
            ball_x  += ball_vx;
            ball_y  += ball_vy;

            int const bx = (int)ball_x,
                      by = (int)ball_y;

            if (by <= 0 || by >= H-8) ball_vy = -ball_vy;

            if (bx <= 10+8 && bx+8 >= 10 &&
                by+8 >= left_y && by <= left_y+32 && ball_vx < 0) {
                int const offset = (by + 4) - (left_y + 16);
                ball_vx = -ball_vx;
                ball_vy = offset >> 3;
            }

            if (bx+8 >= W-10-8 && bx <= W-10-8+8 &&
                by+8 >= right_y && by <= right_y+32 && ball_vx > 0) {
                int const offset = (by + 4) - (right_y + 16);
                ball_vx = -ball_vx;
                ball_vy = offset >> 3;
            }

            if (bx < 0) {
                score_r++;
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = -ball_vx;
                ball_vy = 0;
            } else if (bx > W-8) {
                score_l++;
                ball_x = (W-8)/2;
                ball_y = (H-8)/2;
                ball_vx = -ball_vx;
                ball_vy = 0;
            }

            int const diff = score_l - score_r;
            if ((score_l>=11 || score_r>=11) && (diff>=2 || diff<=-2)) {
                winner = diff>0 ? 1 : 2;
            }
        }

        struct sprite sprite[] = {
            {
                .attr0 = (left_y & 0xFF) | 0x8000, /* tall */
                .attr1 = (10 & 0x1FF) | 0x4000,               /* x */
                .attr2 = 0 | (0<<12),              /* tile 0, palbank 0 */
            },
            {
                .attr0 = (right_y & 0xFF) | 0x8000,
                .attr1 = ((W-10-8) & 0x1FF) | 0x4000,
                .attr2 = 0 | (1<<12),              /* palbank 1 */
            },
            {
                .attr0 = ((int)ball_y & 0xFF) | (winner ? disable : 0),
                .attr1 = (int)ball_x & 0x1FF,
                .attr2 = 4 | (0<<12),             /* tile 4, palbank 0 */
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
