#include "font.h"
#include "sprites.h"

static void mem_copy(uint16_t volatile *dst, const uint16_t *src, int count) {
    for (int i = 0; i < count; i++) dst[i] = src[i];
}
#include <stdint.h>

#define len(x) (int)(sizeof x / sizeof *x)

static int const W = 240,
                 H = 160;

struct rgb555 {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t   : 1;
};
static struct rgb555 volatile *const  bg_palette = (struct rgb555 volatile*)0x05000000,
                              *const obj_palette = (struct rgb555 volatile*)0x05000200;

struct dispcnt {
    uint16_t mode              : 3;
    uint16_t cgb_mode          : 1;
    uint16_t display_frame     : 1;
    uint16_t hblank_free       : 1;
    uint16_t obj_vram_mapping  : 1;
    uint16_t forced_blank      : 1;
    uint16_t enable_bg0        : 1;
    uint16_t enable_bg1        : 1;
    uint16_t enable_bg2        : 1;
    uint16_t enable_bg3        : 1;
    uint16_t enable_obj        : 1;
    uint16_t enable_win0       : 1;
    uint16_t enable_win1       : 1;
    uint16_t enable_objwin     : 1;
};
static struct dispcnt volatile *const reg_dispcnt = (struct dispcnt volatile*)0x04000000;

struct bgcnt {
    uint16_t priority          : 2;
    uint16_t char_base_block   : 2;
    uint16_t mosaic            : 1;
    uint16_t color_mode        : 1;
    uint16_t                   : 2;
    uint16_t screen_base_block : 5;
    uint16_t area_overflow     : 1;
    uint16_t screen_size       : 2;
};
static struct bgcnt volatile *const reg_bg0cnt  = (struct bgcnt volatile*)0x04000008;

static uint16_t volatile *const reg_vcount = (uint16_t volatile*)0x04000006;
static uint16_t volatile *const reg_keys   = (uint16_t volatile*)0x04000130;

static uint16_t volatile *const bg_tiles  = (uint16_t*)0x06000000,
                         *const bg_map    = (uint16_t*)0x0600F800,
                         *const obj_tiles = (uint16_t*)0x06010000;

static struct rgb555 const warm_colors[] = {
    {31, 0, 0}, {31,10, 0}, {31,20, 0}, {31,31, 0},
};
static struct rgb555 const cool_colors[] = {
    { 0, 0,31}, { 0,31,31}, { 0,31, 0}, {10,10,31},
};
static struct rgb555 const star_colors[] = {
    {31, 0, 0}, {31,31, 0}, { 0,31, 0}, { 0,31,31},
    { 0, 0,31}, {31, 0,31}, {31,15, 0}, {15, 0,31},
};

static struct rgb555 const spectator_palette[] = {
    {31,31,31}, // transparent / white
    { 0, 0, 0}, // black outline
    {31,24,16}, // skin
    { 0, 0,31}, // shirt
    {10,10,10}, // pants
    {24,12, 0}, // hair
    {31,31,31}, // eye white
};

struct oam {
    struct {
        uint16_t y       :  8;
        uint16_t         :  1;
        uint16_t hide    :  1;
        uint16_t         :  4;
        uint16_t shape   :  2;
    } attr0;
    struct {
        uint16_t x       :  9;
        uint16_t         :  5;
        uint16_t size    :  2;
    } attr1;
    struct {
        uint16_t tile    : 10;
        uint16_t         :  2;
        uint16_t palbank :  4;
    } attr2;
    uint16_t             : 16;
};
static struct oam volatile *const oam = (struct oam volatile*)0x07000000;

struct star {
    _Accum x, y, vx, vy;
    uint8_t palbank;
    uint8_t pad[3];
};

enum {
    TILE_PADDLE       = 0,
    TILE_BALL         = 4,
    TILE_STAR         = 5,
    TILE_SPEC_MALE_SKINNY = 6,
    TILE_SPEC_MALE_NORMAL = TILE_SPEC_MALE_SKINNY + 9,
    TILE_SPEC_MALE_STRONG = TILE_SPEC_MALE_NORMAL + 9,
    TILE_SPEC_FEMALE_SKINNY = TILE_SPEC_MALE_STRONG + 9,
    TILE_SPEC_FEMALE_NORMAL = TILE_SPEC_FEMALE_SKINNY + 9,
    TILE_SPEC_FEMALE_STRONG = TILE_SPEC_FEMALE_NORMAL + 9,
};

