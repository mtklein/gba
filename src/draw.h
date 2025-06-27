#pragma once
#include <stdint.h>

static int const W = 240,
                 H = 160;

extern struct rgb555 {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t x : 1;
} *palette;

struct fb {
    uint16_t lo : 8;
    uint16_t hi : 8;
};

void draw_init(void);

struct fb* vsync_swap(void);

void     clear(struct fb*, uint8_t color);
void set_pixel(struct fb*, uint8_t color, int x, int y);
void fill_rect(struct fb*, uint8_t color, int x, int y, int w, int h);
void draw_char(struct fb*, uint8_t color, int x, int y, char);
void draw_str (struct fb*, uint8_t color, int x, int y, char const[]);
void draw_num (struct fb*, uint8_t color, int x, int y, int);

/* Step 4: basic sprite support */
struct oam_entry {
    uint16_t attr0;
    uint16_t attr1;
    uint16_t attr2;
    uint16_t pad;
};

void sprite_init(void);
void sprite_flush(void);
