/* t_arith.c - differential tests for the arithmetic core at 0x27000-0x31000 */
#include "batch.h"
#include "harness.h"

#include "../src/runtime.h"

W1(row_count_over_limit)
W1(init_descriptor_record)
W1(pitch_from_state)
W1(tall_metric_from_font)
W1(small_metric_from_font)
W1(reset_flags_and_state)

static uint16_t a_key_repeat_gate(uint8_t *ds, const uint16_t *e,
                                  const uint16_t *s)
{
    return key_repeat_gate(ds, e[0], s);
}

void test_arith(void)
{
    BATCH(
        {"row_count_over_limit", 0x27E60, "b", a_row_count_over_limit, 0},
        {"init_descriptor_record", 0x2F1C4, "p", a_init_descriptor_record, 0},
        {"pitch_from_state", 0x2EB00, "w", a_pitch_from_state, 0},
        {"tall_metric_from_font", 0x27E86, "w", a_tall_metric_from_font, 0},
        {"small_metric_from_font", 0x2805E, "w", a_small_metric_from_font, 0},
        {"reset_flags_and_state", 0x2AB3C, "w", a_reset_flags_and_state, 0},
        {"key_repeat_gate", 0x28414, "w|w", a_key_repeat_gate, 0},
    );

    /* 0x28414 - the four repeat-key codes are the only interesting ones, so
     * walk them against every low byte the caller can supply and a row byte on
     * both sides of the limit.  0xFC1D divides by the low byte, so zero and one
     * have to be in the sweep as well as the values that divide 11 evenly. */
    {
        static const uint16_t codes[] = {0xFC1C, 0xFC1D, 0xFC1E, 0xFC1F,
                                         0x0000, 0xFC1B, 0xFFFF};
        static const uint8_t his[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12,
                                      13, 22, 23, 127, 128, 200, 255};

        for (size_t c = 0; c < sizeof(codes) / sizeof(codes[0]); c++) {
            for (unsigned lo = 0; lo < 24; lo++) {
                for (size_t h = 0; h < sizeof(his) / sizeof(his[0]); h++) {
                    random_image((uint32_t)(0x28414u + c * 977u + lo * 31u +
                                            h));
                    image[0x3EA0] = 11;
                    Args args = {.er = {(uint16_t)(lo | (his[h] << 8))},
                                 .stk = {codes[c]}, .nstk = 1};
                    uint16_t rom_ret = run_rom(0x28414, &args,
                                               "key_repeat_gate");
                    begin_c();
                    check_ret("key_repeat_gate", rom_ret,
                              key_repeat_gate(after_c, args.er[0], args.stk));
                    check_mem_all("key_repeat_gate");
                }
            }
        }
    }

    /* 0x27E60 - the limit is 181, so pin the count byte on both sides of the
     * boundary the flag turns over at */
    for (unsigned count = 0; count < 256; count += 15) {
        for (unsigned add = 0; add < 256; add += 51) {
            random_image((uint32_t)(0x27E60u + count * 256u + add));
            image[0xD143] = (uint8_t)count;
            Args args = {.er = {(uint16_t)add}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x27E60, &args, "row_count_over_limit");
            begin_c();
            check_ret("row_count_over_limit", rom_ret,
                      row_count_over_limit(after_c, (uint8_t)add));
            check_mem_all("row_count_over_limit");
        }
    }

    /* 0x2F62E - only submodes 1..3 draw, so walk the boundary and one value on
     * each side of it instead of waiting for a random byte to land there */
    {
        static const uint8_t submodes[] = {0, 1, 2, 3, 4, 255};
        for (size_t i = 0; i < sizeof(submodes) / sizeof(submodes[0]); i++) {
            random_image((uint32_t)(0x2F62Eu + i));
            image[0xD112] = submodes[i];
            Args args = {.nstk = 0};
            run_rom(0x2F62E, &args, "draw_submode_box");
            begin_c();
            draw_submode_box(after_c);
            check_mem_all("draw_submode_box");
        }
    }

    /* 0x2E812 - the index gates two of the strokes, so walk all four cases the
     * ROM distinguishes instead of relying on random bytes for them */
    for (unsigned index = 0; index < 6; index++) {
        random_image((uint32_t)(0x2E812u + index));
        Args args = {.er = {(uint16_t)index}, .nstk = 0};
        run_rom(0x2E812, &args, "draw_bar_frames");
        begin_c();
        draw_bar_frames(after_c, (uint8_t)index);
        check_mem_all("draw_bar_frames");
    }
}
