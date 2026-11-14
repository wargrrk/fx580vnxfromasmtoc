/*
 * print.c - character drawing.
 *
 * The screen is a 192x64 monochrome bitmap stored as 24 bytes per row with
 * the leftmost pixel in bit 7.  Glyphs come out of the font extractors as
 * 16-bit rows (14-pixel font) or 8-bit rows (10/8-pixel fonts), MSB first,
 * and are merged into the buffer with a per-bit mask.
 */
#include "print.h"

#include "../src/runtime.h"
#include "font.h"

#define FONT_SIZE_ADDR   0xD137
#define DRAW_MODE_ADDR   0xD138
#define CURRENT_BUF_ADDR 0xD139

/* Mask tables in the ROM.  0x0388C keeps the high (8-bit) bits of a byte,
 * 0x03894 keeps the low (bit+1) bits. */
#define MASK_HI_ADDR 0x388Cu
#define MASK_LO_ADDR 0x3894u
/* The rectangle fill at 0x09834 reads a different low-bits table, two bytes
 * further along the same block. */
#define FILL_MASK_LO_ADDR 0x389Cu

/* Control character 1 in a printed string.  The ROM asks f_10E3A for an
 * index, which reads one byte through data segment 3 (0x3FFDC); the flat
 * 64 KiB data window used here cannot express that address, so the constant
 * is captured directly. */
#define CONTENT_1_INDEX 3u
/* d_038A4: character used for control character 1, indexed by the value above. */
#define CONTENT_1_CHARS 0x38A4u
/* f_29978 always returns the width of one control-character-1 cell. */
#define CONTENT_1_STEP 4u
#define FLAG_0D11C 0xD11Cu
#define MODE_0D111 0xD111u

/* One merged byte, plus the r0/r1 pair the ROM leaves behind, which differs per
 * draw mode: some paths end with the new byte in r0 and the old one in r1,
 * others swap that or keep the mask in r1.  Callers that only care about the
 * screen ignore the return; 0x093C0 hands it straight back to its caller, and
 * the differential test compares it. */
uint16_t screen_blend(uint8_t *ds, uint16_t addr, uint8_t value, uint8_t mask)
{
    uint8_t old, fresh;
    uint16_t pair;

    if (addr <= DRAW_MIN_ADDR)
        return (uint16_t)(((uint16_t)mask << 8) | value);
    if (mask != 0)
        value &= mask;

    /* the read happens even where the store will be dropped - the chip lets
     * the load through and only discards the write */
    old = ds[addr];
    switch (ds[DRAW_MODE_ADDR]) {
    case 0:                     /* replace masked bits */
        fresh = (uint8_t)((old & (uint8_t)~mask) | value);
        pair = (uint16_t)(((uint16_t)old << 8) | fresh);
        break;
    case 1:                     /* set */
        fresh = (uint8_t)(old | value);
        pair = (uint16_t)(((uint16_t)mask << 8) | fresh);
        break;
    case 2:                     /* clear */
        fresh = (uint8_t)(old & (uint8_t)((uint8_t)~mask | value));
        pair = (uint16_t)(((uint16_t)old << 8) | fresh);
        break;
    case 4:                     /* replace with the complement */
        fresh = (uint8_t)((old & (uint8_t)~mask) |
                          ((uint8_t)~value & mask));
        pair = (uint16_t)(((uint16_t)fresh << 8) |
                          (uint8_t)(old & (uint8_t)~mask));
        break;
    default:                    /* mode 3 and anything else falls through to XOR */
        fresh = (uint8_t)(old ^ value);
        pair = (uint16_t)(((uint16_t)mask << 8) | fresh);
        break;
    }
    /* the chip drops stores outside RAM/SFR/LCD, so the model must not invent
     * them either */
    if (ds_writable(addr))
        ds[addr] = fresh;
    return pair;
}

void screen_blend_immediate(uint8_t *ds, uint16_t addr, uint8_t value, uint8_t mask)
{
    (void)screen_blend(ds, addr, (uint8_t)(value & mask), mask);
}