#define SPEC_COUNT 6
static const struct { int tile; int x; } spectator_info[SPEC_COUNT] = {
    {TILE_SPEC_MALE_SKINNY,  10},
    {TILE_SPEC_MALE_NORMAL,  46},
    {TILE_SPEC_MALE_STRONG,  82},
    {TILE_SPEC_FEMALE_SKINNY,118},
    {TILE_SPEC_FEMALE_NORMAL,154},
    {TILE_SPEC_FEMALE_STRONG,190},
};
static const int spectator_y = H - 24;

static void font_to_tile(uint16_t volatile *tile, const uint8_t glyph[8]) {
    for (int r = 0; r < 8; r++) {
        uint8_t const bits = glyph[r];
        uint32_t mask = 0;
        for (int c = 0; c < 8; c++) {
            uint32_t nib = (bits & (1 << (7 - c))) ? 0x1u : 0x0u;
            mask |= nib << (c*4);
        }
        tile[r*2 + 0] = (uint16_t)( mask        & 0xFFFFu);
        tile[r*2 + 1] = (uint16_t)((mask >> 16) & 0xFFFFu);
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
        .enable_bg0       = 1,
        .enable_obj       = 1,
    };
    *reg_bg0cnt = (struct bgcnt){
        .screen_base_block = 31,
    };

    bg_palette[0] = (struct rgb555){31,31,31};
    bg_palette[1] = (struct rgb555){ 0, 0, 0};

    obj_palette[ 0] = (struct rgb555){31,31,31};
    int warm_idx = 0, cool_idx = 0;
    uint16_t old_keys = 0;

    obj_palette[ 1] = warm_colors[warm_idx];
    obj_palette[ 2] = (struct rgb555){ 0,31, 0};
    obj_palette[17] = cool_colors[cool_idx];
    for (int i = 0; i < len(star_colors); i++) {
        int base = (2 + i) * 16;
        obj_palette[base + 0] = (struct rgb555){31,31,31};
        obj_palette[base + 1] = star_colors[i];
    }

    int spec_base = 10 * 16;
    obj_palette[spec_base + 0] = spectator_palette[0];
    obj_palette[spec_base + 1] = spectator_palette[1];
    obj_palette[spec_base + 2] = spectator_palette[2];
    obj_palette[spec_base + 3] = spectator_palette[3];
    obj_palette[spec_base + 4] = spectator_palette[4];
    obj_palette[spec_base + 5] = spectator_palette[5];
    obj_palette[spec_base + 6] = spectator_palette[6];

    for (int ch = 32; ch < 127; ch++) {
        font_to_tile(bg_tiles + (ch-32+1)*16, font_get((char)ch));
    }
    for (int i = 0; i < 32*32; i++) {
        bg_map[i] = 0;
    }
    for (int i = 0; i < 128; i++) {
        oam[i] = (struct oam){.attr0.hide = 1};
    }

    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 16; i++) {
            obj_tiles[t*16 + i] = 0x1111;
        }
    }
    static const uint16_t ball_tile[] = {
        0x2200,0x0022, 0x2220,0x0222,
        0x2222,0x2222, 0x2222,0x2222,
        0x2222,0x2222, 0x2222,0x2222,
        0x2220,0x0222, 0x2200,0x0022,
    };
    for (int i = 0; i < 16; i++) {
        obj_tiles[4*16 + i] = ball_tile[i];
    }
    static const uint16_t star_tile[] = {
        0x1000,0x0000, 0x1100,0x0001,
        0x1010,0x0010, 0x1111,0x0111,
        0x1010,0x0010, 0x1100,0x0001,
        0x1000,0x0000, 0x0000,0x0000,
    };
    for (int i = 0; i < 16; i++) {
        obj_tiles[5*16 + i] = star_tile[i];
    }

    int tile_off = TILE_SPEC_MALE_SKINNY * 16;
    mem_copy(obj_tiles + tile_off, spectator_male_skinny, len(spectator_male_skinny));
    tile_off += 9*16;
    mem_copy(obj_tiles + tile_off, spectator_male_normal, len(spectator_male_normal));
    tile_off += 9*16;
    mem_copy(obj_tiles + tile_off, spectator_male_strong, len(spectator_male_strong));
    tile_off += 9*16;
    mem_copy(obj_tiles + tile_off, spectator_female_skinny, len(spectator_female_skinny));
    tile_off += 9*16;
    mem_copy(obj_tiles + tile_off, spectator_female_normal, len(spectator_female_normal));
    tile_off += 9*16;
    mem_copy(obj_tiles + tile_off, spectator_female_strong, len(spectator_female_strong));

    struct star stars[9];
    for (int i = 0; i < len(stars); i++) {
        stars[i] = (struct star){
            .x = (W-8)/2,
            .y = (H-8)/2,
            .vx = 0,
            .vy = 0,
            .palbank = (uint8_t)(2 + (i & 7)),
        };
    }
    int next_star_color = 0;

    _Accum left_y  = (H-32)/2;
    _Accum right_y = (H-32)/2;
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
        uint16_t pressed = keys & (uint16_t)~old_keys;
        old_keys = keys;

        if (pressed & (1<<2)) {
            warm_idx = (warm_idx + 1) % len(warm_colors);
            obj_palette[1] = warm_colors[warm_idx];
        }
        if (pressed & (1<<3)) {
            cool_idx = (cool_idx + 1) % len(cool_colors);
            obj_palette[17] = cool_colors[cool_idx];
        }

        if (keys & (1<<6)) { left_y  -= 2; }
        if (keys & (1<<7)) { left_y  += 2; }
        if (keys & (1<<0)) { right_y -= 2; }
        if (keys & (1<<1)) { right_y += 2; }

        if (left_y  < 0)     left_y  = 0;
        if (left_y  > H-32)  left_y  = H-32;
        if (right_y < 0)     right_y = 0;
        if (right_y > H-32)  right_y = H-32;

        for (int i = 0; i < len(stars); i++) {
            stars[i].x += stars[i].vx;
            stars[i].y += stars[i].vy;
            if (winner) {
                stars[i].vy += 1/256.0K;
                stars[i].palbank = (uint8_t)(2 + (++next_star_color % len(star_colors)));
            }
        }

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
                for (int i = 0; i < len(stars); i++) {
                    _Accum const s = 0.75K;
                    switch (i & 7) {
                        case 0:  stars[i].vx = +s;  stars[i].vy =  0;  break;
                        case 1:  stars[i].vx = +s;  stars[i].vy = -s; break;
                        case 2:  stars[i].vx =  0;  stars[i].vy = -s; break;
                        case 3:  stars[i].vx = -s;  stars[i].vy = -s; break;
                        case 4:  stars[i].vx = -s;  stars[i].vy =  0;  break;
                        case 5:  stars[i].vx = -s;  stars[i].vy = +s; break;
                        case 6:  stars[i].vx =  0;  stars[i].vy = +s; break;
                        default: stars[i].vx = +s;  stars[i].vy = +s; break;
                    }
                }
            }
        }

        struct oam sprite[3 + len(stars) + SPEC_COUNT*9];
        sprite[0] = (struct oam){
            .attr0 = { .y = left_y, .shape = 2 },
            .attr1 = { .x =     10, .size  = 1 },
            .attr2 = { .tile = 0, .palbank = 0 },
        };
        sprite[1] = (struct oam){
            .attr0 = { .y =  right_y, .shape = 2 },
            .attr1 = { .x = (W-10-8), .size  = 1 },
            .attr2 = { .tile = 0, .palbank = 1 },
        };
        sprite[2] = (struct oam){
            .attr0 = { .y = ball_y, .shape = 0, .hide = !!winner },
            .attr1 = { .x = ball_x, .size  = 0 },
            .attr2 = { .tile = 4, .palbank = 0 },
        };
        int idx = 3;
        for (int i = 0; i < len(stars); i++) {
            sprite[idx++] = (struct oam){
                .attr0 = { .y = stars[i].y, .shape = 0, .hide = !winner },
                .attr1 = { .x = stars[i].x, .size  = 0 },
                .attr2 = { .tile = 5, .palbank = stars[i].palbank },
            };
        }

        for (int s = 0; s < SPEC_COUNT; s++) {
            int base = spectator_info[s].tile;
            int x = spectator_info[s].x;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    sprite[idx++] = (struct oam){
                        .attr0 = { .y = (uint16_t)(spectator_y + r*8), .shape = 0 },
                        .attr1 = { .x = (uint16_t)(x + c*8), .size = 0 },
                        .attr2 = { .tile = (uint16_t)(base + r*3 + c), .palbank = 10 },
                    };
                }
            }
        }

        vsync();

        for (int i = 0; i < len(sprite); i++) {
            oam[i].attr0 = sprite[i].attr0;
            oam[i].attr1 = sprite[i].attr1;
            oam[i].attr2 = sprite[i].attr2;
        }
        bg_draw_num( 3,1, score_l);
        bg_draw_num(27,1, score_r);
        if (winner) {
            bg_draw_str(12,10, winner==1 ? "P1 WINS!" : "P2 WINS!");
        }
    }
}
