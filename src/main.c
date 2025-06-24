#include <stdint.h>
#include "font.h"

#define REG_DISPCNT (*(volatile uint16_t*)0x4000000)
#define MODE3 3
#define BG2_ENABLE (1<<10)

#define VIDEO_BUFFER ((volatile uint16_t*)0x6000000)
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

static inline void drawPixel(int x, int y, uint16_t color) {
    VIDEO_BUFFER[y * SCREEN_WIDTH + x] = color;
}

static void drawChar(int x, int y, char ch, uint16_t color) {
    const uint8_t* glyph = font_get(ch);
    for(int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for(int col = 0; col < 8; ++col) {
            if(bits & (1 << (7 - col)))
                drawPixel(x + col, y + row, color);
        }
    }
}

static void drawString(int x, int y, const char* str, uint16_t color) {
    while(*str) {
        drawChar(x, y, *str++, color);
        x += 8;
    }
}

#define RGB15(r,g,b) ((r) | ((g)<<5) | ((b)<<10))

int main(void) {
    REG_DISPCNT = MODE3 | BG2_ENABLE;
    drawString(10, 10, "HELLO WORLD", RGB15(31,31,31));
    for(;;) { }
    return 0;
}
