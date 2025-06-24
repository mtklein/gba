#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t*)0x4000000)
#define MODE4 4
#define BG2_ENABLE (1<<10)
#define PALETTE ((volatile uint16_t*)0x5000000)
#define REG_VCOUNT (*(volatile uint16_t*)0x4000006)

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define COLOR_BLACK 0
#define COLOR_WHITE 1

#define FRONT_BUFFER ((volatile uint16_t*)0x6000000)
#define BACK_BUFFER  ((volatile uint16_t*)0x600A000)

static volatile uint16_t* videoBuffer = BACK_BUFFER;

static inline void flipPage(void) {
    if(REG_DISPCNT & (1<<4)) {
        REG_DISPCNT &= ~(1<<4);
        videoBuffer = BACK_BUFFER;
    } else {
        REG_DISPCNT |= (1<<4);
        videoBuffer = FRONT_BUFFER;
    }
}

static inline void waitForVBlank(void) {
    while(REG_VCOUNT >= 160);
    while(REG_VCOUNT < 160);
}

static inline void drawPixel(int x, int y, uint8_t color) {
    uint32_t idx;
    uint16_t cur;
    if(x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    idx = (uint32_t)y * (SCREEN_WIDTH/2u) + (uint32_t)(x >> 1);
    cur = videoBuffer[idx];
    if(x & 1) {
        uint16_t mask = (uint16_t)((uint16_t)color << 8);
        cur = (uint16_t)((cur & 0x00FFu) | mask);
    } else {
        uint16_t mask = (uint16_t)color;
        cur = (uint16_t)((cur & 0xFF00u) | mask);
    }
    videoBuffer[idx] = cur;
}

static void fillRect(int x, int y, int w, int h, uint8_t color) {
    for(int r = 0; r < h; ++r)
        for(int c = 0; c < w; ++c)
            drawPixel(x + c, y + r, color);
}

static void clearScreen(uint8_t color) {
    uint16_t fill = (uint16_t)(color | (color << 8));
    for(int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT / 2; ++i)
        videoBuffer[i] = fill;
}

typedef struct {
    int x, y;  // fixed point 8.8
    int vx, vy;
} Ball;

#define BALL_SIZE 8
#define BALL_SPEED 256

int main(void) {
    Ball ball;

    REG_DISPCNT = MODE4 | BG2_ENABLE;
    PALETTE[COLOR_BLACK] = 0;
    PALETTE[COLOR_WHITE] = 0x7FFF;

    videoBuffer = FRONT_BUFFER;
    clearScreen(COLOR_BLACK);
    videoBuffer = BACK_BUFFER;
    clearScreen(COLOR_BLACK);

    ball.x = (SCREEN_WIDTH/2 - BALL_SIZE/2) << 8;
    ball.y = (SCREEN_HEIGHT/2 - BALL_SIZE/2) << 8;
    ball.vx = BALL_SPEED;
    ball.vy = BALL_SPEED;

    while(1) {
        int bx;
        int by;

        ball.x += ball.vx;
        ball.y += ball.vy;

        bx = ball.x >> 8;
        by = ball.y >> 8;

        if(bx <= 0) {
            ball.vx = BALL_SPEED;
            ball.x = 0;
        } else if(bx >= SCREEN_WIDTH - BALL_SIZE) {
            ball.vx = -BALL_SPEED;
            ball.x = (SCREEN_WIDTH - BALL_SIZE) << 8;
        }

        if(by <= 0) {
            ball.vy = BALL_SPEED;
            ball.y = 0;
        } else if(by >= SCREEN_HEIGHT - BALL_SIZE) {
            ball.vy = -BALL_SPEED;
            ball.y = (SCREEN_HEIGHT - BALL_SIZE) << 8;
        }

        waitForVBlank();
        flipPage();
        clearScreen(COLOR_BLACK);
        fillRect(ball.x >> 8, ball.y >> 8, BALL_SIZE, BALL_SIZE, COLOR_WHITE);
    }
}

