/* draw.h - plotting primitives: coordinate scalers and the line drawer. */
#ifndef NX_DRAW_H
#define NX_DRAW_H

#include <stdint.h>

/* The 0x097xx cluster converts a character cell (column, row) into the pixel
 * coordinates of one of the four corner pixels of that cell.  Each routine
 * takes the cell as (row << 8) | column, subtracts one from both, multiplies
 * by the cell size and adds the corner offset.  Only the low byte of each
 * product survives, exactly as in the ROM. */

/* 0x0970E - bottom-right corner of a 10x42 cell (offset 11, 22). */
uint16_t cell_corner_br(uint8_t *ds, uint16_t cell);

/* 0x09734 - top-right corner of a 12x62 cell (offset 3, 15). */
uint16_t cell_corner_tr(uint8_t *ds, uint16_t cell);

/* 0x0975A - bottom-right corner of a 12x62 cell (offset 15, 15). */
uint16_t cell_corner_br2(uint8_t *ds, uint16_t cell);

/* 0x09780 - the mode-dependent cell origin; row 2 shifts by one cell width
 * and, in submode 2, by a further 12 pixels. */
uint16_t cell_origin(uint8_t *ds, uint16_t cell);

/* 0x09C4C - address of one 10-byte record in the 0xD392 table, from the
 * 4-byte-wide, 16-byte-tall cell grid.  The second argument (r2) is the row
 * index the caller loads from the 0xD0F9 table. */
uint16_t cell_record_addr(uint8_t *ds, uint16_t cell, uint16_t index);

/* 0x08BDE - wipe the 24-byte screen rows from `index` onward (count runs down
 * from 0x0600 by 24 per index). */
uint16_t screen_rows_clear_from(uint8_t *ds, uint16_t index);

/* 0x08E62 - Bresenham line from `start` to `end`, both packed as
 * (y << 8) | x.  Coordinates are signed bytes; the routine plots through
 * 0x091EA, which does no bounds check. */
void draw_line(uint8_t *ds, uint16_t start, uint16_t end);

/* 0x08D00 - two 2-pixel vertical strokes from `y` down to the low byte of
 * `spec`, plus four optional corner groups selected by the high byte:
 * 0x80 right-top, 0x20 left-top, 0x40 right-bottom, 0x10 left-bottom.  The
 * bottom groups use the row the loop stopped on, so the box closes on
 * `spec & 0xFF`.  Plots through 0x091E2 (bounds-checked). */
void box_frame(uint8_t *ds, uint16_t xy, uint16_t spec);

/* 0x08CB8 - the one-pixel frame, same `spec` layout as box_frame. */
void box_frame_thin(uint8_t *ds, uint16_t xy, uint16_t spec);

/* 0x08C7A - shift the rows of buffer 1 up by one, dropping the row at
 * `index`, and zero the tail the move leaves behind. */
uint16_t screen_scroll_rows_up(uint8_t *ds, uint16_t index, uint16_t high);

/* 0x08DC0 - a 2-pixel-thick rounded box: the loop runs from `y + 2` to
 * `(spec & 0xFF) - 2`, clamping the sides on the row that matches
 * `spec >> 8`, and the two closing rows are drawn after it. */
void box_round_rect(uint8_t *ds, uint16_t xy, uint16_t spec);

#endif
