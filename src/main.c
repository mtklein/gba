#include <stdint.h>
#include "draw.h"

static uint16_t volatile *reg_keys = (uint16_t volatile*)0x04000130;

void main(void) __attribute__((noreturn));
void main(void) {
    draw_init();

    /* Background and sprite palettes */
    palette[0] = (struct rgb555){31,31,31,0}; /* white */
    palette[1] = (struct rgb555){0,0,0,0};   /* black */

    obj_palette[0]  = palette[0];
    obj_palette[1]  = (struct rgb555){31,0,0,0};  /* left paddle */
    obj_palette[17] = (struct rgb555){0,0,31,0};  /* right paddle (bank 1 index 1) */

    sprite_init();

    int left_y  = (H-16)/2;
    int right_y = (H-16)/2;

    for (;;) {
        uint16_t keys = ~*reg_keys;
        if (keys & (1<<6)) { left_y  -= 2; }
        if (keys & (1<<7)) { left_y  += 2; }
        if (keys & (1<<0)) { right_y -= 2; }
        if (keys & (1<<1)) { right_y += 2; }

        if (left_y  < 0)     left_y  = 0;
        if (left_y  > H-16)  left_y  = H-16;
        if (right_y < 0)     right_y = 0;
        if (right_y > H-16)  right_y = H-16;

        shadow_oam[0].attr0 = (left_y & 0xFF) | 0x4000; /* tall */
        shadow_oam[0].attr1 = 10 & 0x1FF;               /* x */
        shadow_oam[0].attr2 = 0 | (0<<12);              /* tile 0, palbank 0 */

        shadow_oam[1].attr0 = (right_y & 0xFF) | 0x4000;
        shadow_oam[1].attr1 = (W-10-8) & 0x1FF;
        shadow_oam[1].attr2 = 0 | (1<<12);              /* palbank 1 */

        vsync_swap();
    }
}
