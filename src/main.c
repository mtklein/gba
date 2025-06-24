#include <stdint.h>
#include <stdbool.h>
#include "font.h"

#define REG_DISPCNT (*(volatile uint16_t*)0x4000000)
#define MODE4 4
#define BG2_ENABLE (1<<10)
#define PALETTE ((volatile uint16_t*)0x5000000)

#define REG_VCOUNT (*(volatile uint16_t*)0x4000006)
#define REG_KEYINPUT (*(volatile uint16_t*)0x4000130)

#define KEY_A      (1<<0)
#define KEY_B      (1<<1)
#define KEY_SELECT (1<<2)
#define KEY_START  (1<<3)
#define KEY_UP     (1<<6)
#define KEY_DOWN   (1<<7)

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define COLOR_BLACK 0
#define COLOR_WHITE 1
#define COLOR_LEFT  2
#define COLOR_RIGHT 3

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

static inline uint16_t keysCurrent(void) {
    return ~REG_KEYINPUT & 0x03FF;
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
    for(int r=0;r<h;r++)
        for(int c=0;c<w;c++)
            drawPixel(x+c, y+r, color);
}

static void clearScreen(uint8_t color) {
    uint16_t fill = (uint16_t)((uint16_t)color | ((uint16_t)color << 8));
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT/2;i++) {
        videoBuffer[i] = fill;
    }
}

static void drawChar(int x, int y, char ch, uint8_t color) {
    const uint8_t* glyph = font_get(ch);
    for(int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for(int col = 0; col < 8; ++col) {
            if(bits & (1 << (7 - col)))
                drawPixel(x + col, y + row, color);
        }
    }
}

static void drawString(int x, int y, const char* str, uint8_t color) {
    while(*str) {
        drawChar(x, y, *str++, color);
        x += 8;
    }
}

static void drawNumber(int x, int y, int val, uint8_t color) {
    char tens = (char)('0' + (val/10)%10);
    char ones = (char)('0' + (val%10));
    if(val >= 10) {
        drawChar(x, y, tens, color);
        x += 8;
    }
    drawChar(x, y, ones, color);
}

#define RGB15(r,g,b) ((r) | ((g)<<5) | ((b)<<10))

#define PADDLE_HEIGHT 30
#define PADDLE_WIDTH 4
#define BALL_SIZE 4
#define PADDLE_SPEED 3
#define BALL_SPEED 768

typedef struct { int y; } Paddle;

typedef struct {
    int x, y;   // fixed point (8.8)
    int vx, vy;
} Ball;

static const uint16_t warmColors[] = {
    RGB15(31,0,0), RGB15(31,10,0), RGB15(31,20,0), RGB15(31,31,0)
};
static const uint16_t coolColors[] = {
    RGB15(0,0,31), RGB15(0,31,31), RGB15(0,31,0), RGB15(10,10,31)
};
static const int NUM_WARM = sizeof(warmColors)/2;
static const int NUM_COOL = sizeof(coolColors)/2;

static uint32_t rng = 1;
static uint32_t rand32(void) {
    rng = rng*1664525 + 1013904223;
    return rng;
}

typedef struct {
    int x, y;   // fixed 8.8
    int vx, vy;
    int life;
    int color;
} Particle;

#define MAX_PARTICLES 40
static Particle particles[MAX_PARTICLES];

static const int8_t dirs[8][2] = {
    {-2,-2},{-2,0},{-2,2},{0,-2},{0,2},{2,-2},{2,0},{2,2}
};

static void spawnFirework(int color) {
    int baseX = rand32()%SCREEN_WIDTH;
    int baseY = rand32()%SCREEN_HEIGHT;
    for(int d=0; d<8; d++) {
        for(int i=0;i<MAX_PARTICLES;i++) if(particles[i].life<=0) {
            particles[i].x = baseX<<8;
            particles[i].y = baseY<<8;
            particles[i].vx = dirs[d][0]*32;
            particles[i].vy = dirs[d][1]*32;
            particles[i].life = 30;
            particles[i].color = color;
            break;
        }
    }
}

static void updateParticles(void) {
    for(int i=0;i<MAX_PARTICLES;i++) if(particles[i].life>0) {
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy += 2; // gravity
        particles[i].life--;
    }
}

static void drawParticles(void) {
    for(int i=0;i<MAX_PARTICLES;i++) if(particles[i].life>0) {
        drawPixel(particles[i].x>>8, particles[i].y>>8, (uint8_t)particles[i].color);
    }
}

