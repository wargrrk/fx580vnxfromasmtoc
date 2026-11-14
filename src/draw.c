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

/* Pack a pixel the way every routine here does: (y << 8) | x, both bytes. */
static uint16_t pix(uint8_t x, uint8_t y)
{
    return (uint16_t)(((uint16_t)y << 8) | x);
}

/* 0x08D00 - the outline of a menu/selection frame.  The two vertical strokes
 * are always drawn from the start row to the row named by the low byte of
 * `spec`; the four corner groups are gated by bits of the high byte and, on
 * the bottom edge, use the row the loop finished on (r9 is part of er8, so
 * `mov er0, er8` after the loop reloads the *final* row). */
void box_frame(uint8_t *ds, uint16_t xy, uint16_t spec)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t end_y = (uint8_t)spec;
    uint8_t flags = (uint8_t)(spec >> 8);

    if (flags & 0x80) {
        screen_pixel(ds, pix((uint8_t)(x + 2), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 3), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 2), (uint8_t)(y + 1)), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 3), (uint8_t)(y + 1)), 0, 0);
    }

    if (flags & 0x20) {
        screen_pixel(ds, pix((uint8_t)(x - 1), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 2), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 1), (uint8_t)(y + 1)), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 2), (uint8_t)(y + 1)), 0, 0);
    }

    /* r9 counts up and wraps at 8 bits, so the loop always reaches end_y. */
    for (;;) {
        screen_pixel(ds, pix(x, y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);
        if (y == end_y)
            break;
        y = (uint8_t)(y + 1);
    }

    if (flags & 0x40) {
        screen_pixel(ds, pix((uint8_t)(x + 2), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 3), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 2), (uint8_t)(y - 1)), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x + 3), (uint8_t)(y - 1)), 0, 0);
    }

    if (flags & 0x10) {
        screen_pixel(ds, pix((uint8_t)(x - 1), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 2), y), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 1), (uint8_t)(y - 1)), 0, 0);
        screen_pixel(ds, pix((uint8_t)(x - 2), (uint8_t)(y - 1)), 0, 0);
    }
}

/* 0x08DC0 - a thick rounded box.  The top two rows are drawn first, the sides
 * run from `y + 2` to `(spec & 0xFF) - 2`, and on the row equal to the high
 * byte of `spec` the left side is pulled one column further out (the notch
 * that squares the corner).  The last two rows are drawn after the loop. */
void box_round_rect(uint8_t *ds, uint16_t xy, uint16_t spec)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t mid = (uint8_t)(spec >> 8);
    uint8_t end_y = (uint8_t)((uint8_t)spec - 2);

    screen_pixel(ds, pix((uint8_t)(x + 2), y), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 3), y), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 1), (uint8_t)(y + 1)), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 2), (uint8_t)(y + 1)), 0, 0);

    y = (uint8_t)(y + 2);

    for (;;) {
        screen_pixel(ds, pix(x, y), 0, 0);

        if (y == mid) {
            screen_pixel(ds, pix((uint8_t)(x - 1), y), 0, 0);
            screen_pixel(ds, pix((uint8_t)(x - 1), (uint8_t)(y - 1)), 0, 0);
            screen_pixel(ds, pix((uint8_t)(x - 1), (uint8_t)(y + 1)), 0, 0);
            /* The notch falls through into the .l_05C store, so this pixel is
             * drawn only on the `mid` row. */
            screen_pixel(ds, pix((uint8_t)(x - 2), y), 0, 0);
        } else {
            screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);
        }

        if (y == end_y)
            break;
        y = (uint8_t)(y + 1);
    }

    y = (uint8_t)(y + 1);
    screen_pixel(ds, pix(x, y), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);

    y = (uint8_t)(y + 1);
    screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 2), y), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 2), (uint8_t)(y + 1)), 0, 0);
    screen_pixel(ds, pix((uint8_t)(x + 3), (uint8_t)(y + 1)), 0, 0);
}

/* 0x08CB8 - the one-pixel frame: a single vertical stroke from the start row
 * to `spec & 0xFF` with optional half-pixel caps on the top row (bit 7 right,
 * bit 5 left) and the bottom row (bit 6 right, bit 4 left).  Like box_frame it
 * plots through 0x091E2, the unchecked pixel store, and the bottom caps use
 * the row the loop finished on. */
void box_frame_thin(uint8_t *ds, uint16_t xy, uint16_t spec)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t end_y = (uint8_t)spec;
    uint8_t flags = (uint8_t)(spec >> 8);

    if (flags & 0x80)
        screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);
    if (flags & 0x20)
        screen_pixel(ds, pix((uint8_t)(x - 1), y), 0, 0);

    for (;;) {
        screen_pixel(ds, pix(x, y), 0, 0);
        if (y == end_y)
            break;
        y = (uint8_t)(y + 1);
    }

    if (flags & 0x40)
        screen_pixel(ds, pix((uint8_t)(x + 1), y), 0, 0);
    if (flags & 0x10)
        screen_pixel(ds, pix((uint8_t)(x - 1), y), 0, 0);
}

/* 0x08C7A - drop one 24-byte row out of buffer 1: the rows below `index` move
 * up one slot (row 0 is left alone, so the shift starts at 0xDDEC) and the
 * tail the move leaves behind is zeroed.  `high` only survives as the high
 * byte of the `mul er2, r0` operand, because the listing loads the row size
 * with `mov r2, 24` and leaves r3 alone - the row count is therefore
 * `(24 | (high & 0xFF00)) * (index + 1)`, computed in 16 bits like the ROM. */
uint16_t screen_scroll_rows_up(uint8_t *ds, uint16_t index, uint16_t high)
{
    uint16_t rows = (uint16_t)(24u * (uint16_t)(uint8_t)((uint8_t)index + 1u));

    (void)high;
    uint16_t count = (uint16_t)(0x0600u - rows);

    memmove_nn(ds, 0xDDECu, (uint16_t)(0xDDD4u + rows), count);

    return memzero(ds, (uint16_t)(0xE3D4u - rows + 24u),
                   (uint16_t)(rows - 24u));
}

/* 0x08BFC - clear `24 * value` bytes of buffer 1 starting at row 1.  The `mul`
 * is 8x8, so only the low byte of the argument picks the count.  A count over
 * 150 rows runs past 0xEC00 and the copy eats its own stack, which is where the
 * ROM stops having a behaviour to match, so the test keeps within that. */
uint16_t row_buffer_clear_24(uint8_t *ds, uint16_t value)
{
    return memzero(ds, 0xDDECu, (uint16_t)(24u * (uint16_t)(uint8_t)value));
}
