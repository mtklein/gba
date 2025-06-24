#include <stdint.h>

static int const W = 240,
                 H = 160;

struct DMA {
    void const *src;
    void       *dst;
    uint32_t    cnt;
};
static struct DMA volatile *dma  = (struct DMA volatile*)0x040000B0;

static uint16_t volatile *reg_dispcnt = (uint16_t volatile*)0x04000000,
                         *reg_vcount  = (uint16_t volatile*)0x04000006;

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

struct ball {
    int x,vx,  // All .8 fixed point
        y,vy;
};

void main(void) {
    *reg_dispcnt = 4 | (1<<10);

    enum {BG,BALL};
    palette[  BG] = ((union rgb555){.r=31, .g=31, .b=31}).rgbx;
    palette[BALL] = ((union rgb555){.r= 0, .g=15, .b=31}).rgbx;

    clear(front_fb, BG);
    clear( back_fb, BG);

    int const size =   6,
             speed = 384;

    struct ball ball = {
        .x = (W/2 - size/2) << 8, .vx = speed,
        .y = (H/2 - size/2) << 8, .vy = speed,
    };

    for (uint16_t *fb; (fb = vsync_swap());) {
        ball.x += ball.vx;
        ball.y += ball.vy;

        int const ix = ball.x >> 8,
                  iy = ball.y >> 8;

        if (ix <= 0) {
            ball. x = 0;
            ball.vx = +speed;
        } else if (ix >= W - size) {
            ball. x = (W - size) << 8;
            ball.vx = -speed;
        }

        if (iy <= 0) {
            ball. y = 0;
            ball.vy = +speed;
        } else if (iy >= H - size) {
            ball. y = (H - size) << 8;
            ball.vy = -speed;
        }

        clear    (fb, BG);
        fill_rect(fb, ball.x>>8,ball.y>>8, size,size, BALL);
    }
}

