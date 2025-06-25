#include <stdint.h>
#include "font.h"

#define len(x) (int)(sizeof x / sizeof *x)

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

static void draw_char(uint16_t *fb, int x, int y, char ch, uint8_t color) {
    uint8_t const *glyph = font_get(ch);
    for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
        if (glyph[r] & (1 << (7 - c))) {
            set_pixel(fb, x+c, y+r, color);
        }
    }
}

static void draw_str(uint16_t *fb, int x, int y, char const *s, uint8_t color) {
    for (; *s; x += 8) {
        draw_char(fb, x, y, *s++, color);
    }
}


static void draw_num(uint16_t *fb, int x, int y, int v, uint8_t color) {
    if (v >= 10) {
        int const tens = (v * 103) >> 10;
        draw_char(fb, x, y, (char)('0' + tens), color);
        x += 8;
        v -= 10*tens;
    }
    draw_char(fb, x, y, (char)('0' + v), color);
}


static int const paddle_h     = 30,
                 paddle_w     = 4,
                 paddle_speed = 3,
                 ball_size    = 4,
                 ball_speed   = 384;

struct paddle { int const x; int y; };  // integer pixels
struct ball { int x,y,vx,vy; };         // .8 fixed point pixels

static union rgb555 const warm_color[] = {
    {.r=31, .g= 0, .b= 0},
    {.r=25, .g= 0, .b=20},
    {.r=31, .g=20, .b= 0},
    {.r=25, .g=25, .b= 0},
};
static union rgb555 const cool_color[] = {
    {.r= 0, .g= 0, .b=31},
    {.r= 0, .g=25, .b=25},
    {.r= 0, .g=31, .b= 0},
    {.r=10, .g=10, .b=20},
};

struct particle { int x, y, vx, vy, life, color; }; // .8 fixed pixels

static union rgb555 const fire_color[] = {
    {.r=31, .g= 0, .b= 0},
    {.r=31, .g=31, .b= 0},
    {.r= 0, .g=31, .b= 0},
    {.r= 0, .g=31, .b=31},
    {.r= 0, .g= 0, .b=31},
    {.r=31, .g= 0, .b=31},
    {.r=31, .g=15, .b= 0},
    {.r=15, .g= 0, .b=31},
};

enum { FIRE_START = 4, NUM_FIRE_COLORS = len(fire_color) };
#define N_PARTICLES 32
static struct particle particles[N_PARTICLES];
static int fireworks = 0;

static void firework_start(void) {
    for (int i = 0; i < N_PARTICLES; i++) {
        struct particle *p = &particles[i];
        p->x = (W/2) << 8;
        p->y = (H/2) << 8;
        int const s = 192;
        switch (i & 7) {
            case 0:  p->vx = +s;  p->vy = 0;   break;
            case 1:  p->vx = +s;  p->vy = -s;  break;
            case 2:  p->vx = 0;   p->vy = -s;  break;
            case 3:  p->vx = -s;  p->vy = -s;  break;
            case 4:  p->vx = -s;  p->vy = 0;   break;
            case 5:  p->vx = -s;  p->vy = +s;  break;
            case 6:  p->vx = 0;   p->vy = +s;  break;
            default: p->vx = +s;  p->vy = +s;  break;
        }
        p->life = 60;
        p->color = FIRE_START + i % NUM_FIRE_COLORS;
    }
    fireworks = 1;
}

static void firework_update(void) {
    for (int i = 0; i < N_PARTICLES; i++) {
        struct particle *p = &particles[i];
        if (p->life <= 0) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 8;
        p->life--;
    }
}

static void firework_draw(uint16_t *fb) {
    for (int i = 0; i < N_PARTICLES; i++) {
        struct particle const *p = &particles[i];
        if (p->life <= 0) continue;
        int const x = p->x >> 8;
        int const y = p->y >> 8;
        if ((unsigned)x < W && (unsigned)y < H) {
            set_pixel(fb, x, y, (uint8_t)p->color);
            if ((unsigned)(x+1) < W) set_pixel(fb, x+1, y, (uint8_t)p->color);
            if ((unsigned)(y+1) < H) set_pixel(fb, x, y+1, (uint8_t)p->color);
            if ((unsigned)(x+1) < W && (unsigned)(y+1) < H) set_pixel(fb, x+1, y+1, (uint8_t)p->color);
        }
    }
}

