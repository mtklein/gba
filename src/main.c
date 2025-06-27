#include <stdint.h>
#include "draw.h"

void main(void) __attribute__((noreturn));
void main(void) {
    draw_init();

    struct rgb555 *color = palette;

    *color++ = (struct rgb555){.r=31,.g=31,.b=31}; /* WHITE */
    *color++ = (struct rgb555){.r=0, .g=0, .b=0}; /* BLACK */

    /* Step 2 demonstration: just show the background tile. */
    for (;;) {
        vsync_swap();
    }
}
