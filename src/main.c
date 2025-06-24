#include <stdint.h>
#include <stdbool.h>
#include "font.h"

static int const W = 240,
                 H = 160;

struct DMA {
    void const *src;
    void       *dst;
    uint32_t    cnt;
};
static struct DMA volatile *dma  = (struct DMA volatile*)0x040000B0;

static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006,
                         *reg_keys    = (uint16_t volatile*)0x04000130;

static uint16_t *palette  = (uint16_t*)0x05000000,
                *front_fb = (uint16_t*)0x06000000,
                *back_fb  = (uint16_t*)0x0600A000;

static uint16_t* vsync_swap(void) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

    *reg_dispcnt ^= (1<<4);
    if (*reg_dispcnt & (1<<4)) {
        return front_fb;
    } else {
        return back_fb;
    }
}

union rgb555 {
    uint16_t rgbx;
    struct {
        uint16_t r : 5;
        uint16_t g : 5;
        uint16_t b : 5;
        uint16_t x : 1;
    };
};

union mode4_pair {
    uint16_t both;
    struct { uint8_t lo,hi; };
};

static inline void set_pixel(uint16_t *fb, int x, int y, uint8_t color) {
    int const ix = y * (W/2) + x/2;
    union mode4_pair px = {fb[ix]};
    if (x & 1) {
        px.hi = color;
    } else {
        px.lo = color;
    }
    fb[ix] = px.both;
}

static void fill_rect(uint16_t *fb, int l, int t, int w, int h, uint8_t color) {
    int const r = l+w,
              b = t+h;
    for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) {
        set_pixel(fb, x,y, color);
    }
}

static void clear(uint16_t *fb, uint8_t color) {
    union mode4_pair const src = {.lo=color, .hi=color};
    if (dma[3].cnt & (1u<<31)) __builtin_trap();
    dma[3].src = &src;
    dma[3].dst = fb;
    dma[3].cnt = (W*H/2) | (2<<23) | (1u<<31);
    if (dma[3].cnt & (1u<<31)) __builtin_trap();
}

static inline uint16_t keys_curr(void) {
    return ~*reg_keys & 0x03FF;
}

static void draw_char(uint16_t *fb, int x, int y, char ch, uint8_t color) {
    uint8_t const *glyph = font_get(ch);
    for (int r = 0; r < 8; r++) {
        uint8_t bits = glyph[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (1 << (7 - c)))
                set_pixel(fb, x+c, y+r, color);
        }
    }
}

static void draw_str(uint16_t *fb, int x, int y, char const *s, uint8_t color) {
    for (; *s; s++, x += 8)
        draw_char(fb, x, y, *s, color);
}

__attribute__((optnone))
static void draw_num(uint16_t *fb, int x, int y, int v, uint8_t color) {
    int tens = 0;
    while (v >= 10) { v -= 10; tens++; }
    if (tens) {
        draw_char(fb, x, y, (char)('0' + tens), color);
        x += 8;
    }
    draw_char(fb, x, y, (char)('0' + v), color);
}


static int const paddle_h    = 30,
                 paddle_w    = 4,
                 ball_size   = 4,
                 paddle_speed = 3,
                 ball_speed   = 768;

struct paddle { int y; };
struct ball { int x,y,vx,vy; };

static union rgb555 const warm[] = {
    {.r=31,.g=0, .b=0 },
    {.r=31,.g=10,.b=0 },
    {.r=31,.g=20,.b=0 },
    {.r=31,.g=31,.b=0 },
};
static union rgb555 const cool[] = {
    {.r=0, .g=0, .b=31},
    {.r=0, .g=31,.b=31},
    {.r=0, .g=31,.b=0 },
    {.r=10,.g=10,.b=31},
};
static int const NUM_WARM = sizeof(warm)/sizeof(warm[0]);