void screen_pixel(uint8_t *ds, uint16_t xy, int clear, int check_bounds)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);

    if (check_bounds && (x > 191 || y > 63))
        return;

    uint16_t base = ds[CURRENT_BUF_ADDR] != 0 ? 0xDDD4u : 0xE3D4u;
    uint16_t address = (uint16_t)(base + (uint16_t)(24u * y) + (x >> 3));
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    /* with the bounds check off the row index runs to 255, which walks the
     * address out of the buffers and into ROM or open bus; the chip drops
     * those stores and so must the model */
    if (!ds_writable(address))
        return;
    if (clear)
        ds[address] &= (uint8_t)~mask;
    else
        ds[address] |= mask;
}

static uint16_t buffer_addr(uint16_t base, uint8_t stride, uint16_t xy,
                            uint16_t *out)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    /* y >= 0x80 addresses rows above the buffer (the ROM negates it). */
    int16_t row = (int8_t)y;
    uint16_t a = (uint16_t)(base + stride * row + (x >> 3));

    *out = a;
    return (uint16_t)(x & 7);
}

uint16_t screen_addr(uint8_t *ds, uint16_t xy, uint16_t *out)
{
    uint16_t base = ds[CURRENT_BUF_ADDR] != 0 ? 0xDDD4u : 0xE3D4u;

    return buffer_addr(base, SCREEN_ROW_BYTES, xy, out);
}

uint16_t lcd_addr(uint8_t *ds, uint16_t xy, uint16_t *out)
{
    (void)ds;
    return buffer_addr(0xF800u, 32, xy, out);
}

/* 0x093C0 - merge one byte into the screen at `addr`, for a 16-bit entry whose
 * halves are `value` (low) and `mask` (high).
 *
 * The routine only touches addresses *above* the first row of buffer 1: the
 * guard is `0xDDEB >= addr`, so 0xDDEB itself is refused and 0xDDEC - the last
 * byte of row 0 - is the first byte it may write.  Nothing else is range
 * checked, so the store can land in the LCD or in ROM, where the chip drops it.
 *
 * Returns the er0 pair the ROM has at its `rt`: the untouched arguments when
 * the guard fired, otherwise whatever the chosen draw-mode path left, see
 * screen_blend(). */
uint16_t screen_blit_masked(uint8_t *ds, uint16_t value_mask, uint16_t addr)
{
    uint8_t value = (uint8_t)value_mask;
    uint8_t mask = (uint8_t)(value_mask >> 8);

    if (addr <= DRAW_MIN_ADDR)
        return value_mask;
    return screen_blend(ds, addr, (uint8_t)(value & mask), mask);
}

