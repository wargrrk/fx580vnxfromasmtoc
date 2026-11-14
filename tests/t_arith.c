/* t_arith.c - differential tests for the arithmetic core at 0x27000-0x31000 */#include <string.h>

#include "batch.h"
#include "harness.h"

#include "../src/runtime.h"

W1(row_count_over_limit)
W1(init_descriptor_record)
W2(mul16)
W1(shift_until_carry)
W1(pitch_from_state)
W1(tall_metric_from_font)
W1(small_metric_from_font)
W1(reset_flags_and_state)

static uint16_t a_key_repeat_gate(uint8_t *ds, const uint16_t *e,
                                  const uint16_t *s)
{
    return key_repeat_gate(ds, e[0], s);
}

/* 0x1BB28 / 0x1BB3A / 0x1BB4E / 0x1BB60 shift a run of registers by four bits.
 * Nothing but the register file changes, so the whole test is a comparison
 * between the image the ROM left and the one the C body leaves. */
static void one_shift(uint32_t addr, const char *name,
                      uint16_t (*shift)(uint8_t *, uint8_t *), uint8_t *er,
                      uint16_t mask, uint32_t seed)
{
    uint8_t mine[16];

    random_image(seed);
    Args args = {.nstk = 0};
    for (int i = 0; i < 8; i++)
        args.er[i] = (uint16_t)(er[2 * i] | ((uint16_t)er[2 * i + 1] << 8));
    uint16_t rom_ret = run_rom(addr, &args, name);
    begin_c();
    /* the C body has to start from the *input* image: shifting the ROM's own
     * output a second time is what a first attempt did, and on a uniform byte
     * pattern that quietly maps back onto itself */
    memcpy(mine, er, sizeof(mine));
    uint16_t c_ret = shift(after_c, mine);
    check_ret(name, rom_ret, c_ret);
    check_regs(name, mine, mask);
}

static void test_register_shifts(void)
{
    static const uint16_t masks[] = {0x0001, 0x00FF, 0x0100, 0x7F7F, 0xFF80,
                                     0xA5A5, 0xFFFF, 0xF0F0};

    for (size_t m = 0; m < sizeof(masks) / sizeof(masks[0]); m++) {
        uint8_t er[16];
        uint32_t seed = 0xF0000u + (uint32_t)m;

        for (int b = 0; b < 16; b++)
            er[b] = (uint8_t)(masks[m] >> ((b % 2) * 8));

        one_shift(0x1BB28, "shift_qr0_right4", shift_qr0_right4, er, 0x00FFu,
                  seed + 1);
        one_shift(0x1BB3A, "shift_r1_r9_right4", shift_r1_r9_right4, er,
                  0x03FEu, seed + 2);
        one_shift(0x1BB4E, "shift_qr0_left4", shift_qr0_left4, er, 0x00FFu,
                  seed + 3);
        one_shift(0x1BB60, "shift_r1_r9_left4", shift_r1_r9_left4, er, 0x03FEu,
                  seed + 4);
    }
}


void test_arith(void)
{
    BATCH(
        {"row_count_over_limit", 0x27E60, "b", a_row_count_over_limit, 0},
        {"init_descriptor_record", 0x2F1C4, "p", a_init_descriptor_record, 0},
        {"mul16", 0x1EDC8, "ww", a_mul16, 0},
        {"shift_until_carry", 0x2406C, "w", a_shift_until_carry, 0},
        {"pitch_from_state", 0x2EB00, "w", a_pitch_from_state, 0},
        {"tall_metric_from_font", 0x27E86, "w", a_tall_metric_from_font, 0},
        {"small_metric_from_font", 0x2805E, "w", a_small_metric_from_font, 0},
        {"reset_flags_and_state", 0x2AB3C, "w", a_reset_flags_and_state, 0},
        {"key_repeat_gate", 0x28414, "w|w", a_key_repeat_gate, 0},
    );

    test_register_shifts();

    /* 0x298E2 - the two rows come from the pointer table at 0x07AC2, so run it
     * against every buffer/mode combination the screen path can see: the
     * scratch lives at the top of the stack, which the harness does not
     * compare, and everything else must match byte for byte. */
    for (uint32_t seed = 0; seed < 4; seed++) {
        uint16_t buffer = STACK_TOP;
        random_image(0x298E2u + seed * 7919u);
        image[0xD138] = (uint8_t)(seed & 1);
        image[0xD139] = (uint8_t)((seed >> 1) & 1);
        Args args = {.nstk = 0};
        run_rom(0x298E2, &args, "print_indexed_rows");
        begin_c();
        print_indexed_rows(after_c, buffer);
        check_mem_all("print_indexed_rows");
    }

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
