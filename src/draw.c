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
    /* er10 = end_low - x0 as a *16-bit* subtraction of two zero-extended
     * bytes, so |dx| runs to 255.  A signed-byte delta here makes every line
     * wider than 127 columns take the wrong step sign. */
    int16_t dx = (int16_t)((uint16_t)((uint16_t)(uint8_t)end - (uint16_t)x0));
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

/* Word loads and stores go through the core's alignment rule: the address bit
 * 0 is dropped for anything wider than a byte, so an odd pointer reads and
 * writes the word that starts one byte below it. */
static uint16_t draw_ld16(const uint8_t *ds, uint16_t addr)
{
    uint16_t a = (uint16_t)(addr & 0xFFFEu);

    return (uint16_t)(ds[a] | ((uint16_t)ds[(uint16_t)(a + 1)] << 8));
}

static void draw_st16(uint8_t *ds, uint16_t addr, uint16_t value)
{
    uint16_t a = (uint16_t)(addr & 0xFFFEu);

    ds_put(ds, a, (uint8_t)value);
    ds_put(ds, (uint16_t)(a + 1), (uint8_t)(value >> 8));
}

/* 0x09B90 - the shared tail of f_09B72.  `mov r1, 1` puts the 1 in the high
 * half of er0 before the byte move overwrites only the low half, so this comes
 * back as 0x0100 or 0x0101 rather than 0 or 1.  Callers that test `cmp r0, 0`
 * only look at the low byte. */
static uint16_t submode_is_one(uint8_t *ds)
{
    return (uint16_t)(0x0100u | ((ds[0xD116] & 0x0Fu) == 1u ? 1u : 0u));
}