void char_print(uint8_t *ds, uint16_t xy, uint8_t ch, uint8_t marker)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t font = ds[FONT_SIZE_ADDR];
    uint8_t glyph[2 * FONT14_ROWS];
    uint8_t width, threshold, height, stride, mid_mask;
    uint8_t bit, mask_a, mask_b, counter;
    uint16_t screen, end, glyph_pos;
    int blank = 0;

    if (x > 191)
        return;
    if ((int8_t)y >= 64)
        return;

    if (marker == 241 || marker == 242) {
        width = 12;
        threshold = 4;
        height = (uint8_t)(font - 2);
    } else if (font == 14) {
        width = 11;
        threshold = 5;
        height = (uint8_t)(font - 1);
    } else {
        width = 6;
        threshold = 2;
        height = (uint8_t)(font - 1);
    }

    end = ds[CURRENT_BUF_ADDR] != 0 ? 0xE3D3u : 0xE9D3u;

    if (font > 10)
        char_get_14_buf(ds, ch, marker, glyph);
    else
        char_get_l14_buf(ds, ch, marker, glyph);

    bit = (uint8_t)screen_addr(ds, xy, &screen);
    mask_a = ds[MASK_HI_ADDR + bit];
    mask_b = ds[MASK_LO_ADDR + ((width + x) & 7)];
    stride = font > 10 ? 2 : 1;
    mid_mask = font > 10 ? 0xFF : threshold;

    glyph_pos = 0;
    counter = 0;
    for (;;) {
        const uint8_t *row = blank ? ds + BLANK_GLYPH : glyph + glyph_pos;

        if (stride == 1) {
            if (bit == 0) {
                screen_blend(ds, screen, row[0], mask_b);
            } else {
                uint8_t v = (uint8_t)(row[0] >> bit);
                int shift = 8 - width - bit;

                if (shift > 0)
                    mask_a &= (uint8_t)(0xFF << shift);
                screen_blend(ds, screen, (uint8_t)(v & mask_a), mask_a);
                if (bit >= threshold + 1) {
                    uint8_t v2 = (uint8_t)(row[0] << (8 - bit));

                    screen_blend(ds, (uint16_t)(screen + 1),
                                 (uint8_t)(v2 & mask_b), mask_b);
                }
            }
        } else {
            uint16_t v = (uint16_t)(row[0] | ((uint16_t)row[1] << 8));
            uint16_t shifted = (uint16_t)(v >> bit);
            uint8_t lo = (uint8_t)shifted;
            uint8_t hi = (uint8_t)(shifted >> 8);

            screen_blend(ds, screen, hi, mask_a);
            if (bit < threshold + 1) {
                screen_blend(ds, (uint16_t)(screen + 1), lo, mask_b);
            } else {
                screen_blend(ds, (uint16_t)(screen + 1), lo, mid_mask);
                screen_blend(ds, (uint16_t)(screen + 2),
                             (uint8_t)(row[0] << (8 - bit)), mask_b);
            }
        }

        screen = (uint16_t)(screen + SCREEN_ROW_BYTES);
        if (screen > end)
            return;
        glyph_pos = (uint16_t)(glyph_pos + stride);
        counter++;
        if (counter == height)
            blank = 1;
        if (counter > height)
            return;
    }
}

/* 0x0904C - char_print with the marker forced to 0.  The entry is `push lr /
 * push xr4 / push qr8 / mov r3, 0` and then a branch into the body at 0x0905C,
 * which is exactly where char_print's own prologue ends; the only difference
 * between the two entry points is the byte in r3.  char_print has no observable
 * return value (its test compares the screen only), so neither does this. */
void char_print_1byte(uint8_t *ds, uint16_t xy, uint8_t ch)
{
    char_print(ds, xy, ch, 0);
}

/*
 * 0x08FF6 - control character 1 inside a string.  Draws one cell (in "set
 * pixels" mode) just to the left of the cursor and advances the line by a
 * fixed amount.  Returns the new x coordinate.
 */
static uint8_t print_control_1(uint8_t *ds, uint16_t xy, uint8_t marker)
{
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t x = (uint8_t)xy;
    uint8_t index = CONTENT_1_INDEX;
    uint8_t flag = ds[FLAG_0D11C];
    uint8_t ch, save, offset;

    if (index == 1)
        flag ^= 1;
    if (flag == 0 || ds[MODE_0D111] == 2)
        ch = ' ';
    else
        ch = ds[(uint8_t)(CONTENT_1_CHARS + index)];

    save = ds[DRAW_MODE_ADDR];
    ds[DRAW_MODE_ADDR] = 1;
    offset = (uint8_t)(x - (ds[FONT_SIZE_ADDR] == 14 ? 2 : 1));
    char_print(ds, (uint16_t)((y << 8) | offset), ch, marker);
    ds[DRAW_MODE_ADDR] = save;

    return (uint8_t)(x + CONTENT_1_STEP);
}

uint8_t line_print(uint8_t *ds, uint16_t xy, uint16_t str, uint16_t *str_out)
{
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t x = (uint8_t)xy;
    uint8_t width = ds[FONT_SIZE_ADDR] == 14 ? 11 : 6;
    uint8_t limit = (uint8_t)(192 - width);
    uint8_t count = 0;
    uint8_t marker = 0;

    while (x <= limit) {
        uint8_t c = ds[str];

        if (c == 0)
            break;
        if (c >= 240) {
            /* Symbol-font markers are consumed silently and become the
             * marker of the next character. */
            marker = c;
        } else if (c == 1) {
            x = print_control_1(ds, (uint16_t)((y << 8) | x), marker);
            count++;
            marker = 0;
        } else if (c == 4) {
            /* In the 14-pixel font, byte 4 escapes the next character so it
             * is drawn nine pixels earlier.  Other fonts drop the byte. */
            if (ds[FONT_SIZE_ADDR] == 14) {
                str++;
                char_print(ds, (uint16_t)((y << 8) | x), ds[str], marker);
                x += 9;
                count++;
                marker = 0;
            }
        } else {
            char_print(ds, (uint16_t)((y << 8) | x), c, marker);
            x += (marker == 241 || marker == 242) ? 12 : width;
            count++;
            marker = 0;
        }
        str++;
    }

    if (str_out)
        *str_out = str;
    return count;
}

