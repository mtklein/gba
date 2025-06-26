#include <stdint.h>
#include "draw.h"

#define len(x) (int)(sizeof x / sizeof *x)

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint16_t volatile *reg_keys = (uint16_t volatile*)0x04000130;

static _Accum const paddle_h  = 30,
                    paddle_w  = 4,
                    ball_size = 5;

struct entity;
typedef void draw_fn(struct entity const*, struct fb *fb, int winner);

enum {PADDLE,BALL,PARTICLE};
struct entity {
    char const type;
    uint8_t    color;
    short      padding;
    _Accum x,y,vx,vy,ax,ay;
    draw_fn *draw;
};

static void draw_paddle(struct entity const *p, struct fb *fb, int winner) {
    (void)winner;
    fill_rect(fb, p->color, p->x,p->y, paddle_w,paddle_h);
}

static void draw_ball(struct entity const *b, struct fb *fb, int winner) {
    if (!winner) {
        fill_rect(fb, b->color, b->x,b->y, ball_size,ball_size);
    }
}

static void draw_particle(struct entity const *p, struct fb *fb, int winner) {
    if (winner) {
        fill_rect(fb, p->color, p->x-1, p->y-1, 3,3);
    }
}

void main(void) {
    draw_init();

    struct rgb555 *color = palette;

    uint8_t const WHITE = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31,.g=31,.b=31};

    uint8_t const BLACK = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=0, .g=0, .b=0};

    uint8_t const WARM = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31, .g= 0, .b= 0};
    *color++ = (struct rgb555){.r=25, .g= 0, .b=20};
    *color++ = (struct rgb555){.r=31, .g=20, .b= 0};
    *color++ = (struct rgb555){.r=25, .g=25, .b= 0};
    int const wrap_warm = (int)(color - palette) - WARM - 1;

    uint8_t const COOL = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r= 0, .g= 0, .b=31};
    *color++ = (struct rgb555){.r= 0, .g=25, .b=25};
    *color++ = (struct rgb555){.r= 0, .g=31, .b= 0};
    *color++ = (struct rgb555){.r=10, .g=10, .b=20};
    int const wrap_cool = (int)(color - palette) - COOL - 1;

    uint8_t const PARTICLE_COLOR = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31, .g= 0, .b= 0};
    *color++ = (struct rgb555){.r=31, .g=31, .b= 0};
    *color++ = (struct rgb555){.r= 0, .g=31, .b= 0};
    *color++ = (struct rgb555){.r= 0, .g=31, .b=31};
    *color++ = (struct rgb555){.r= 0, .g= 0, .b=31};
    *color++ = (struct rgb555){.r=31, .g= 0, .b=31};
    *color++ = (struct rgb555){.r=31, .g=15, .b= 0};
    *color++ = (struct rgb555){.r=15, .g= 0, .b=31};
    int const wrap_particle = (int)(color - palette) - PARTICLE_COLOR - 1;

    struct entity e[] = {
        {
            .type  = PADDLE,
            .x     = 10,
            .y     = (H - paddle_h)/2,
            .draw  = draw_paddle,
            .color = WARM,
        },
        {
            .type  = PADDLE,
            .x     = W-10-paddle_w,
            .y     = (H - paddle_h)/2,
            .draw  = draw_paddle,
            .color = COOL,
        },
        {
            .type  = BALL,
            .x     = W/2 - ball_size/2,
            .y     = H/2 - ball_size/2,
            .vx    = -1.5K,
            .ay    = 1/256.0K,
            .draw  = draw_ball,
            .color = BLACK,
        },
        {
            .type  = BALL,
            .x     = W/2 - ball_size/2,
            .y     = H/2 - ball_size/2,
            .vx    = +1.5K,
            .ay    = 1/256.0K,
            .draw  = draw_ball,
            .color = BLACK,
        },
        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},

        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},

        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},
        {.type=PARTICLE, .x=W/2, .y=H/2},
    };

    struct entity *left  = e+0,
                  *right = e+1;

    int score1 = 0,
        score2 = 0,
        winner = 0;

    int next_warm_color     = 0,
        next_cool_color     = 0,
        next_particle_color = 0;

    uint16_t keys = 0, held;
    for (struct fb *fb; (fb = vsync_swap());) {
        held = keys;
        keys = ~*reg_keys;

        left->vy = right->vy = 0;
        if (keys & (1<<6)) {  left->vy = -3; }
        if (keys & (1<<7)) {  left->vy = +3; }
        if (keys & (1<<0)) { right->vy = -3; }
        if (keys & (1<<1)) { right->vy = +3; }

        if ((keys & (1<<2)) && !(held & (1<<2))) {
            left ->color = (uint8_t)(WARM + (++next_warm_color & wrap_warm));
        }
        if ((keys & (1<<3)) && !(held & (1<<3))) {
            right->color = (uint8_t)(COOL + (++next_cool_color & wrap_cool));
        }
        for (int i = 0; i < len(e); i++) {
            if (e[i].type == PARTICLE) {
                e[i].color = (uint8_t)(PARTICLE_COLOR + (++next_particle_color & wrap_particle));
            }
        }

        for (int i = 0; i < len(e); i++) {
            e[i].x += e[i].vx;
            e[i].y += e[i].vy;

            e[i].vx += e[i].ax;
            e[i].vy += e[i].ay;
        }

        left ->y = clamp( left->y, 0, H-paddle_h);
        right->y = clamp(right->y, 0, H-paddle_h);

        for (int i = 0; i < len(e); i++) {
            if (e[i].type != BALL) {
                continue;
            }
            struct entity *ball = e+i;
            int const bx = ball->x,
                      by = ball->y;

            if (by <= 0           && ball->vy < 0) { ball->vy = -ball->vy; }
            if (by >= H-ball_size && ball->vy > 0) { ball->vy = -ball->vy; }

            if (bx < 0) {
                score2++;
                ball->x  = W/2;
                ball->y  = H/2;
                ball->vx = -ball->vx;
                ball->vy = 0;
            }
            if (bx > W - ball_size) {
                score1++;
                ball->x  = W/2;
                ball->y  = H/2;
                ball->vx = -ball->vx;
                ball->vy = 0;
            }

            if (1 && left->x - ball_size <= bx && bx <= left->x + paddle_w
                  && left->y - ball_size <= by && by <= left->y + paddle_h) {
                int const offset = (by + ball_size/2) - (left->y + paddle_h/2);
                ball->x  = left->x + paddle_w;
                ball->vx = -ball->vx;
                ball->vy = (_Accum)offset >> 3;
            }
            if (1 && right->x - ball_size <= bx && bx <= right->x + paddle_w
                  && right->y - ball_size <= by && by <= right->y + paddle_h) {
                int const offset = (by + ball_size/2) - (right->y + paddle_h/2);
                ball->x  = right->x - ball_size;
                ball->vx = -ball->vx;
                ball->vy = (_Accum)offset >> 3;
            }
        }

        int diff = score1 - score2;
        if (winner == 0 && (score1 >= 11 || score2 >= 11) && (diff >= 2 || diff <= -2)) {
            winner = diff > 0 ? 1 : 2;

            for (int i = 0; i < len(e); i++) {
                switch (e[i].type) {
                    case PADDLE:
                        break;

                    case BALL:
                        e[i].draw = 0;
                        e[i].vx = e[i].vy = e[i].ax = e[i].ay = 0;
                        break;

                    case PARTICLE:
                        e[i].draw = draw_particle;
                        _Accum const s = 0.75K;
                        switch (i & 7) {
                            case 0:  e[i].vx = +s;  e[i].vy =  0; break;
                            case 1:  e[i].vx = +s;  e[i].vy = -s; break;
                            case 2:  e[i].vx =  0;  e[i].vy = -s; break;
                            case 3:  e[i].vx = -s;  e[i].vy = -s; break;
                            case 4:  e[i].vx = -s;  e[i].vy =  0; break;
                            case 5:  e[i].vx = -s;  e[i].vy = +s; break;
                            case 6:  e[i].vx =  0;  e[i].vy = +s; break;
                            case 7: e[i].vx = +s;  e[i].vy = +s; break;
                        }
                        e[i].ay = 1/256.0K;
                        break;
                }
            }
        }

        clear(fb, WHITE);
        for (int i = 0; i < len(e); i++) {
            if (e[i].draw) {
                e[i].draw(e+i, fb, winner);
            }
        }
        draw_num(fb,  left->color,                      30,10, score1);
        draw_num(fb, right->color, W-30-8*(score2>=10?2:1),10, score2);
        if (winner) {
            draw_str(fb, winner==1 ? left->color : right->color, (W-8*7)/2, H/2-4
                       , winner==1 ? "P1 WINS!" : "P2 WINS!");
        }
    }
}