void main(void) {
    *reg_dispcnt = 4 | (1<<10);

    enum {BG,BALL,LEFT,RIGHT};
    palette[BG   ] = ((union rgb555){.r=31,.g=31,.b=31}).rgbx;
    palette[BALL ] = ((union rgb555){.r=0, .g=0, .b=0 }).rgbx;
    palette[LEFT ] = warm_color->rgbx;
    palette[RIGHT] = cool_color->rgbx;
    for (int i = 0; i < NUM_FIRE_COLORS; i++) {
        palette[FIRE_START + i] = fire_color[i].rgbx;
    }

    clear(front_fb, BG);
    clear( back_fb, BG);

    struct paddle left  = {.x=           10, .y = (H - paddle_h)/2};
    struct paddle right = {.x=W-10-paddle_w, .y = (H - paddle_h)/2};
    struct ball ball = {
        .x  = (W/2 - ball_size/2) << 8,
        .y  = (H/2 - ball_size/2) << 8,
        .vx = -ball_speed,
        .vy = 0,
    };

    int score1 = 0,
        score2 = 0,
        winner = 0,
          warm = 0,
          cool = 0;

    uint16_t keys = 0, held;
    for (uint16_t *fb; (fb = vsync_swap());) {
        held = keys;
        keys = ~*reg_keys;

        if (!winner) {
            if (keys & (1<<6)) { if ( left.y >          0)  left.y -= paddle_speed; }
            if (keys & (1<<7)) { if ( left.y < H-paddle_h)  left.y += paddle_speed; }
            if (keys & (1<<0)) { if (right.y >          0) right.y -= paddle_speed; }
            if (keys & (1<<1)) { if (right.y < H-paddle_h) right.y += paddle_speed; }

            if ((keys & (1<<2)) && !(held & (1<<2))) {
                palette[LEFT]  = warm_color[++warm % len(warm_color)].rgbx;
            }
            if ((keys & (1<<3)) && !(held & (1<<3))) {
                palette[RIGHT] = cool_color[++cool % len(cool_color)].rgbx;
            }

            ball.x += ball.vx;
            ball.y += ball.vy;

            int const bx = ball.x >> 8,
                      by = ball.y >> 8;

            if (by <= 0           && ball.vy < 0) { ball.vy = -ball.vy; }
            if (by >= H-ball_size && ball.vy > 0) { ball.vy = -ball.vy; }

            if (1 && bx + ball_size >= left.x
                  && by + ball_size >= left.y
                  && bx <= left.x + paddle_w
                  && by <= left.y + paddle_h) {
                int const offset = (by + ball_size/2) - (left.y + paddle_h/2);
                ball.x  = (left.x + paddle_w) << 8;
                ball.vx = +ball_speed;
                ball.vy = offset*32;
            }
            if (1 && bx + ball_size >= right.x
                  && by + ball_size >= right.y
                  && bx <= right.x + paddle_w
                  && by <= right.y + paddle_h) {
                int const offset = (by + ball_size/2) - (right.y + paddle_h/2);
                ball.x  = (right.x - ball_size) << 8;
                ball.vx = -ball_speed;
                ball.vy = offset*32;
            }

            if (bx < 0) {
                score2++;
                ball.x  = (W/2)<<8;
                ball.y  = (H/2)<<8;
                ball.vx = +ball_speed;
                ball.vy = 0;
            }
            if (bx > W - ball_size) {
                score1++;
                ball.x  = (W/2)<<8;
                ball.y  = (H/2)<<8;
                ball.vx = -ball_speed;
                ball.vy = 0;
            }

            int diff = score1 - score2;
            if ((score1 >= 11 || score2 >= 11) && (diff >= 2 || diff <= -2)) {
                winner = diff > 0 ? 1 : 2;
            }
        } else {
            if (!fireworks) firework_start();
            firework_update();
        }

        clear(fb, BG);
        fill_rect(fb,  left.x   ,  left.y   ,  paddle_w,  paddle_h,  LEFT);
        fill_rect(fb, right.x   , right.y   ,  paddle_w,  paddle_h, RIGHT);
        fill_rect(fb,  ball.x>>8,  ball.y>>8, ball_size, ball_size,  BALL);
        draw_num(fb,                      30,10, score1, LEFT);
        draw_num(fb, W-30-8*(score2>=10?2:1),10, score2, RIGHT);
        if (winner) {
            char const *msg = winner==1 ? "P1 WINS!" : "P2 WINS!";
            draw_str(fb, (W-8*7)/2, H/2-4, msg, winner==1 ? LEFT : RIGHT);
            firework_draw(fb);
        }
    }
}