/* 0x08F7C - line_print entered one instruction earlier, with `mov r0, 0` in
 * front of the body.  Only the low half of er0 is cleared, so the caller's r1
 * is still the y coordinate, the x coordinate becomes 0, and the high half of
 * the returned word is whatever r1 held on the way in. */
uint16_t line_print_col0(uint8_t *ds, uint16_t arg, uint16_t str,
                         uint16_t *str_out)
{
    uint16_t xy = (uint16_t)(arg & 0xFF00u);

    return (uint16_t)(((uint16_t)arg & 0xFF00u) |
                      line_print(ds, xy, str, str_out));
}

uint8_t print_mode_label(uint8_t *ds)
{
    uint8_t mode = ds[0xD111];
    uint8_t submode = ds[0xD112];
    uint8_t index = 0;

    ds[FONT_SIZE_ADDR] = 10;
    if (mode == 69) {
        if (submode == 5)
            index = 1;
        else if (submode == 6)
            index = 2;
    } else if (mode == 75) {
        uint8_t detail = ds[0xD149];
        if (detail < 4) {
            if (submode == 4)
                index = (uint8_t)(3 + detail);
            else if (submode == 5)
                index = (uint8_t)(7 + detail);
            else if (submode == 6)
                index = (uint8_t)(11 + detail);
        }
    }

    uint16_t entry = (uint16_t)(0x3F74u + 2u * index);
    uint16_t string = (uint16_t)(ds[entry] | ((uint16_t)ds[(uint16_t)(entry + 1)] << 8));
    return line_print(ds, 0x0100, string, NULL);
}

/* 0x08F02 / 0x08F04 - expand one level of a nested smart string into the
 * 64-byte frame at `buf` and print it at the origin.  Tags 2 and 4 pull a
 * constant out of the string tables (tag 4 sets the high byte of the index)
 * and everything else is copied verbatim.  When the expanded text is longer
 * than DECOMP_MAX the ROM asks for a copy into the static buffer at 0x1DB4
 * with the arguments swapped, so the frame ends up holding the string kept
 * there; printing still continues from the frame.  Returns the character
 * count from line_print and, through `end_out`, the source pointer past the
 * terminator. */
uint16_t str_decompress_print(uint8_t *ds, uint16_t str, uint16_t buf,
                              uint16_t *end_out)
{
    uint16_t src = str;
    uint16_t dst = buf;
    uint16_t out = buf;
    uint8_t count;

    while (ds[src] != 0) {
        uint8_t tag = ds[src];

        if (tag == 2 || tag == 4) {
            uint16_t index;

            src++;
            /* Tag 4 references the string tables with the high byte set (and
             * its index one lower), tag 2 references the low table. */
            index = tag == 4 ? (uint16_t)(0x0100u | (uint8_t)(ds[src] - 1))
                             : ds[src];
            dst = (uint16_t)(dst + copy_string_constant(ds, index, dst));
        } else {
            ds[dst++] = tag;
        }
        src++;
    }
    ds[dst] = 0;

    /* Too long for the frame: the ROM asks for a copy into the static buffer
     * at 0x1DB4, but passes the two arguments the other way round, so the
     * frame is overwritten with the "ERROR" string kept there.  Printing
     * always continues from the frame. */
    if (smart_strlen_n(ds, out) > DECOMP_MAX)
        smart_strcpy_nn(ds, out, DECOMP_STATIC);

    count = line_print(ds, 0, out, NULL);
    if (end_out)
        *end_out = (uint16_t)(str + smart_strlen_n(ds, str));
    return count;
}