void main(void) {
    *reg_dispcnt = 4 | (1<<10);

    enum {BG,WHITE,LEFT,RIGHT};
    palette[BG   ] = ((union rgb555){.r=31,.g=31,.b=31}).rgbx;
    palette[WHITE] = ((union rgb555){.r=31,.g=31,.b=31}).rgbx;
    palette[LEFT ] = warm[0].rgbx;
    palette[RIGHT] = cool[0].rgbx;

    clear(front_fb, BG);
    clear( back_fb, BG);

    struct paddle left  = {.y = (H - paddle_h)/2};
    struct paddle right = {.y = (H - paddle_h)/2};
    struct ball ball = {
        .x = (W/2 - ball_size/2) << 8,
        .y = (H/2 - ball_size/2) << 8,
        .vx = -ball_speed,
        .vy = 0,
    };

    int score1 = 0, score2 = 0;
    int warm_idx = 0;
    bool game_over = false;
    int winner = 0;

    for (uint16_t *fb; (fb = vsync_swap());) {
        uint16_t keys = keys_curr();

        if (!game_over) {
            if (keys & (1<<6)) { if (left.y > 0) left.y -= paddle_speed; }
            if (keys & (1<<7)) { if (left.y < H-paddle_h) left.y += paddle_speed; }
            if (keys & (1<<0)) { if (right.y > 0) right.y -= paddle_speed; }
            if (keys & (1<<1)) { if (right.y < H-paddle_h) right.y += paddle_speed; }

            static uint16_t prev = 0; // for edge detection
            if ((keys & (1<<2)) && !(prev & (1<<2))) {
                warm_idx = (warm_idx+1)%NUM_WARM;
                palette[LEFT] = warm[warm_idx].rgbx;
            }
            prev = keys;

            ball.x += ball.vx;
            ball.y += ball.vy;

            int bx = ball.x>>8;
            int by = ball.y>>8;

            if (by <=0 && ball.vy<0) ball.vy = -ball.vy;
            if (by >= H-ball_size && ball.vy>0) ball.vy = -ball.vy;

            int left_x  = 10;
            int right_x = W-10-paddle_w;

            if (bx <= left_x + paddle_w &&
                bx + ball_size >= left_x &&
                by + ball_size >= left.y && by <= left.y + paddle_h) {
                int offset = (by + ball_size/2) - (left.y + paddle_h/2);
                ball.x = (left_x + paddle_w)<<8;
                ball.vx = ball_speed;
                ball.vy = offset*32;
            }
            if (bx + ball_size >= right_x &&
                bx <= right_x + paddle_w &&
                by + ball_size >= right.y && by <= right.y + paddle_h) {
                int offset = (by + ball_size/2) - (right.y + paddle_h/2);
                ball.x = (right_x - ball_size)<<8;
                ball.vx = -ball_speed;
                ball.vy = offset*32;
            }

            if (bx < 0) {
                score2++;
                ball.x = (W/2)<<8;
                ball.y = (H/2)<<8;
                ball.vx = ball_speed;
                ball.vy = 0;
                left.y = right.y = (H - paddle_h)/2;
            } else if (bx > W-ball_size) {
                score1++;
                ball.x = (W/2)<<8;
                ball.y = (H/2)<<8;
                ball.vx = -ball_speed;
                ball.vy = 0;
                left.y = right.y = (H - paddle_h)/2;
            }

            int diff = score1 - score2;
            if ((score1 >=11 || score2 >=11) && (diff>=2 || diff<=-2)) {
                game_over = true;
                winner = diff > 0 ? 1 : 2;
            }
        }

        clear(fb, BG);
        int left_x  = 10;
        int right_x = W-10-paddle_w;
        fill_rect(fb, left_x,  left.y, paddle_w, paddle_h, LEFT);
        fill_rect(fb, right_x, right.y, paddle_w, paddle_h, RIGHT);
        fill_rect(fb, ball.x>>8, ball.y>>8, ball_size, ball_size, WHITE);
        draw_num(fb, 30,10, score1, LEFT);
        draw_num(fb, W-30-8*(score2>=10?2:1),10, score2, RIGHT);
        if (game_over) {
            char const *msg = winner==1?"P1 WINS!":"P2 WINS!";
            draw_str(fb, (W-8*7)/2, H/2-4, msg, winner==1?LEFT:RIGHT);
        }
    }
}
