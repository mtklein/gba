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

struct rgb555 {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t x : 1;
};
static struct rgb555 *palette = (struct rgb555*)0x05000000;

struct fb {  // mode 4
    uint16_t lo : 8;
    uint16_t hi : 8;
};
static struct fb *front = (struct fb*)0x06000000,
                 *back  = (struct fb*)0x0600A000;

static struct fb* vsync_swap(void) {
    while (*reg_vcount >= H);
    while (*reg_vcount <  H);

    *reg_dispcnt ^= (1<<4);
    if (*reg_dispcnt & (1<<4)) {
        return front;
    } else {
        return back;
    }
}

static inline void set_pixel(struct fb *fb, int x, int y, uint8_t color) {
    if ((unsigned)x < W && (unsigned)y < H) {
        struct fb *px = fb + y * (W/2) + x/2;
        if (x & 1) {
            px->hi = color;
        } else {
            px->lo = color;
        }
    }
}

static void fill_rect(struct fb *fb, int l, int t, int w, int h, uint8_t color) {
    int const r = l+w,
              b = t+h;
    for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) {
        set_pixel(fb, x,y, color);
    }
}

static void clear(struct fb *fb, uint8_t color) {
    struct fb const src = {.lo=color, .hi=color};
    dma[3].src = &src;
    dma[3].dst = fb;
    dma[3].cnt = (W*H/2) | (2<<23) | (1u<<31);
}

static void draw_char(struct fb *fb, int x, int y, char ch, uint8_t color) {
    uint8_t const *glyph = font_get(ch);
    for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
        if (glyph[r] & (1 << (7 - c))) {
            set_pixel(fb, x+c, y+r, color);
        }
    }
}

static void draw_str(struct fb *fb, int x, int y, char const *s, uint8_t color) {
    for (; *s; x += 8) {
        draw_char(fb, x, y, *s++, color);
    }
}


static void draw_num(struct fb *fb, int x, int y, int v, uint8_t color) {
    if (v >= 10) {
        int const tens = (v * 103) >> 10;
        draw_char(fb, x, y, (char)('0' + tens), color);
        x += 8;
        v -= 10*tens;
    }
    draw_char(fb, x, y, (char)('0' + v), color);
}

static _Accum const paddle_h     = 30,
                    paddle_w     = 4,
                    paddle_speed = 3,
                    ball_size    = 4,
                    ball_speed   = 1.5K;

struct paddle   { _Accum x,y,vx,vy; };
struct ball     { _Accum x,y,vx,vy; };
struct particle { _Accum x,y,vx,vy; int color; };

static struct rgb555 const warm_color[] = {
    {.r=31, .g= 0, .b= 0},
    {.r=25, .g= 0, .b=20},
    {.r=31, .g=20, .b= 0},
    {.r=25, .g=25, .b= 0},
};
static struct rgb555 const cool_color[] = {
    {.r= 0, .g= 0, .b=31},
    {.r= 0, .g=25, .b=25},
    {.r= 0, .g=31, .b= 0},
    {.r=10, .g=10, .b=20},
};
static struct rgb555 const particle_color[] = {
    {.r=31, .g= 0, .b= 0},
    {.r=31, .g=31, .b= 0},
    {.r= 0, .g=31, .b= 0},
    {.r= 0, .g=31, .b=31},
    {.r= 0, .g= 0, .b=31},
    {.r=31, .g= 0, .b=31},
    {.r=31, .g=15, .b= 0},
    {.r=15, .g= 0, .b=31},
};

