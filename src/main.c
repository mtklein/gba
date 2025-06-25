#include <stdint.h>
#include "draw.h"

#define len(x) (int)(sizeof x / sizeof *x)

static uint16_t volatile *reg_keys = (uint16_t volatile*)0x04000130;

static _Accum const paddle_h     = 30,
                    paddle_w     = 4,
                    paddle_speed = 3,
                    ball_size    = 4,
                    ball_speed   = 1.5K;

typedef void draw_fn(void const *self, struct fb *fb, int winner);

struct paddle   { _Accum x,y,vx,vy; uint8_t color; uint8_t pad[3]; draw_fn *draw; };
struct ball     { _Accum x,y,vx,vy; uint8_t color; uint8_t pad[3]; draw_fn *draw; };
struct particle { _Accum x,y,vx,vy; uint8_t color; uint8_t pad[3]; draw_fn *draw; };

static void draw_paddle(void const *self, struct fb *fb, int winner) {
    struct paddle const *p = self;
    (void)winner;
    fill_rect(fb, p->color, p->x,p->y, paddle_w,paddle_h);
}

static void draw_ball(void const *self, struct fb *fb, int winner) {
    if (!winner) {
        struct ball const *b = self;
        fill_rect(fb, b->color, b->x,b->y, ball_size,ball_size);
    }
}

static void draw_particle(void const *self, struct fb *fb, int winner) {
    if (winner) {
        struct particle const *p = self;
        fill_rect(fb, p->color, p->x-1, p->y-1, 3,3);
    }
}

void main(void) {
    draw_init();

    struct rgb555 *color = palette;

    uint8_t const BG = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31,.g=31,.b=31};

    uint8_t const BALL = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=0, .g=0, .b=0};

    uint8_t const WARM = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31, .g= 0, .b= 0};
    *color++ = (struct rgb555){.r=25, .g= 0, .b=20};
    *color++ = (struct rgb555){.r=31, .g=20, .b= 0};
    *color++ = (struct rgb555){.r=25, .g=25, .b= 0};
    int const warm_color_mask = (int)(color - palette) - WARM - 1;

    uint8_t const COOL = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r= 0, .g= 0, .b=31};
    *color++ = (struct rgb555){.r= 0, .g=25, .b=25};
    *color++ = (struct rgb555){.r= 0, .g=31, .b= 0};
    *color++ = (struct rgb555){.r=10, .g=10, .b=20};
    int const cool_color_mask = (int)(color - palette) - COOL - 1;

    uint8_t const PARTICLE = (uint8_t)(color - palette);
    *color++ = (struct rgb555){.r=31, .g= 0, .b= 0};
    *color++ = (struct rgb555){.r=31, .g=31, .b= 0};
    *color++ = (struct rgb555){.r= 0, .g=31, .b= 0};
    *color++ = (struct rgb555){.r= 0, .g=31, .b=31};
    *color++ = (struct rgb555){.r= 0, .g= 0, .b=31};
    *color++ = (struct rgb555){.r=31, .g= 0, .b=31};
    *color++ = (struct rgb555){.r=31, .g=15, .b= 0};
    *color++ = (struct rgb555){.r=15, .g= 0, .b=31};
    int const particle_color_mask = (int)(color - palette) - PARTICLE - 1;

    clear(front, BG);
    clear(back , BG);

    struct paddle left  = {
        .x = 10,
        .y = (H - paddle_h)/2,
        .vx = 0,
        .vy = 0,
        .draw = draw_paddle,
        .color = WARM,
    };
    struct paddle right = {
        .x = W-10-paddle_w,
        .y = (H - paddle_h)/2,
        .vx = 0,
        .vy = 0,
        .draw = draw_paddle,
        .color = COOL,
    };
    struct ball ball = {
        .x  = W/2 - ball_size/2,
        .y  = H/2 - ball_size/2,
        .vx = -ball_speed,
        .vy = 0,
        .draw = draw_ball,
        .color = BALL,
    };

    struct particle particle[9];  // only 8 draw, but this makes for pretty color cycling
    int next_particle_color = 0;

    for (int i = 0; i < len(particle); i++) {
        struct particle *p = particle+i;
        p->x  = W/2;
        p->y  = H/2;
        p->vx = 0;
        p->vy = 0;
        p->draw  = draw_particle;
        p->color = (uint8_t)(PARTICLE + (++next_particle_color & particle_color_mask));
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
            left.color = (uint8_t)(WARM + (++warm & warm_color_mask));
        }
        if ((keys & (1<<3)) && !(held & (1<<3))) {
            right.color = (uint8_t)(COOL + (++cool & cool_color_mask));
        }

        left.x  += left.vx;
        left.y  += left.vy;
        right.x += right.vx;
        right.y += right.vy;
        if (left.y < 0)            left.y = 0;
        if (left.y > H - paddle_h) left.y = H - paddle_h;
        if (right.y < 0)           right.y = 0;
        if (right.y > H - paddle_h) right.y = H - paddle_h;

        for (int i = 0; i < len(particle); i++) {
            struct particle *p = particle+i;
            p->x += p->vx;
            p->y += p->vy;
            if (winner) {
                p->vy += 1/256.0K;
                p->color = (uint8_t)(PARTICLE + (++next_particle_color & particle_color_mask));
            }
        }
        if (!winner) {
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
            if (!winner && (score1 >= 11 || score2 >= 11) && (diff >= 2 || diff <= -2)) {
                winner = diff > 0 ? 1 : 2;
                for (int i = 0; i < len(particle); i++) {
                    struct particle *p = particle+i;
                    _Accum const s = 0.75K;
                    switch (i & 7) {
                        case 0:  p->vx = +s;  p->vy =  0;  break;
                        case 1:  p->vx = +s;  p->vy = -s; break;
                        case 2:  p->vx =  0;  p->vy = -s; break;
                        case 3:  p->vx = -s;  p->vy = -s; break;
                        case 4:  p->vx = -s;  p->vy =  0;  break;
                        case 5:  p->vx = -s;  p->vy = +s; break;
                        case 6:  p->vx =  0;  p->vy = +s; break;
                        default: p->vx = +s;  p->vy = +s; break;
                    }
                }
            }
        }

        clear(fb, BG);
        left.draw(&left, fb, winner);
        right.draw(&right, fb, winner);
        ball.draw(&ball, fb, winner);
        for (int i = 0; i < len(particle); i++) {
            struct particle const *p = particle+i;
            p->draw(p, fb, winner);
        }

        draw_num(fb,  left.color,                      30,10, score1);
        draw_num(fb, right.color, W-30-8*(score2>=10?2:1),10, score2);
        if (winner) {
            char const *msg = winner==1 ? "P1 WINS!" : "P2 WINS!";
            draw_str(fb, (winner==1 ? left.color : right.color), (W-8*7)/2, H/2-4, msg);
        }
    }
}
