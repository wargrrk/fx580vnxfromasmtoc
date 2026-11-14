/* t_draw.c - differential tests for the plotting primitives. */
#include "batch.h"

#include "../src/draw.h"

W1(cell_corner_br)
W1(cell_corner_tr)
W1(cell_corner_br2)
W1(cell_origin)
W2(cell_record_addr)
W1(screen_rows_clear_from)

static uint16_t a_box_frame(uint8_t *ds, const uint16_t *e, const uint16_t *s)
{
    (void)s;
    box_frame(ds, e[0], e[1]);
    return 0;
}

static uint16_t a_box_round_rect(uint8_t *ds, const uint16_t *e,
                                 const uint16_t *s)
{
    (void)s;
    box_round_rect(ds, e[0], e[1]);
    return 0;
}

static uint16_t a_draw_line(uint8_t *ds, const uint16_t *e, const uint16_t *s)
{
    (void)s;
    draw_line(ds, e[0], e[1]);
    return 0;
}

void test_draw(void)
{
    BATCH(
        {"cell_corner_br", 0x0970E, "w", a_cell_corner_br, 0},
        {"cell_corner_tr", 0x09734, "w", a_cell_corner_tr, 0},
        {"cell_corner_br2", 0x0975A, "w", a_cell_corner_br2, 0},
        {"cell_origin", 0x09780, "w", a_cell_origin, 0},
        {"cell_record_addr", 0x09C4C, "ww", a_cell_record_addr, 0},
        {"screen_rows_clear_from", 0x08BDE, "s", a_screen_rows_clear_from, 0},
        {"box_frame", 0x08D00, "ww!", a_box_frame, 0},
        {"box_round_rect", 0x08DC0, "ww!", a_box_round_rect, 0},
        {"draw_line", 0x08E62, "ww!", a_draw_line, 0},
    );
}
