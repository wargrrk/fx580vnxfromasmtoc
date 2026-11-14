/* draw.c - coordinate scalers and the Bresenham line drawer.
 *
 * These are the primitives the graph/matrix drawing code builds on.  All the
 * arithmetic is 8- and 16-bit on the nX-U8/100, so the wrap-around of the
 * original is reproduced instead of being "fixed".
 */
#include "draw.h"

#include "print.h"
#include "runtime.h"

/* 0x0970E: r6 = low(10*(x-1)) + 11, r0 = low(42*(y-1)) + 22, and the result
 * is (r6 << 8) | r0 - the high half comes from r1, which the routine loads
 * from r6 itself. */
uint16_t cell_corner_br(uint8_t *ds, uint16_t cell)
{
    (void)ds;
    uint8_t x = (uint8_t)((uint8_t)cell - 1);
    uint8_t y = (uint8_t)((cell >> 8) - 1);
    uint8_t hi = (uint8_t)((uint8_t)(10u * x) + 11);
    uint8_t lo = (uint8_t)((uint8_t)(42u * y) + 22);

    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* 0x09734: the same shape with a 12-pixel cell width and offsets 3 and 15. */
uint16_t cell_corner_tr(uint8_t *ds, uint16_t cell)
{
    (void)ds;
    uint8_t x = (uint8_t)((uint8_t)cell - 1);
    uint8_t y = (uint8_t)((cell >> 8) - 1);
    uint8_t hi = (uint8_t)((uint8_t)(12u * x) + 3);
    uint8_t lo = (uint8_t)((uint8_t)(62u * y) + 15);

    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* 0x0975A - the bottom corner of the same 12x62 cell: offset 15 on both axes. */
uint16_t cell_corner_br2(uint8_t *ds, uint16_t cell)
{
    (void)ds;
    uint8_t x = (uint8_t)((uint8_t)cell - 1);
    uint8_t y = (uint8_t)((cell >> 8) - 1);
    uint8_t hi = (uint8_t)((uint8_t)(12u * x) + 15);
    uint8_t lo = (uint8_t)((uint8_t)(62u * y) + 15);

    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* 0x09780 - row 2 is the row the "wide" labels use; there the origin moves by
 * one cell width, and submode 2 pulls it back by 12 pixels. */
uint16_t cell_origin(uint8_t *ds, uint16_t cell)
{
    uint8_t y = (uint8_t)((cell >> 8) - 1);
    uint8_t lo = (uint8_t)((uint8_t)(42u * y) + 9);
    uint8_t hi = 21;

    if (y == 2) {
        if (ds[0xD112] == 2)
            lo = (uint8_t)(lo + 12);
        else
            lo = (uint8_t)((uint8_t)(lo + 42) + 12);
    }
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* 0x09C4C - the record index is (x-1) + 4*(y-1), plus the low byte of 16*x,
 * scaled by ten and offset into the table at 0xD392. */
uint16_t cell_record_addr(uint8_t *ds, uint16_t cell, uint16_t index)
{
    (void)ds;
    uint8_t x = (uint8_t)cell;
    uint8_t y = (uint8_t)(cell >> 8);

    /* r2 carries its own argument (`mov r1, r3; mov r0, r10` in the callers),
     * r3 is cleared, so the sum starts from that byte minus one. */
    uint16_t sum = (uint16_t)((uint8_t)((uint8_t)index - 1) + 4u * (uint8_t)(y - 1));
    uint8_t low = (uint8_t)((uint8_t)sum + (uint8_t)((uint16_t)x * 16u));

    /* `mul er2, r0` multiplies the low byte only, so the 16-bit sum loses its
     * high half here - just like the ROM. */
    return (uint16_t)(0xD392u + (uint16_t)((uint16_t)low * 10u));
}

/* 0x08E62 - Bresenham.  The ROM keeps the step signs in r2/r3 and the current
 * point in r6/r7, swapping the roles of r6/r7 for steep lines; the byte at
 * [sp-3] remembers which of the two it is.  The loop counter is the low byte
 * of |dx| taken *before* the steep swap, so a line whose |dy| wins steps
 * |dx| times - one pixel per column, which is how the original draws. */
void draw_line(uint8_t *ds, uint16_t start, uint16_t end)
{
    uint8_t x0 = (uint8_t)start;
    uint8_t y0 = (uint8_t)(start >> 8);
    int16_t dx = (int16_t)(int8_t)((uint8_t)end - x0);
    int16_t dy = (int16_t)((int16_t)(int8_t)(uint8_t)(end >> 8) - (int16_t)(int8_t)y0);

    uint8_t r6 = x0;
    uint8_t r7 = y0;
    uint8_t sx = 1;
    uint8_t sy = 1;
    uint16_t step_s1;
    uint16_t step_s2;
    uint16_t err;
    uint16_t dx2;
    uint16_t dy2;
    uint8_t count;
    uint8_t steep = 0;

    screen_pixel(ds, start, 0, 1);

    if (dx < 0) {
        dx = (int16_t)(0 - (uint16_t)dx);
        sx = 0xFF;
    }
    if (dy < 0) {
        dy = (int16_t)(0 - (uint16_t)dy);
        sy = 0xFF;
    }

    /* 0x08EAA parks |dx| in er8 as the loop counter, and the steep branch
     * replaces it with |dy| at 0x08EB6 before swapping the two deltas. */
    count = (uint8_t)dx;

    if ((uint16_t)dy > (uint16_t)dx) {
        count = (uint8_t)dy;

        uint8_t swap = r6;
        r6 = r7;
        r7 = swap;

        uint16_t swap16 = (uint16_t)dx;
        dx = dy;
        dy = (int16_t)swap16;

        uint8_t swap8 = sx;
        sx = sy;
        sy = swap8;

        steep = 1;
    }

    /* The ROM parks r2/r3 at [sp-2]/[sp-1] and the steep flag at [sp-3]. */
    step_s2 = sx;
    step_s1 = sy;

    err = (uint16_t)dx;
    dx2 = (uint16_t)((uint16_t)dx << 1);
    dy2 = (uint16_t)((uint16_t)dy << 1);

    for (uint8_t n = count; n != 0; n--) {
        err = (uint16_t)(err - dy2);
        if ((int16_t)err < 0) {
            err = (uint16_t)(err + dx2);
            r7 = (uint8_t)(r7 + step_s1);
        }
        r6 = (uint8_t)(r6 + step_s2);

        /* `l r2, -3[er14]` reloads the steep flag and the load sets Z from
         * it, so the swap happens exactly on steep lines. */
        uint8_t a = r6;
        uint8_t b = r7;
        if (steep) {
            a = r7;
            b = r6;
        }
        screen_pixel(ds, (uint16_t)(((uint16_t)b << 8) | a), 0, 1);
    }
}

/* 0x08BDE - clear the 24-byte rows from `index` to the end of buffer 2.
 * `mul er0, r1` keeps the low byte of the argument and writes the 16-bit
 * product back into er0, so r1 ends up holding the *high* half of index*24 and
 * the `subc r3, r1` that follows is a correct 16-bit subtraction. */
uint16_t screen_rows_clear_from(uint8_t *ds, uint16_t index)
{
    uint16_t offset = (uint16_t)((uint8_t)index * 24u);
    uint16_t count = (uint16_t)(0x0600u - offset);

    return memzero(ds, (uint16_t)(0xDDD4u + offset), count);
}