/* 0x2E918 */
uint16_t glyph_row_insert(uint8_t *ds, uint16_t cell, uint16_t pos,
                          const uint16_t *stk)
{
    /* `push lr` pushes lcsr *and* lr, so the frame is sixteen bytes and the
     * stacked words sit at er14 + 16/18/20 with the byte at 22. */
    uint16_t x_p = stk[0];      /* 16[er14] */
    uint16_t limit_p = stk[1];  /* 18[er14] */
    uint16_t row_p = stk[2];    /* 20[er14] */
    uint8_t extra = (uint8_t)stk[3];    /* 22[er14], read as a byte */

    uint16_t acc = cell;        /* er0: r0 in the low byte, r1 in the high one */
    uint16_t er6 = 0;
    uint16_t er2 = 0;
    uint16_t er12 = 0;
    uint8_t font = ds[0xD137];
    uint8_t r5 = 0;
    uint8_t flag = 0;
    uint8_t f1 = 0;
    uint8_t f2 = 0;

    ds_put(ds, row_p, 0);                               /* 2E92E */

    /* 2E930 mov r0, r4 ; 2E932 mov r1, 0 */
    acc = font;
    er12 = (uint16_t)(draw_ld16(ds, cell) + acc);        /* 2E934..2E936 */
    er6 = er12;                                         /* 2E938 */
    er12 = (uint16_t)(er12 + acc);                      /* 2E93A */

    if (!((int16_t)er12 > 65))                          /* 2E93C bc gts */
        goto tail;

    if ((uint8_t)submode_is_one(ds) == 0)               /* 2E946..2E94C */
        goto l_090;
    if (ds[limit_p] == 0)                               /* 2E950..2E952 */
        goto l_090;
    if (extra != 0)                                     /* 2E954..2E956 */
        goto l_064;

    er2 = ds[limit_p];                                  /* 2E958 l r2 ; r3 = 0 */
    acc = (uint16_t)(font + er2);                       /* 2E95C..2E960 */
    if ((int16_t)acc <= 65)                             /* 2E966 bc les */
        return (uint16_t)(acc & 0xFF00u);               /* 2E970 .l_058 */
    if (ds[x_p] >= 192)                                 /* 2E96C bc ge */
        ds_put(ds, x_p, ds[0xD156]);                    /* 2E976 .l_05E */
    else
        return (uint16_t)(acc & 0xFF00u);

l_064:
    acc = (uint16_t)((acc & 0x00FFu) | 0x0100u);        /* 2E97C mov r1, 1 */
    if (ds[0xD111] == 2)                                /* 2E982 */
        acc = (uint16_t)((acc & 0x00FFu) | 0x0F00u);    /* 2E986 mov r1, 15 */

    er2 = ds[limit_p];                                  /* 2E98A..2E98C */
    er12 = (uint16_t)(acc >> 8);                        /* 2E98E */
    acc = (uint16_t)(font + er12);                      /* 2E992..2E996 */
    if ((int16_t)er2 < (int16_t)acc)                    /* 2E99A bc ges */
        return (uint16_t)(acc & 0xFF00u);               /* 2E99C..2E99E */
    ds_put(ds, limit_p, (uint8_t)(ds[limit_p] - font));  /* 2E9A2..2E9A6 */

l_090:
    ds_put(ds, row_p, 1);                               /* 2E9AC */
    r5 = font;                                          /* 2E9AE */
    if (ds[0xD111] == 2)                                /* 2E9B4..2E9B8 */
        goto l_12C;

    flag = 0;                                           /* 2E9BC..2E9BE */
    if (ds[0xD15A] == 0) {                              /* 2E9C0 */
        f2 = (uint8_t)(font + 1);                       /* 2E9C6..2E9CA */
        f1 = (uint8_t)(f2 + font);                      /* 2E9CC..2E9D0 */
        acc = f2;                                       /* 2E9D2 mov r1, 0 */
        er2 = draw_ld16(ds, pos);                       /* 2E9D4 */
        if ((int16_t)er2 <= (int16_t)acc)               /* 2E9D8 bc les */
            goto l_0E0;

        er2 = f1;                                       /* 2E9DA..2E9DC */
        acc = draw_ld16(ds, pos);                       /* 2E9DE */
        if ((int16_t)acc >= (int16_t)er2)               /* 2E9E2 bc ges */
            goto l_0F4;
        acc = (uint16_t)(acc + 63);                     /* 2E9E4 */
        acc = (uint16_t)(acc - er6);                    /* 2E9E6..2E9E8 */
        er2 = (uint16_t)(font + font);                  /* 2E9EA..2E9EC */
        if ((int16_t)acc >= (int16_t)er2)               /* 2E9F0 bc ges */
            goto l_0F4;
        flag = 1;                                       /* 2E9F2..2E9F4 */
        goto l_0F4;

    l_0E0:
        acc = er2;                                      /* 2E9F8 */
        if ((int16_t)er2 <= 1)                          /* 2E9FA..2E9FE */
            goto l_0F4;
        er2 = f2;                                       /* 2EA00..2EA02 */
        if ((int16_t)acc >= (int16_t)er2)               /* 2EA04..2EA06 */
            goto l_0F4;
        flag = 1;                                       /* 2EA08..2EA0A */
    }

l_0F4:
    if (flag == 0)                                      /* 2EA0C..2EA0E */
        goto l_12C;

    acc = draw_ld16(ds, pos);                           /* 2EA10 */
    acc = (uint16_t)(acc - 1);                          /* 2EA12 */
    r5 = (uint8_t)acc;                                  /* 2EA16 */
    er2 = (uint16_t)(acc & 0x00FFu);                    /* 2EA14..2EA1C */
    acc = (uint16_t)(er2 - font);                       /* 2EA1C..2EA1E */
    er6 = acc;                                          /* 2EA22 */
    er2 = (uint16_t)(draw_ld16(ds, cell) - acc);        /* 2EA24..2EA26 */
    draw_st16(ds, cell, er2);                           /* 2EA2A */
    if (ds[limit_p] != 0)                               /* 2EA30 bc le */
        ds_put(ds, limit_p, (uint8_t)(ds[limit_p] - (uint8_t)er6));
    if ((er6 & 0x8000u) != 0)                           /* 2EA3A..2EA3C bc ps */
        ds_put(ds, row_p, 0);                           /* 2EA3E..2EA42 */

l_12C:
    acc = draw_ld16(ds, pos);                           /* 2EA44..2EA46 */
    acc = (uint16_t)(acc - r5);                         /* 2EA48..2EA4A */
    draw_st16(ds, pos, acc);                            /* 2EA4C */
    if ((acc & 0x8000u) != 0) {                         /* 2EA4E..2EA50 */
        acc = (uint16_t)(font + draw_ld16(ds, pos));    /* 2EA52..2EA58 */
        draw_st16(ds, pos, acc);                        /* 2EA5A */
    }

    /* 2EA5C mov r0, r5 - only the low half changes, and the scroll returns the
     * memzero destination, whose high byte is what r1 ends up holding. */
    acc = screen_scroll_rows_up(ds, (uint16_t)((acc & 0xFF00u) | r5), 0);
    return (uint16_t)((acc & 0xFF00u) | 1u);            /* 2EA62 */

tail:
    draw_st16(ds, cell, er6);                           /* 2EA68 */
    return (uint16_t)((acc & 0xFF00u) | 1u);            /* 2EA6A */
}