void main(void) {
    *reg_dispcnt = 4 | (1<<10);

    enum {BG,BALL,LEFT,RIGHT,PARTICLES};
    palette[BG   ] = (struct rgb555){.r=31,.g=31,.b=31};
    palette[BALL ] = (struct rgb555){.r=0, .g=0, .b=0 };
    palette[LEFT ] = *warm_color;
    palette[RIGHT] = *cool_color;
    for (int i = 0; i < len(particle_color); i++) {
        palette[PARTICLES + i] = particle_color[i];
    }
    clear(front, BG);
    clear(back , BG);

    struct paddle left  = {.x=           10, .y = (H - paddle_h)/2, .vx=0, .vy=0};
    struct paddle right = {.x=W-10-paddle_w, .y = (H - paddle_h)/2, .vx=0, .vy=0};
    struct ball ball = {
        .x  = W/2 - ball_size/2,
        .y  = H/2 - ball_size/2,
        .vx = -ball_speed,
        .vy = 0,
    };

    struct particle particle[9];  // only 8 draw, but this makes for pretty color cycling
    int next_particle_color = 0;

    for (int i = 0; i < len(particle); i++) {
        struct particle *p = particle+i;
        p->x = W/2;
        p->y = H/2;
        _Accum const s = 0.75K;
        switch (i & 7) {
            case 0:  p->vx = +s;  p->vy =  0;  break;
            case 1:  p->vx = +s;  p->vy = -s;  break;
            case 2:  p->vx =  0;  p->vy = -s;  break;
            case 3:  p->vx = -s;  p->vy = -s;  break;
            case 4:  p->vx = -s;  p->vy =  0;  break;
            case 5:  p->vx = -s;  p->vy = +s;  break;
            case 6:  p->vx =  0;  p->vy = +s;  break;
            default: p->vx = +s;  p->vy = +s;  break;
        }
        p->color = PARTICLES + (++next_particle_color % len(particle_color));
    }

    int score1 = 0,
        score2 = 0,
        winner = 0,
          warm = 0,
          cool = 0;

    uint16_t keys = 0, held;
    for (struct fb *fb; (fb = vsync_swap());) {
        held = keys;
        keys = ~*reg_keys;

        left.vy = 0;
        right.vy = 0;
        if (keys & (1<<6)) { if ( left.y >          0)  left.vy = -paddle_speed; }
        if (keys & (1<<7)) { if ( left.y < H-paddle_h)  left.vy = +paddle_speed; }
        if (keys & (1<<0)) { if (right.y >          0) right.vy = -paddle_speed; }
        if (keys & (1<<1)) { if (right.y < H-paddle_h) right.vy = +paddle_speed; }

        if ((keys & (1<<2)) && !(held & (1<<2))) {
            palette[LEFT]  = warm_color[++warm % len(warm_color)];
        }
        if ((keys & (1<<3)) && !(held & (1<<3))) {
            palette[RIGHT] = cool_color[++cool % len(cool_color)];
        }

        left.x  += left.vx;
        left.y  += left.vy;
        right.x += right.vx;
        right.y += right.vy;
        if (left.y < 0)            left.y = 0;
        if (left.y > H - paddle_h) left.y = H - paddle_h;
        if (right.y < 0)           right.y = 0;
        if (right.y > H - paddle_h) right.y = H - paddle_h;

        if (winner) {
            for (int i = 0; i < len(particle); i++) {
                struct particle *p = particle+i;
                p->x  += p->vx;
                p->y  += p->vy;
                p->vy += 1/256.0K;
                p->color = PARTICLES + (++next_particle_color % len(particle_color));
            }
        } else {
            ball.x += ball.vx;
            ball.y += ball.vy;

            int const bx = ball.x,
                      by = ball.y;

            if (by <= 0           && ball.vy < 0) { ball.vy = -ball.vy; }
            if (by >= H-ball_size && ball.vy > 0) { ball.vy = -ball.vy; }

            if (1 && bx + ball_size >= left.x
                  && by + ball_size >= left.y
                  && bx <= left.x + paddle_w
                  && by <= left.y + paddle_h) {
                int const offset = (by + ball_size/2) - (left.y + paddle_h/2);
                ball.x  = left.x + paddle_w;
                ball.vx = +ball_speed;
                ball.vy = offset >> 3;
            }
            if (1 && bx + ball_size >= right.x
                  && by + ball_size >= right.y
                  && bx <= right.x + paddle_w
                  && by <= right.y + paddle_h) {
                int const offset = (by + ball_size/2) - (right.y + paddle_h/2);
                ball.x  = right.x - ball_size;
                ball.vx = -ball_speed;
                ball.vy = offset >> 3;
            }

            if (bx < 0) {
                score2++;
                ball.x  = W/2;
                ball.y  = H/2;
                ball.vx = +ball_speed;
                ball.vy = 0;
            }
            if (bx > W - ball_size) {
                score1++;
                ball.x  = W/2;
                ball.y  = H/2;
                ball.vx = -ball_speed;
                ball.vy = 0;
            }

            int diff = score1 - score2;
            if ((score1 >= 11 || score2 >= 11) && (diff >= 2 || diff <= -2)) {
                winner = diff > 0 ? 1 : 2;
            }
        }

        clear(fb, BG);
        fill_rect(fb,  left.x, left.y, paddle_w,paddle_h,  LEFT);
        fill_rect(fb, right.x,right.y, paddle_w,paddle_h, RIGHT);
        if (!winner) {
            fill_rect(fb, ball.x,ball.y, ball_size,ball_size,  BALL);
        }
        draw_num(fb,                      30,10, score1,  LEFT);
        draw_num(fb, W-30-8*(score2>=10?2:1),10, score2, RIGHT);

        if (winner) {
            for (int i = 0; i < len(particle); i++) {
                struct particle const *p = particle+i;
                int const x = p->x,
                          y = p->y;
                fill_rect(fb, x-1,y-1, 3,3, (uint8_t)p->color);
            }
            char const *msg = winner==1 ? "P1 WINS!" : "P2 WINS!";
            draw_str(fb, (W-8*7)/2, H/2-4, msg, winner==1 ? LEFT : RIGHT);
        }
    }
}
