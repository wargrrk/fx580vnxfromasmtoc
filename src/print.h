/* print.h - character drawing into the packed screen buffers. */
#ifndef NX_PRINT_H
#define NX_PRINT_H

#include <stdint.h>

#include "../src/runtime.h"

/* f_093D2 silently ignores everything at or below this address.  In
 * particular buffer 1's first row (0xDDD4..0xDDEB) is never drawn. */
#define DRAW_MIN_ADDR 0xDDEBu

/* Row source used for the extra trailing row; holds zeroes in the ROM. */
#define BLANK_GLYPH 0x0AE2u

#define SCREEN_ROW_BYTES 24

/* 0x093D2: blend `value` into one screen byte according to the draw mode at
 * 0xD138.  `mask` selects the bits that participate.  `value` is and-ed with
 * `mask` first (unless the mask is zero). */
uint16_t screen_blend(uint8_t *ds, uint16_t addr, uint8_t value, uint8_t mask);
void screen_blend_immediate(uint8_t *ds, uint16_t addr, uint8_t value, uint8_t mask);

/* 0x093C0 - the same merge entered with a packed (value, mask) word and an
 * absolute byte address; returns the r0/r1 pair the ROM has at its `rt`.  The
 * guard here is `0xDDEB >= addr`, so row 0 of buffer 1 is out of reach. */
uint16_t screen_blit_masked(uint8_t *ds, uint16_t value_mask, uint16_t addr);

/* 0x09834 / 0x09826 - fill or clear a rectangle; `xy` is (y << 8) | x and
 * `size` is (h << 8) | w.  Both return the running address the ROM leaves in
 * er0, which is the caller's to ignore. */
uint16_t fill_rect_solid(uint8_t *ds, uint16_t xy, uint16_t size);
uint16_t fill_rect_clear(uint8_t *ds, uint16_t xy, uint16_t size);

/* 0x091E2 / 0x091E6 / 0x091EA - set or clear one pixel in the current buffer.
 * The three entry points share one body: 0x091E2 sets without checking bounds
 * (its `bc al` jumps straight past the range test), 0x091E6 clears a
 * bounds-checked pixel and 0x091EA sets a bounds-checked one. */
void screen_pixel(uint8_t *ds, uint16_t xy, int clear, int check_bounds);

/* 0x094C8 / 0x094BE: address of the byte containing pixel (x, y) in the current
 * packed buffer, plus the bit position (x & 7) as the return value.  The y
 * coordinate is treated as signed, so values >= 0x80 address rows above the
 * buffer.  0x094BE is the same computation for the 32-byte-per-row LCD
 * framebuffer. */
uint16_t screen_addr(uint8_t *ds, uint16_t xy, uint16_t *out);
uint16_t lcd_addr(uint8_t *ds, uint16_t xy, uint16_t *out);

/* 0x09056: draw one character.
 *   xy      (y << 8) | x, in pixels
 *   ch      character code
 *   marker  0 for the normal font; 241/242 select the wide symbol cells
 * The font size and draw mode come from 0xD137/0xD138 and the target buffer
 * from 0xD139.  The glyph is drawn MSB-first, 24 bytes per row; the last row
 * is always drawn from the blank source (see BLANK_GLYPH). */
void char_print(uint8_t *ds, uint16_t xy, uint8_t ch, uint8_t marker);

/* The ROM keeps the decompressed text in the 64-byte frame it allocates at
 * SP-64; this model takes that buffer as an argument so callers can choose
 * where it lives.  str_decompress_print parks strings longer than 34 bytes in
 * the static buffer at 0x1DB4 instead of the frame. */
/* The ROM uses the 64-byte slot it allocates at SP-64: with the default
 * stack pointer that is 0xECB2 (14 bytes of saved state on top of it), so
 * this model takes the buffer as an argument instead. */
#define DECOMP_BUFFER  0xECB2u
#define DECOMP_STATIC  0x1DB4u
#define DECOMP_MAX     34u

/* 0x08F02: expand one level of a nested smart string and print it at (0, 0).
 * Returns the character count from line_print; *end_out gets the pointer just
 * past the source string. */
uint16_t str_decompress_print(uint8_t *ds, uint16_t str, uint16_t buf,
                              uint16_t *end_out);

/* 0x08F7E: print the NUL-terminated string at `str` starting at pixel (x, y).
 * Advances while x stays within the line width; the returned count is the
 * number of printed characters and *str_out receives the pointer the loop
 * stopped at (the terminator when the string ran out). */
uint8_t line_print(uint8_t *ds, uint16_t xy, uint16_t str, uint16_t *str_out);

/* 0x2C3FE - print the label selected by the current mode/submode at row 1. */
uint8_t print_mode_label(uint8_t *ds);

#endif
