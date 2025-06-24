#include <stdint.h>

#define REG_DISPCNT (*(uint16_t volatile*)0x4000000)
#define REG_VCOUNT  (*(uint16_t volatile*)0x4000006)
#define MODE4 4
#define BG2_ENABLE (1<<10)

#define W 240
#define H 160

static uint16_t* swap_buffers(uint16_t *front, uint16_t *back) {
    while (REG_VCOUNT >= H);
    while (REG_VCOUNT <  H);

    REG_DISPCNT ^= (1<<4);
    if (REG_DISPCNT & (1<<4)) {
        return front;
    } else {
        return back;
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

static inline void set_pixel(uint16_t *fb,
                             int x, int y, uint8_t color) {
    int const ix = y * (W/2) + x/2;
    union mode4_pair px = {fb[ix]};
    if (x & 1) {
        px.hi = color;
    } else {
        px.lo = color;
    }
    fb[ix] = px.both;
}

static void fill_rect(uint16_t *fb,
                      int l, int t, int w, int h, uint8_t color) {
    int const r = l+w,
              b = t+h;
    for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) {
        set_pixel(fb, x,y, color);
    }
}

static void clear(uint16_t *fb, uint8_t color) {
    union mode4_pair const fill = {.lo=color, .hi=color};
    for(int i = 0; i < W*H/2; i++) {
        fb[i] = fill.both;
    }
}

struct ball {
    int x,vx,  // All .8 fixed point
        y,vy;
};

int main(void) {
    REG_DISPCNT = MODE4 | BG2_ENABLE;

    uint16_t *palette = (uint16_t*)0x05000000,
             *front   = (uint16_t*)0x06000000,
             *back    = (uint16_t*)0x0600A000,
             *fb      = back;

    enum {BG,BALL};
    palette[  BG] = ((union rgb555){.r=31, .g=31, .b=31}).rgbx;
    palette[BALL] = ((union rgb555){.r= 0, .g=15, .b=31}).rgbx;

    clear(front, BG);
    clear( back, BG);

    int const size = 6, speed = 512;

    struct ball ball = {
        .x = (W/2 - size/2) << 8, .vx = speed,
        .y = (H/2 - size/2) << 8, .vy = speed,
    };

    while (1) {
        fb = swap_buffers(front, back);

        ball.x += ball.vx;
        ball.y += ball.vy;

        int const ix = ball.x >> 8,
                  iy = ball.y >> 8;

        clear    (fb, BG);
        fill_rect(fb, ix,iy, size,size, BALL);

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
    }
}

