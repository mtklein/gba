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

#define RGB15(r,g,b) ((r) | ((g)<<5) | ((b)<<10))

#define PADDLE_H 30
#define PADDLE_W 4
#define BALL_SIZE 4
#define PADDLE_SPEED 3
#define BALL_SPEED 768

struct paddle { int y; };
struct ball { int x,y,vx,vy; };

static uint16_t const warm[] = {
    RGB15(31,0,0), RGB15(31,10,0), RGB15(31,20,0), RGB15(31,31,0)
};
static uint16_t const cool[] = {
    RGB15(0,0,31), RGB15(0,31,31), RGB15(0,31,0), RGB15(10,10,31)
};
static int const NUM_WARM = sizeof(warm)/2;

void main(void) {
    *reg_dispcnt = 4 | (1<<10);

    enum {BG,WHITE,LEFT,RIGHT};
    palette[BG   ] = RGB15(31,31,31);
    palette[WHITE] = RGB15(31,31,31);
    palette[LEFT ] = warm[0];
    palette[RIGHT] = cool[0];

    clear(front_fb, BG);
    clear( back_fb, BG);

    struct paddle left  = {.y = (H - PADDLE_H)/2};
    struct paddle right = {.y = (H - PADDLE_H)/2};
    struct ball ball = {
        .x = (W/2 - BALL_SIZE/2) << 8,
        .y = (H/2 - BALL_SIZE/2) << 8,
        .vx = -BALL_SPEED,
        .vy = 0,
    };

    int score1 = 0, score2 = 0;
    int warm_idx = 0;
    bool game_over = false;
    int winner = 0;

    for (uint16_t *fb; (fb = vsync_swap());) {
        uint16_t keys = keys_curr();

        if (!game_over) {
            if (keys & (1<<6)) { if (left.y > 0) left.y -= PADDLE_SPEED; }
            if (keys & (1<<7)) { if (left.y < H-PADDLE_H) left.y += PADDLE_SPEED; }
            if (keys & (1<<0)) { if (right.y > 0) right.y -= PADDLE_SPEED; }
            if (keys & (1<<1)) { if (right.y < H-PADDLE_H) right.y += PADDLE_SPEED; }

            static uint16_t prev = 0; // for edge detection
            if ((keys & (1<<2)) && !(prev & (1<<2))) {
                warm_idx = (warm_idx+1)%NUM_WARM;
                palette[LEFT] = warm[warm_idx];
            }
            prev = keys;

            ball.x += ball.vx;
            ball.y += ball.vy;

            int bx = ball.x>>8;
            int by = ball.y>>8;

            if (by <=0 && ball.vy<0) ball.vy = -ball.vy;
            if (by >= H-BALL_SIZE && ball.vy>0) ball.vy = -ball.vy;

            int left_x  = 10;
            int right_x = W-10-PADDLE_W;

            if (bx <= left_x + PADDLE_W &&
                bx + BALL_SIZE >= left_x &&
                by + BALL_SIZE >= left.y && by <= left.y + PADDLE_H) {
                int offset = (by + BALL_SIZE/2) - (left.y + PADDLE_H/2);
                ball.x = (left_x + PADDLE_W)<<8;
                ball.vx = BALL_SPEED;
                ball.vy = offset*32;
            }
            if (bx + BALL_SIZE >= right_x &&
                bx <= right_x + PADDLE_W &&
                by + BALL_SIZE >= right.y && by <= right.y + PADDLE_H) {
                int offset = (by + BALL_SIZE/2) - (right.y + PADDLE_H/2);
                ball.x = (right_x - BALL_SIZE)<<8;
                ball.vx = -BALL_SPEED;
                ball.vy = offset*32;
            }

            if (bx < 0) {
                score2++;
                ball.x = (W/2)<<8;
                ball.y = (H/2)<<8;
                ball.vx = BALL_SPEED;
                ball.vy = 0;
                left.y = right.y = (H - PADDLE_H)/2;
            } else if (bx > W-BALL_SIZE) {
                score1++;
                ball.x = (W/2)<<8;
                ball.y = (H/2)<<8;
                ball.vx = -BALL_SPEED;
                ball.vy = 0;
                left.y = right.y = (H - PADDLE_H)/2;
            }

            int diff = score1 - score2;
            if ((score1 >=11 || score2 >=11) && (diff>=2 || diff<=-2)) {
                game_over = true;
                winner = diff > 0 ? 1 : 2;
            }
        }

        clear(fb, BG);
        int left_x  = 10;
        int right_x = W-10-PADDLE_W;
        fill_rect(fb, left_x,  left.y, PADDLE_W, PADDLE_H, LEFT);
        fill_rect(fb, right_x, right.y, PADDLE_W, PADDLE_H, RIGHT);
        fill_rect(fb, ball.x>>8, ball.y>>8, BALL_SIZE, BALL_SIZE, WHITE);
        draw_num(fb, 30,10, score1, LEFT);
        draw_num(fb, W-30-8*(score2>=10?2:1),10, score2, RIGHT);
        if (game_over) {
            char const *msg = winner==1?"P1 WINS!":"P2 WINS!";
            draw_str(fb, (W-8*7)/2, H/2-4, msg, winner==1?LEFT:RIGHT);
        }
    }
}