int main(void) {
    Paddle left;
    Paddle right;
    Ball ball;
    int score1 = 0, score2 = 0;
    bool gameOver = false;
    int winner = 0;
    int warmIdx = 0, coolIdx = 0;
    uint16_t leftColor;
    uint16_t rightColor;
    uint16_t oldKeys = 0;
    int fireworkTimer = 0;

    REG_DISPCNT = MODE4 | BG2_ENABLE;
    PALETTE[COLOR_BLACK] = RGB15(0,0,0);
    PALETTE[COLOR_WHITE] = RGB15(31,31,31);

    left.y = (SCREEN_HEIGHT - PADDLE_HEIGHT)/2;
    right.y = (SCREEN_HEIGHT - PADDLE_HEIGHT)/2;
    ball.x = (SCREEN_WIDTH/2)<<8;
    ball.y = (SCREEN_HEIGHT/2)<<8;
    ball.vx = -BALL_SPEED;
    ball.vy = 0;
    leftColor = warmColors[warmIdx];
    rightColor = coolColors[coolIdx];
    PALETTE[COLOR_LEFT] = leftColor;
    PALETTE[COLOR_RIGHT] = rightColor;

    while(1) {
        uint16_t keys;
        uint16_t pressed;
        int leftX = 10;
        int rightX = SCREEN_WIDTH-10-PADDLE_WIDTH;

        keys = keysCurrent();
        pressed = (keys ^ oldKeys) & keys;
        oldKeys = keys;

        if(!gameOver) {
            int bx, by;
            if(keys & KEY_UP)   { if(left.y>0) left.y -= PADDLE_SPEED; }
            if(keys & KEY_DOWN) { if(left.y<SCREEN_HEIGHT-PADDLE_HEIGHT) left.y += PADDLE_SPEED; }
            if(keys & KEY_A)    { if(right.y>0) right.y -= PADDLE_SPEED; }
            if(keys & KEY_B)    { if(right.y<SCREEN_HEIGHT-PADDLE_HEIGHT) right.y += PADDLE_SPEED; }

            if(pressed & KEY_SELECT) { warmIdx = (warmIdx+1)%NUM_WARM; leftColor = warmColors[warmIdx]; PALETTE[COLOR_LEFT] = leftColor; }
            if(pressed & KEY_START)  { coolIdx = (coolIdx+1)%NUM_COOL; rightColor = coolColors[coolIdx]; PALETTE[COLOR_RIGHT] = rightColor; }

            ball.x += ball.vx;
            ball.y += ball.vy;
            bx = ball.x>>8;
            by = ball.y>>8;

            if(by <=0 && ball.vy<0) ball.vy = -ball.vy;
            if(by >= SCREEN_HEIGHT-BALL_SIZE && ball.vy>0) ball.vy = -ball.vy;

            if(bx <= leftX + PADDLE_WIDTH &&
               bx + BALL_SIZE >= leftX &&
               by + BALL_SIZE >= left.y && by <= left.y + PADDLE_HEIGHT) {
                int offset;
                ball.x = (leftX + PADDLE_WIDTH)<<8;
                ball.vx = BALL_SPEED;
                offset = (by + BALL_SIZE/2) - (left.y + PADDLE_HEIGHT/2);
                ball.vy = offset*32;
            }
            if(bx + BALL_SIZE >= rightX &&
               bx <= rightX + PADDLE_WIDTH &&
               by + BALL_SIZE >= right.y && by <= right.y + PADDLE_HEIGHT) {
                int offset;
                ball.x = (rightX - BALL_SIZE)<<8;
                ball.vx = -BALL_SPEED;
                offset = (by + BALL_SIZE/2) - (right.y + PADDLE_HEIGHT/2);
                ball.vy = offset*32;
            }

            if(bx < 0) {
                score2++; ball.x = (SCREEN_WIDTH/2)<<8; ball.y = (SCREEN_HEIGHT/2)<<8; ball.vx = BALL_SPEED; ball.vy = 0;
                left.y = right.y = (SCREEN_HEIGHT - PADDLE_HEIGHT)/2;
            } else if(bx > SCREEN_WIDTH-BALL_SIZE) {
                score1++; ball.x = (SCREEN_WIDTH/2)<<8; ball.y = (SCREEN_HEIGHT/2)<<8; ball.vx = -BALL_SPEED; ball.vy = 0;
                left.y = right.y = (SCREEN_HEIGHT - PADDLE_HEIGHT)/2;
            }

            if((score1>=11 || score2>=11) && (score1>score2?score1-score2:score2-score1)>=2) {
                gameOver = true;
                winner = score1>score2?1:2;
            }
        } else {
            fireworkTimer++;
            if(fireworkTimer>30) {
                spawnFirework(winner==1?COLOR_LEFT:COLOR_RIGHT);
                fireworkTimer=0;
            }
            updateParticles();
        }
        clearScreen(COLOR_BLACK);
        fillRect(leftX, left.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_LEFT);
        fillRect(rightX, right.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_RIGHT);
        drawNumber(30,10,score1,COLOR_LEFT);
        drawNumber(SCREEN_WIDTH-30-8*(score2>=10?2:1),10,score2,COLOR_RIGHT);
        fillRect(ball.x>>8, ball.y>>8, BALL_SIZE, BALL_SIZE, COLOR_WHITE);
        drawParticles();
        if(gameOver) {
            const char* msg = winner==1?"P1 WINS!":"P2 WINS!";
            int msgWidth = 8*7; // 7 chars
            drawString((SCREEN_WIDTH-msgWidth)/2, SCREEN_HEIGHT/2-4, msg, winner==1?COLOR_LEFT:COLOR_RIGHT);
        }

        waitForVBlank();
        flipPage();
    }
}