/* 0x09826 / 0x09834 - fill a rectangle with a solid pattern.
 *
 * Three entries share one body; they differ only in the 16-bit pattern they
 * push into the frame: 0x09826 clears (0x0000), 0x09834 fills (0xFFFF) and
 * 0x09846 takes it from a stacked argument.  The first two are modelled here.
 *
 * The geometry check is done on bytes: the additions are `add r0, r2` /
 * `add r1, r3` followed by `bc lt`, which aborts on the *carry out of the
 * byte*, not on the 16-bit sum, and then `cmp r0, 193` / `cmp r1, 65` reject
 * only the low byte.  So a rect may end at x + w == 192 and y + h == 64 and
 * still be drawn.
 *
 * The per-row walk works in bytes: the bit position from screen_addr selects a
 * mask pair from two ROM tables (0x0388C = the low bits of the byte, 0x0389C =
 * the high ones), and the pattern is shifted right by that bit count so the
 * halves line up.  Each iteration merges one byte with the low mask and the
 * *next* byte with the high mask; on the last, partial byte both masks are
 * shifted left by 7 - remaining and re-masked with the table value they came
 * from, which is what keeps the pair inside one byte.  A row costs
 * 24 - written bytes of advance, and the row loop runs exactly h times.
 *
 * The frame word holds the running address, so the return value is that
 * address after the last advance - the ROM's er0 comes from the same cell. */
static uint16_t fill_rect_pattern(uint8_t *ds, uint16_t xy, uint16_t size,
                                  uint16_t pattern)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t w = (uint8_t)size;
    uint8_t h = (uint8_t)(size >> 8);
    uint16_t cell = 0;
    uint8_t bit, remaining, save, rows, written, r13, r12, r14, r15;

    if ((uint16_t)x + w > 0xFFu || (uint8_t)(x + w) >= 193)
        return xy;
    if ((uint16_t)y + h > 0xFFu || (uint8_t)(y + h) >= 65)
        return xy;

    bit = (uint8_t)screen_addr(ds, xy, &cell);
    remaining = (uint8_t)(w - 1);
    save = remaining;
    rows = h;

    for (;;) {
        {
            /* the pattern's low byte is the one that is shifted: the ROM loads
             * it into the high half of er12 and shifts that */
            uint16_t spread = (uint16_t)(((uint16_t)(pattern & 0xFFu) << 8) >>
                                         bit);

            r13 = (uint8_t)(spread >> 8);
            r12 = (uint8_t)spread;
        }
        r15 = ds[MASK_HI_ADDR + bit];
        r14 = ds[FILL_MASK_LO_ADDR + bit];
        written = 0;
        for (;;) {
            if (remaining < 8) {
                uint8_t shift = (uint8_t)(7 - remaining);
                uint16_t pair = (uint16_t)(((uint16_t)r15 << 8) | r14) << shift;
                uint8_t keep = r15;

                r15 = (uint8_t)((pair >> 8) & keep);
                r14 = (uint8_t)pair;
            }
            written++;
            (void)screen_blit_masked(ds, (uint16_t)(((uint16_t)r15 << 8) | r13),
                                     cell);
            cell = (uint16_t)(cell + 1);
            if (bit != 0)
                (void)screen_blit_masked(ds,
                                         (uint16_t)(((uint16_t)r14 << 8) | r12),
                                         cell);
            if (remaining < 8)
                break;
            remaining = (uint8_t)(remaining - 8);
        }
        cell = (uint16_t)(cell + (uint8_t)(24 - written));
        remaining = save;
        if (--rows == 0)
            break;
    }
    return cell;
}

/* 0x09834 - fill the rectangle with ones. */
uint16_t fill_rect_solid(uint8_t *ds, uint16_t xy, uint16_t size)
{
    return fill_rect_pattern(ds, xy, size, 0xFFFFu);
}

/* 0x09826 - the same walk with a zero pattern, i.e. a clear. */
uint16_t fill_rect_clear(uint8_t *ds, uint16_t xy, uint16_t size)
{
    return fill_rect_pattern(ds, xy, size, 0x0000u);
}
