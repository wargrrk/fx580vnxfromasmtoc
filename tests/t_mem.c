/* t_mem.c - differential tests for the block memory cluster. */
#include "batch.h"
#include "harness.h"

#include <string.h>

#include "../src/runtime.h"

W2(max_u16)
W1(clear_160_bytes)
W1(clear_200_bytes)
W0(clear_block_d6b2)
W0(record10_d046_to_d010)
W0(record10_d010_to_d046)
W0(record10_d000_to_d046)
W0(record10_d03c_to_d000)
W0(record10_d03c_to_d010)
W0(record10_d000_to_d03c)
W0(record10_d050_to_d010)
W0(record10_d000_to_d050)
W0(record10_d030_to_d010)
W0(record10_d000_to_d030)
W0(record10_d000_to_d010)
W0(record10_d010_to_d000)
W0(record10_d010_to_d050)
W0(record10_d050_to_d000)
W0(record10_d030_to_d000)
W0(record10_d010_to_d030)
W0(record_high_byte_add5)
W1(digit_0D009)
W1(digit_at)

static void test_memcpy(void)
{
    static const uint16_t ns[] = {0, 1, 2, 3, 7, 16, 31, 64, 200, 512};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (int overlap = 0; overlap < 3; overlap++) {
            random_image((uint32_t)(t * 3 + overlap + 500));
            uint16_t dst = 0xD200, src = 0xD600;
            if (overlap == 1) { dst = 0xD200; src = 0xD210; }
            if (overlap == 2) { dst = 0xD210; src = 0xD200; }
            uint16_t n = ns[t];
            Args a = {.er = {dst, src}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x26FFA, &a, "memcpy_nn");
            begin_c();
            uint16_t c = memcpy_nn(after_c, dst, src, n);
            check_ret("memcpy_nn", r, c);
            check_mem_all("memcpy_nn");
        }
    }
}

static void test_memmove(void)
{
    static const uint16_t ns[] = {0, 1, 2, 3, 7, 16, 31, 64, 200};
    static const int offs[][2] = {
        {0, 0}, {0x200, 0x200}, {0x200, 0x210}, {0x210, 0x200},
        {0x200, 0x208}, {0x208, 0x200}, {0x200, 0x600}, {0x600, 0x200},
    };
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (size_t k = 0; k < sizeof(offs) / sizeof(offs[0]); k++) {
            random_image((uint32_t)(t * 17 + k + 600));
            uint16_t dst = (uint16_t)(0xD000 + offs[k][0]);
            uint16_t src = (uint16_t)(0xD000 + offs[k][1]);
            uint16_t n = ns[t];
            Args a = {.er = {dst, src}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x0A8AA, &a, "memmove_nn");
            begin_c();
            uint16_t c = memmove_nn(after_c, dst, src, n);
            check_ret("memmove_nn", r, c);
            check_mem_all("memmove_nn");
        }
    }
}

static void test_memset(void)
{
    static const uint16_t ns[] = {0, 1, 2, 5, 16, 64, 300};
    static const uint8_t vals[] = {0x00, 0xFF, 0x5A};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (size_t v = 0; v < sizeof(vals) / sizeof(vals[0]); v++) {
            random_image((uint32_t)(t * 3 + v + 700));
            uint16_t dst = 0xD200;
            uint16_t n = ns[t];
            Args a = {.er = {dst, vals[v]}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x1EB94, &a, "memset_n");
            begin_c();
            uint16_t c = memset_n(after_c, dst, vals[v], n);
            check_ret("memset_n", r, c);
            check_mem_all("memset_n");
        }
    }
}

static void test_memzero(void)
{
    static const uint16_t ns[] = {0, 1, 2, 5, 16, 64, 300};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        random_image((uint32_t)(t + 800));
        uint16_t dst = 0xD200;
        uint16_t n = ns[t];
        Args a = {.er = {dst, n}, .nstk = 0};
        uint16_t r = run_rom(0x09D34, &a, "memzero");
        begin_c();
        uint16_t c = memzero(after_c, dst, n);
        check_ret("memzero", r, c);
        check_mem_all("memzero");
    }
}

/* Counts with the top bit set: the ROM's `cmp 0, n ; bc lt` takes the branch
 * for every non-zero 16-bit count, i.e. the count is effectively unsigned.
 * The stack is moved to 0xD100 so the copy ranges do not overwrite it. */
static void test_count_edges(void)
{
    random_image(900);
    Args a = {.er = {0xD200, 0xD600}, .stk = {0x8000}, .nstk = 1, .sp = 0xD100};
    uint16_t r1 = run_rom(0x26FFA, &a, "memcpy_nn(0x8000)");
    begin_c();
    uint16_t c1 = memcpy_nn(after_c, 0xD200, 0xD600, 0x8000);
    check_ret("memcpy_nn(0x8000)", r1, c1);
    check_mem("memcpy_nn(0x8000)", 0xD200, CMP_END);

    random_image(901);
    Args b = {.er = {0xD200, 0x5A}, .stk = {0xF000}, .nstk = 1, .sp = 0xD100};
    uint16_t r2 = run_rom(0x1EB94, &b, "memset_n(0xF000)");
    begin_c();
    uint16_t c2 = memset_n(after_c, 0xD200, 0x5A, 0xF000);
    check_ret("memset_n(0xF000)", r2, c2);
    check_mem("memset_n(0xF000)", 0xD200, CMP_END);
}

static void test_shift_range_offsets(void)
{
    static const uint8_t lowers[] = {0, 10, 50, 200};
    static const uint8_t starts[] = {0, 1, 3};
    static const uint16_t deltas[] = {1, 0x7FFF, 0xFFFF};

    for (size_t l = 0; l < sizeof(lowers); l++) {
        for (size_t s = 0; s < sizeof(starts); s++) {
            for (size_t d = 0; d < sizeof(deltas) / sizeof(deltas[0]); d++) {
                random_image(0xB1000u + (uint32_t)(l * 100 + s * 10 + d));
                image[0xD169] = 6;
                for (uint8_t index = 0; index < 6; index++) {
                    uint16_t entry = (uint16_t)(0xDBE4u + (uint16_t)index * 8);
                    image[entry] = (uint8_t)(0xE0 + index * 13);
                    image[entry + 1] = (uint8_t)(0x30 + index);
                    image[entry + 2] = (uint8_t)(index * 40);
                    image[entry + 3] = 180;
                }
                image[0xDBE7u + (uint16_t)starts[s] * 8] = 180;

                Args args = {
                    .er = {deltas[d], (uint16_t)(lowers[l] | (starts[s] << 8))},
                    .nstk = 0,
                };
                run_rom(0x2F832, &args, "shift_range_offsets");
                begin_c();
                shift_range_offsets(after_c, deltas[d], lowers[l], starts[s]);
                check_mem("shift_range_offsets", 0xDBE4, 0xDC14);
            }
        }
    }
}

static void test_compare_bcd_records(void)
{
    static const uint8_t first_values[] = {0, 1, 4, 5, 9};
    static const uint8_t second_values[] = {0, 1, 4, 5, 9};
    for (size_t a = 0; a < sizeof(first_values); a++) {
        for (size_t b = 0; b < sizeof(second_values); b++) {
            random_image(0xB2000u + (uint32_t)(a * 10 + b));
            for (uint16_t i = 0; i < 10; i++) {
                uint8_t digit_a = (uint8_t)((a * 3 + i) % 10);
                uint8_t digit_b = (uint8_t)((b * 2 + i) % 10);
                image[0xD000 + i] = (uint8_t)((digit_a << 4) | digit_a);
                image[0xD010 + i] = (uint8_t)((digit_b << 4) | digit_b);
            }
            image[0xD001] = first_values[a];
            image[0xD011] = second_values[b];
            image[0xD009] = 0x10;
            image[0xD019] = 0xE0;
            Args args = {.nstk = 0};
            uint16_t rom_ret = run_rom(0x1CA78, &args, "compare_bcd_records");
            begin_c();
            uint8_t c_ret = compare_bcd_records(after_c);
            check_ret("compare_bcd_records", (uint16_t)(rom_ret & 0xFF), c_ret);
            check_mem("compare_bcd_records", 0xD000, 0xD01B);
        }
    }

    for (uint8_t invalid = 0; invalid < 2; invalid++) {
        random_image(0xB2010u + invalid);
        image[0xD009] = invalid ? 0xF0 : 0x10;
        image[0xD019] = invalid ? 0xF0 : 0xE0;
        Args args = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x1CA78, &args, "compare_bcd_records invalid");
        begin_c();
        uint8_t c_ret = compare_bcd_records(after_c);
        check_ret("compare_bcd_records invalid", (uint16_t)(rom_ret & 0xFF), c_ret);
        check_mem("compare_bcd_records invalid", 0xD000, 0xD01B);
    }
}

static void test_decimal_record_predicate(void)
{
    static const uint8_t statuses[] = {0, 1, 5, 9, 10, 0xF0};
    static const uint8_t lows[] = {0, 1, 5, 9, 10, 0x99};
    static const uint8_t highs[] = {0, 4, 5, 6, 9};

    for (size_t s = 0; s < sizeof(statuses); s++) {
        for (size_t lo = 0; lo < sizeof(lows); lo++) {
            for (size_t hi = 0; hi < sizeof(highs); hi++) {
                for (uint8_t fill = 0; fill < 2; fill++) {
                    random_image(0xB3000u + (uint32_t)(s * 100 + lo * 10 + hi * 2 + fill));
                    memset(image + 0xD002, fill ? 0x10 : 0x00, 8);
                    image[0xD000] = lows[lo];
                    image[0xD001] = highs[hi];
                    image[0xD009] = statuses[s];
                    Args args = {.nstk = 0};
                    uint16_t rom_ret = run_rom(0x19FD8, &args, "decimal_record_predicate");
                    begin_c();
                    uint8_t c_ret = decimal_record_predicate(after_c);
                    check_ret("decimal_record_predicate", (uint16_t)(rom_ret & 0xFF), c_ret);
                    check_mem("decimal_record_predicate", 0xD000, 0xD01A);
                }
            }
        }
    }
}

/* assign_var stores two fixed-width halfs; the mode byte at 0xD111 turns the
 * second move on (69 = cmp? / 196 = the matrix-ish dispatch), slots run 0-12
 * with the ROM taking any 8-bit index */
static void test_assign_var(void)
{
    static const uint8_t modes[] = {56, 1, 69, 196};
    static const uint8_t slots[] = {0, 1, 2, 5, 11, 12, 20};

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        for (size_t s = 0; s < sizeof(slots) / sizeof(slots[0]); s++) {
            uint8_t slot = slots[s];

            random_image(700u + m * 20 + s);
            image[0xD111] = modes[m];
            image[0xE010] = (uint8_t)(0x11 ^ slot);     /* values, first half */
            image[0xE01A] = (uint8_t)(0x22 ^ slot);     /* values, second half */
            Args a = {.er = {slot, 0xE010}, .nstk = 0};
            uint16_t r = run_rom(0x09DE6, &a, "assign_var");
            begin_c();
            uint16_t c = assign_var(after_c, slot, 0xE010);
            check_ret("assign_var", r, c);
            check_mem("assign_var", 0xD300, 0xD6E0);
        }
    }
}

/* 0x09D62 parks er0 at 0xD10E; 0x09E6E answers only in mode 193 (and keeps
 * the incoming high byte); 0x09E24 walks the descriptor table 0x38E6 in ROM
 * with the submode/skew/cell rules. */
static void test_var_helpers(void)
{
    static const uint16_t words[] = {0, 1, 0x1234, 0x8000, 0xABCD, 0xFFFF};
    static const uint8_t modes[] = {0, 56, 69, 193, 196, 255};
    static const uint8_t subs[] = {0, 1, 3, 4, 7, 0x80, 0xFF};

    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        random_image(800u + (unsigned)i);
        Args a = {.er = {words[i], 0}, .nstk = 0};
        run_rom(0x09D62, &a, "store_0D10E");
        begin_c();
        store_0D10E(after_c, words[i]);
        check_mem("store_0D10E", 0xD100, 0xD120);
    }

    /* a few different incoming high bytes: the ROM keeps er0[15:8] */
    for (size_t i = 0; i < 4; i++) {
        for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
            uint16_t arg = (uint16_t)((0x3400u + i * 0x100u) | 7u);

            random_image(810u + (unsigned)(i * 10 + m));
            image[0xD111] = modes[m];
            image[0xD123] = (uint8_t)(0x33 + i);
            Args a = {.er = {arg, 0}, .nstk = 0};
            uint16_t rr = run_rom(0x09E6E, &a, "mode193_value");
            begin_c();
            uint16_t cc = mode193_value(after_c, arg);
            check_ret("mode193_value", rr, cc);
            check_mem("mode193_value", 0xD100, 0xD130);
        }
    }

    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        random_image(820u + (unsigned)i);
        image[0xD112] = subs[i];
        image[0xD14F] = (uint8_t)(0x10 + i);      /* non-zero: skew path */
        image[0xD115] = (uint8_t)(3 + i * 7);
        Args a = {.er = {0x9A2Bu, 0}, .nstk = 0};
        uint16_t rr = run_rom(0x09E24, &a, "var_mode_attr skew");
        begin_c();
        check_ret("var_mode_attr skew", (uint16_t)(rr & 0xFF),
                  var_mode_attr(after_c));
        check_mem("var_mode_attr skew", 0xD100, 0xD160);
    }

    /* and with the skew byte at zero: no shift, plain submode - 1 */
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        random_image(830u + (unsigned)i);
        image[0xD112] = subs[i];
        image[0xD14F] = 0;                        /* skew disabled */
        image[0xD115] = (uint8_t)(0x40 + i);
        Args a = {.er = {0x9A2Bu, 0}, .nstk = 0};
        uint16_t rr = run_rom(0x09E24, &a, "var_mode_attr plain");
        begin_c();
        check_ret("var_mode_attr plain", (uint16_t)(rr & 0xFF),
                  var_mode_attr(after_c));
        check_mem("var_mode_attr plain", 0xD100, 0xD160);
    }
}

static void test_init_numeric_state(void)
{
    static const uint8_t selectors[] = {0, 4, 5, 6, 255};
    for (size_t i = 0; i < sizeof(selectors); i++) {
        random_image(840u + (unsigned)i);
        image[0xD02F] = selectors[i];
        Args args = {.nstk = 0};
        run_rom(0x1B124, &args, "init_numeric_state");
        begin_c();
        init_numeric_state(after_c);
        check_mem("init_numeric_state", 0xD010, 0xD01A);
    }
}

static void test_small_leaves(void)
{
    BATCH(
        {"max_u16", 0x2ABDA, "ww", a_max_u16, 0},
        {"clear_160_bytes", 0x143CA, "p", a_clear_160_bytes, 0},
        {"clear_200_bytes", 0x14C18, "p", a_clear_200_bytes, 0},
        {"clear_block_d6b2", 0x15BF8, "", a_clear_block_d6b2, 0},
    );
}

/* The sixteen ten-byte record moves at 0x19E54..0x19F62, plus the packed-digit
 * helpers.  Every one of them is fixed-address, so the batch runner's random
 * images are the whole input domain. */
static void test_record10_moves(void)
{
    BATCH(
        {"record10_d046_to_d010", 0x19E54, "", a_record10_d046_to_d010, 0},
        {"record10_d010_to_d046", 0x19E66, "", a_record10_d010_to_d046, 0},
        {"record10_d000_to_d046", 0x19E78, "", a_record10_d000_to_d046, 0},
        {"record10_d03c_to_d000", 0x19E8A, "", a_record10_d03c_to_d000, 0},
        {"record10_d03c_to_d010", 0x19E9C, "", a_record10_d03c_to_d010, 0},
        {"record10_d000_to_d03c", 0x19EAE, "", a_record10_d000_to_d03c, 0},
        {"record10_d050_to_d010", 0x19EC0, "", a_record10_d050_to_d010, 0},
        {"record10_d000_to_d050", 0x19ED2, "", a_record10_d000_to_d050, 0},
        {"record10_d030_to_d010", 0x19EE4, "", a_record10_d030_to_d010, 0},
        {"record10_d000_to_d030", 0x19EF6, "", a_record10_d000_to_d030, 0},
        {"record10_d000_to_d010", 0x19F08, "", a_record10_d000_to_d010, 0},
        {"record10_d010_to_d000", 0x19F1A, "", a_record10_d010_to_d000, 0},
        {"record10_d010_to_d050", 0x19F2C, "", a_record10_d010_to_d050, 0},
        {"record10_d050_to_d000", 0x19F3E, "", a_record10_d050_to_d000, 0},
        {"record10_d030_to_d000", 0x19F50, "", a_record10_d030_to_d000, 0},
        {"record10_d010_to_d030", 0x19F62, "", a_record10_d010_to_d030, 0},
        {"record_high_byte_add5", 0x1B286, "", a_record_high_byte_add5, 0},
        {"digit_0D009", 0x1C6AA, "w", a_digit_0D009, 0},
        {"digit_at", 0x1DAE2, "p", a_digit_at, 0},
    );
}

/* 0x17CA6 takes both addresses from er12/er14, and 0x13B86/0x14BCC read the
 * r12/r13 pair plus er4 - registers the shape language cannot name, so these
 * run by hand.  The odd pointers are there on purpose: both accesses are
 * multi-byte, so the core ignores address bit 0. */
static void test_record10_from_frame(void)
{
    static const uint16_t pairs[][2] = {
        {0xD200, 0xD400}, {0xD201, 0xD401}, {0xD200, 0xD201},
        {0xD400, 0xD3F8}, {0xDC00, 0xD200}, {0xE000, 0xDF00},
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        random_image((uint32_t)(0xC1000 + i));
        Args a = {.er = {0, 0, 0, 0, 0, 0, pairs[i][0], pairs[i][1]}};
        uint16_t rom_ret = run_rom(0x17CA6, &a, "record10_from_frame");
        begin_c();
        uint16_t c_ret = record10_from_frame(after_c, pairs[i][0], pairs[i][1]);
        check_ret("record10_from_frame", rom_ret, c_ret);
        check_mem_all("record10_from_frame");
    }
}

static void test_record10_index(void)
{
    static const uint16_t bases[] = {0xDA40, 0xD000, 0xFFFE, 0x8000};
    static const uint16_t biases[] = {
        0, 1, 9, 10, 0x0B, 0x20, 0x40, 0x63,
        0x64, 0xFF, 0x0100, 0x010B, 0x8000, 0xFFFF,
    };

    for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        for (size_t k = 0; k < sizeof(biases) / sizeof(biases[0]); k++) {
            for (int stride = 0; stride < 2; stride++) {
                random_image((uint32_t)(0xC2000 + b * 100 + k * 2 + stride));
                Args a = {.er = {0, 0, bases[b], 0, 0, 0, biases[k], 0}};
                uint32_t addr = stride ? 0x14BCC : 0x13B86;
                uint16_t rom_ret = run_rom(addr, &a, "record10_index");
                uint16_t rom_off = nx8_er(harness_machine(), 2);
                uint16_t c_off = 0;
                begin_c();
                uint16_t c_ret = stride
                    ? record10_index_5x(after_c, bases[b], biases[k], &c_off)
                    : record10_index_4x(after_c, bases[b], biases[k], &c_off);
                check_ret("record10_index", rom_ret, c_ret);
                check_ret("record10_index er2", rom_off, c_off);
                check_mem_all("record10_index");
            }
        }
    }
}

/* 0x16070 answers in two registers and touches no memory, so the second half
 * has to be read back from the machine instead of from the image. */
static void test_complement32(void)
{
    static const uint16_t lows[] = {0, 1, 0xFFFF, 0xFFFE, 0x8000, 0x1234, 0x00FF};
    static const uint16_t highs[] = {0, 1, 0xFFFF, 0x7FFF, 0x0100};

    for (size_t lo = 0; lo < sizeof(lows) / sizeof(lows[0]); lo++) {
        for (size_t hi = 0; hi < sizeof(highs) / sizeof(highs[0]); hi++) {
            random_image((uint32_t)(0xC3000 + lo * 10 + hi));
            Args a = {.er = {lows[lo], highs[hi], 0, 0}};
            uint16_t rom_ret = run_rom(0x16070, &a, "complement32");
            uint16_t rom_high = nx8_er(harness_machine(), 2);
            uint16_t c_high = 0;
            begin_c();
            uint16_t c_ret = complement32(after_c, lows[lo], highs[hi], &c_high);
            check_ret("complement32", rom_ret, c_ret);
            check_ret("complement32 er2", rom_high, c_high);
            check_mem_all("complement32");
        }
    }
}

/* 0x09E84 answers out of the stack pointer, which the harness can place. */
static void test_stack_bytes_used(void)
{
    static const uint16_t sps[] = {0xED00, 0xEC80, 0xE800, 0xE3E4};

    for (size_t i = 0; i < sizeof(sps) / sizeof(sps[0]); i++) {
        random_image((uint32_t)(0xC4000 + i));
        Args a = {.sp = sps[i]};
        uint16_t rom_ret = run_rom(0x09E84, &a, "stack_bytes_used");
        begin_c();
        check_ret("stack_bytes_used", rom_ret,
                  stack_bytes_used(after_c, sps[i]));
        check_mem_all("stack_bytes_used");
    }
}

/* 0x23F8A writes sixteen bytes into the frame - inside the 0xEC00..0xF000
 * window check_mem_all() skips - so the range is compared explicitly.  Its
 * input is the register file itself, which no shape can describe. */
static void test_store_reg_to_stack(void)
{
    static const uint16_t frames[] = {0xED00, 0xECD0, 0xEC81, 0xE900, 0xED01};

    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        uint16_t er[8] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x1111, 0x2222, 0x3333,
                          frames[i]};
        uint8_t regs[16];
        Args a = {.nstk = 0};

        for (int k = 0; k < 8; k++) {
            regs[2 * k] = (uint8_t)(er[k] & 0xFF);
            regs[2 * k + 1] = (uint8_t)(er[k] >> 8);
        }
        memcpy(a.er, er, sizeof(er));

        random_image((uint32_t)(0xC5000 + i));
        uint16_t rom_ret = run_rom(0x23F8A, &a, "store_reg_to_stack");
        begin_c();
        uint16_t c_ret = store_reg_to_stack(after_c, frames[i], regs);
        check_ret("store_reg_to_stack", rom_ret, c_ret);
        check_mem("store_reg_to_stack", (uint16_t)(frames[i] - 16),
                  (uint16_t)(frames[i] - 16 + 16));
        check_regs("store_reg_to_stack", regs, 0xFFFFu);
    }
}

/* 0x143D6 leaves nothing in er0 and nothing in memory: the answer is in r5 and
 * r2, so the register file has to be compared directly. */
/* 0x09D16 - the copy only happens when neither address is zero. */
static void test_record10_copy_if_present(void)
{
    static const uint16_t addrs[] = {0, 0xD000, 0xD002, 0xD005, 0xD00A,
                                     0xD010, 0xD3F6};

    for (size_t d = 0; d < sizeof(addrs) / sizeof(addrs[0]); d++) {
        for (size_t s = 0; s < sizeof(addrs) / sizeof(addrs[0]); s++) {
            random_image(0xD4000u + (unsigned)(d * 20 + s));
            Args a = {.er = {addrs[d], addrs[s]}};
            uint16_t rom_ret = run_rom(0x09D16, &a, "record10_copy_if_present");
            begin_c();
            uint16_t c_ret = record10_copy_if_present(after_c, addrs[d],
                                                      addrs[s]);
            check_ret("record10_copy_if_present", rom_ret, c_ret);
            check_mem_all("record10_copy_if_present");
        }
    }
}

/* 0x14FC6 answers in er10 and touches nothing else. */
static void test_base_plus_10x_index(void)
{
    static const uint8_t indexes[] = {0, 1, 2, 7, 25, 26, 100, 255};
    static const uint16_t bases[] = {0, 1, 0xD000, 0xD002, 0xFFF0, 0xFFFF};

    for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); i++) {
        for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
            uint8_t mine[16];
            uint16_t er10 = 0;

            random_image(0xD5000u + (unsigned)(i * 10 + b));
            Args a = {.er = {0, 0, 0, bases[b], 0, 0, indexes[i], 0}};
            uint16_t rom_ret = run_rom(0x14FC6, &a, "base_plus_10x_index");
            begin_c();
            uint16_t c_ret = base_plus_10x_index(after_c, indexes[i], bases[b],
                                                &er10);
            check_ret("base_plus_10x_index", rom_ret, c_ret);
            memcpy(mine, after_rom_regs, sizeof(mine));
            mine[10] = (uint8_t)(er10 & 0xFF);
            mine[11] = (uint8_t)(er10 >> 8);
            check_regs("base_plus_10x_index", mine, 0x0C00u);
            check_mem_all("base_plus_10x_index");
        }
    }
}

/* 0x13B96 loads a word at base + 2*index (forced even) and answers 9 in r2. */
static void test_word_at_2x_index(void)
{
    static const uint8_t indexes[] = {0, 1, 2, 3, 8, 64, 127, 255};
    static const uint16_t bases[] = {0, 1, 3, 0xD000, 0xD001, 0xD0FE, 0xFF00};

    for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); i++) {
        for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
            uint8_t mine[16];
            uint8_t r2 = 0;

            random_image(0xD6000u + (unsigned)(i * 10 + b));
            Args a = {.er = {0, bases[b], 0, indexes[i]}};
            uint16_t rom_ret = run_rom(0x13B96, &a, "word_at_2x_index");
            begin_c();
            uint16_t c_ret = word_at_2x_index(after_c, bases[b], indexes[i],
                                              &r2);
            check_ret("word_at_2x_index", rom_ret, c_ret);
            memcpy(mine, after_rom_regs, sizeof(mine));
            mine[2] = r2;
            check_regs("word_at_2x_index", mine, 0x0004u);
            check_mem_all("word_at_2x_index");
        }
    }
}

/* 0x17E52 unpacks a nibble pair out of the record at er12 into r10/r11. */
static void test_nibble_pair_shifted(void)
{
    static const uint8_t bytes[] = {0x00, 0x01, 0x0F, 0x10, 0x5A, 0x8F, 0xFF};
    static const uint8_t flags[] = {0x00, 0x01, 0x10, 0xFF};
    static const uint16_t bases[] = {0xD000, 0xD001, 0xD100, 0xEC00};

    for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        for (size_t v = 0; v < sizeof(bytes) / sizeof(bytes[0]); v++) {
            for (size_t f = 0; f < sizeof(flags) / sizeof(flags[0]); f++) {
                uint16_t base = bases[b];
                uint8_t mine[16];
                uint8_t r10 = 0, r11 = 0;

                random_image(0xD7000u + (unsigned)(b * 100 + v * 10 + f));
                image[base] = bytes[v];
                image[base + 1] = bytes[(v + 3) % sizeof(bytes)];
                image[base + 8] = flags[f];
                Args a = {.er = {0, 0, 0, 0, 0, 0, base, 0}};
                uint16_t rom_ret = run_rom(0x17E52, &a, "nibble_pair_shifted");
                begin_c();
                uint16_t c_ret = nibble_pair_shifted(after_c, base, &r10, &r11);
                check_ret("nibble_pair_shifted", rom_ret, c_ret);
                memcpy(mine, after_rom_regs, sizeof(mine));
                mine[10] = r10;
                mine[11] = r11;
                check_regs("nibble_pair_shifted", mine, 0x0C00u);
                check_mem_all("nibble_pair_shifted");
            }
        }
    }
}

/* 0x1CAFC zeroes the accumulator record whatever was there before. */
static void test_record_reset_flag38(void)
{
    for (int i = 0; i < 16; i++) {
        random_image(0xD8000u + (uint32_t)i);
        /* bias the record so a run that skipped the stores would show up */
        for (int b = 0; b < 10; b++)
            image[0xD000 + b] = (uint8_t)(0xF0 | b);
        Args a = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x1CAFC, &a, "record_reset_flag38");
        begin_c();
        uint16_t c_ret = record_reset_flag38(after_c);
        check_ret("record_reset_flag38", rom_ret, c_ret);
        check_mem("record_reset_flag38", 0xD000, 0xD020);
        check_mem_all("record_reset_flag38");
    }
}

/* 0x1C63E loads 0xD010 from the second argument before 0xD000 from the first,
 * so a source that overlaps the records sees the first half's writes. */
static void test_records_from_pointers(void)
{
    static const uint16_t srcs[] = {0xD000, 0xD001, 0xD002, 0xD008, 0xD010,
                                    0xD012, 0xD01A, 0xD100, 0xEC00};

    for (size_t a = 0; a < sizeof(srcs) / sizeof(srcs[0]); a++) {
        for (size_t b = 0; b < sizeof(srcs) / sizeof(srcs[0]); b++) {
            random_image(0xD9000u + (unsigned)(a * 20 + b));
            Args args = {.er = {srcs[a], srcs[b]}};
            uint16_t rom_ret = run_rom(0x1C63E, &args, "records_from_pointers");
            begin_c();
            uint16_t c_ret = records_from_pointers(after_c, srcs[a], srcs[b]);
            check_ret("records_from_pointers", rom_ret, c_ret);
            check_mem_all("records_from_pointers");
        }
    }
}

/* 0x1C674 writes ten single bytes, so an odd destination stays odd. */
static void test_record_to_pointer(void)
{
    static const uint16_t dests[] = {0xD000, 0xD001, 0xD002, 0xD00F, 0xD010,
                                     0xD100, 0xD3F7, 0xEBFF};

    for (size_t d = 0; d < sizeof(dests) / sizeof(dests[0]); d++) {
        random_image(0xDA000u + (unsigned)d);
        image[0xD05C] = (uint8_t)(dests[d] & 0xFF);
        image[0xD05D] = (uint8_t)(dests[d] >> 8);
        Args args = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x1C674, &args, "record_to_pointer");
        begin_c();
        uint16_t c_ret = record_to_pointer(after_c);
        check_ret("record_to_pointer", rom_ret, c_ret);
        check_mem_all("record_to_pointer");
    }
}

/* 0x13ACC copies from er4 to er8; both are evened out by the copy itself. */
static void test_record10_er8_from_er4(void)
{
    static const uint16_t addrs[] = {0xD000, 0xD001, 0xD002, 0xD010, 0xD012,
                                     0xD100, 0xEC00};

    for (size_t a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
        for (size_t b = 0; b < sizeof(addrs) / sizeof(addrs[0]); b++) {
            random_image(0xDB000u + (unsigned)(a * 20 + b));
            Args args = {.er = {0, 0, addrs[a], 0, addrs[b]}};
            uint16_t rom_ret = run_rom(0x13ACC, &args, "record10_er8_from_er4");
            begin_c();
            uint16_t c_ret = record10_er8_from_er4(after_c, addrs[a], addrs[b]);
            check_ret("record10_er8_from_er4", rom_ret, c_ret);
            check_mem_all("record10_er8_from_er4");
        }
    }
}

/* 0x17FDE zeroes thirty bytes at er14 and sets +16 and +29; an odd frame start
 * rounds down, so the flags land one byte earlier than the argument says. */
static void test_init_struct_30(void)
{
    static const uint16_t frames[] = {0xD000, 0xD001, 0xD010, 0xD021, 0xD100,
                                      0xEC00, 0xEC01};

    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        random_image(0xDC000u + (uint32_t)i);
        for (int b = 0; b < 32; b++)
            image[(uint16_t)((frames[i] & 0xFFFEu) + b)] = 0x5A;
        Args args = {.er = {0, 0, 0, 0, 0, 0, 0, frames[i]}};
        uint16_t rom_ret = run_rom(0x17FDE, &args, "init_struct_30");
        begin_c();
        uint16_t c_ret = init_struct_30(after_c, frames[i]);
        check_ret("init_struct_30", rom_ret, c_ret);
        check_mem_all("init_struct_30");
    }
}

/* 0x1B682 masks the top nibble of 0xD009 and answers 1 unless it is 0 or 0x20. */
static void test_record_kind_flag(void)
{
    static const uint8_t kinds[] = {0x00, 0x10, 0x20, 0x2F, 0x30, 0x5A, 0xF0, 0xFF};
    static const uint16_t args[] = {0, 0x1234, 0xFF00};

    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); a++) {
            random_image(0xDD000u + (unsigned)(k * 10 + a));
            image[0xD009] = kinds[k];
            Args args_in = {.er = {args[a], 0}};
            uint16_t rom_ret = run_rom(0x1B682, &args_in, "record_kind_flag");
            begin_c();
            uint16_t c_ret = record_kind_flag(after_c, args[a]);
            check_ret("record_kind_flag", rom_ret, c_ret);
            check_mem_all("record_kind_flag");
        }
    }
}

/* 0x29490 answers the decoded digit below 15, otherwise the byte just after the
 * pointer. */
static void test_digit_or_next_byte(void)
{
    static const uint8_t bytes[] = {0x00, 0x05, 0x0E, 0x0F, 0x10, 0xF0, 0xF5,
                                    0xFF};
    static const uint16_t ptrs[] = {0xD000, 0xD001, 0xD008, 0xD100};

    for (size_t p = 0; p < sizeof(ptrs) / sizeof(ptrs[0]); p++) {
        for (size_t v = 0; v < sizeof(bytes) / sizeof(bytes[0]); v++) {
            random_image(0xDE000u + (unsigned)(p * 10 + v));
            image[ptrs[p]] = bytes[v];
            image[(uint16_t)(ptrs[p] + 1)] = bytes[(v + 5) % 8];
            Args args = {.er = {ptrs[p], 0}};
            uint16_t rom_ret = run_rom(0x29490, &args, "digit_or_next_byte");
            begin_c();
            uint16_t c_ret = digit_or_next_byte(after_c, ptrs[p]);
            check_ret("digit_or_next_byte", rom_ret, c_ret);
            check_mem_all("digit_or_next_byte");
        }
    }
}

/* 0x153C4 copies ten bytes to er10 + 40. */
static void test_record10_from_er10_plus40(void)
{
    static const uint16_t addrs[] = {0, 0xD000, 0xD001, 0xD010, 0xDFF0};
    static const uint16_t bases[] = {0xD000, 0xD010, 0xDFD0};

    for (size_t a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
        for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
            random_image(0xF7000u + (unsigned)(a * 10 + b));
            Args args = {.er = {0, addrs[a], 0, 0, 0, bases[b]}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x153C4, &args,
                                       "record10_from_er10_plus40");
            begin_c();
            check_ret("record10_from_er10_plus40", rom_ret,
                      record10_from_er10_plus40(after_c, addrs[a], bases[b]));
            check_mem_all("record10_from_er10_plus40");
        }
    }
}

/* 0x14BDC is the stride-5 index with the registers saved around it. */
static void test_record10_index_5x_saved(void)
{
    static const uint8_t biases[] = {0, 1, 7, 25, 26, 100, 200, 255};
    static const uint16_t bases[] = {0, 1, 0xD000, 0xD002, 0xFFF0};

    for (size_t b = 0; b < sizeof(biases) / sizeof(biases[0]); b++) {
        for (size_t a = 0; a < sizeof(bases) / sizeof(bases[0]); a++) {
            random_image(0xDF000u + (unsigned)(b * 10 + a));
            Args args = {.er = {bases[a], biases[b]}};
            uint16_t rom_ret = run_rom(0x14BDC, &args, "record10_index_5x_saved");
            begin_c();
            uint16_t c_ret = record10_index_5x_saved(after_c, bases[a],
                                                     biases[b]);
            check_ret("record10_index_5x_saved", rom_ret, c_ret);
            check_mem_all("record10_index_5x_saved");
        }
    }
}

/* 0x14BF0 and 0x14C04 are `_memcpy_nn` with the count pushed, 200 and 50. */
static void one_memcpy_n(uint32_t addr, const char *name, uint16_t n,
                         uint16_t dst, uint16_t src, uint32_t seed)
{
    random_image(seed);
    Args args = {.er = {dst, src}, .stk = {n}, .nstk = 1};
    uint16_t rom_ret = run_rom(addr, &args, name);
    begin_c();
    uint16_t c_ret = n == 200 ? memcpy_200(after_c, dst, src)
                              : memcpy_50(after_c, dst, src);
    check_ret(name, rom_ret, c_ret);
    check_mem_all(name);
}

static void test_fixed_size_memcpy(void)
{
    static const uint16_t addrs[] = {0xD000, 0xD001, 0xD010, 0xD100, 0xD200};

    for (size_t d = 0; d < sizeof(addrs) / sizeof(addrs[0]); d++) {
        for (size_t s = 0; s < sizeof(addrs) / sizeof(addrs[0]); s++) {
            one_memcpy_n(0x14BF0, "memcpy_200", 200, addrs[d], addrs[s],
                         0xE0000u + (unsigned)(d * 10 + s));
            one_memcpy_n(0x14C04, "memcpy_50", 50, addrs[d], addrs[s],
                         0xE1000u + (unsigned)(d * 10 + s));
        }
    }
}

/* 0x1E5B4 / 0x24B28 are mode-and-substate predicates. */
static void one_mode_test(uint32_t addr, const char *name, uint8_t mode_want,
                          uint16_t arg, uint8_t mode, uint8_t sub)
{
    random_image(0xE2000u + (unsigned)(mode * 4 + sub));
    image[0xD111] = mode;
    image[0xD114] = sub;
    Args args = {.er = {arg, 0}};
    uint16_t rom_ret = run_rom(addr, &args, name);
    begin_c();
    uint16_t c_ret = (mode_want == 75 ? mode_is_75_1(after_c, arg)
                                      : mode_is_69_1(after_c, arg));
    check_ret(name, rom_ret, c_ret);
    check_mem_all(name);
}

static void test_mode_predicates(void)
{
    static const uint8_t modes[] = {0, 69, 75, 76, 255};
    static const uint8_t subs[] = {0, 1, 2, 255};
    static const uint16_t args[] = {0, 0x1234, 0xFF00};

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        for (size_t s = 0; s < sizeof(subs) / sizeof(subs[0]); s++) {
            for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); a++) {
                one_mode_test(0x1E5B4, "mode_is_75_1", 75, args[a], modes[m],
                              subs[s]);
                one_mode_test(0x24B28, "mode_is_69_1", 69, args[a], modes[m],
                              subs[s]);
            }
        }
    }
}

static void test_high_nibble_into_r5(void)
{
    static const uint16_t args[] = {0, 1, 0x00F0, 0xABCD, 0xFFFF};
    static const uint8_t r4s[] = {0x00, 0x0F, 0x10, 0x7F, 0x80, 0xF0, 0xFF};

    for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); a++) {
        for (size_t k = 0; k < sizeof(r4s) / sizeof(r4s[0]); k++) {
            uint16_t er4 = (uint16_t)(0x5A00u | r4s[k]);
            uint8_t mine[16] = {0};
            uint8_t r5 = 0, r2 = 0;
            Args in = {.er = {args[a], 0, er4}, .nstk = 0};

            random_image((uint32_t)(0xC6000 + a * 20 + k));
            run_rom(0x143D6, &in, "high_nibble_into_r5");

            mine[0] = (uint8_t)(args[a] & 0xFF);
            mine[1] = (uint8_t)(args[a] >> 8);
            mine[4] = (uint8_t)(er4 & 0xFF);
            mine[5] = (uint8_t)(er4 >> 8);
            begin_c();
            uint16_t c_ret = high_nibble_into_r5(after_c, args[a], r4s[k],
                                                &r5, &r2);
            mine[5] = r5;
            mine[2] = r2;
            check_ret("high_nibble_into_r5", args[a], c_ret);
            check_regs("high_nibble_into_r5", mine, 0xFFFFu);
            check_mem_all("high_nibble_into_r5");
        }
    }
}

void test_mem(void)
{
    test_small_leaves();
    test_record10_moves();
    test_record10_from_frame();
    test_record10_index();
    test_complement32();
    test_stack_bytes_used();
    test_store_reg_to_stack();
    test_high_nibble_into_r5();
    test_record10_copy_if_present();
    test_record_reset_flag38();
    test_records_from_pointers();
    test_record_to_pointer();
    test_record10_er8_from_er4();
    test_init_struct_30();
    test_record_kind_flag();
    test_digit_or_next_byte();
    test_record10_index_5x_saved();
    test_fixed_size_memcpy();
    test_mode_predicates();
    test_record10_from_er10_plus40();
    test_base_plus_10x_index();
    test_word_at_2x_index();
    test_nibble_pair_shifted();
    test_memcpy();
    test_memmove();
    test_memset();
    test_memzero();
    test_count_edges();
    test_shift_range_offsets();
    test_compare_bcd_records();
    test_decimal_record_predicate();
    test_assign_var();
    test_var_helpers();
    test_init_numeric_state();

    /* 0x09D26 - fixed-range clear, so one random image is enough */
    for (int rep = 0; rep < 4; rep++) {
        random_image((uint32_t)(0xD16C + rep));
        Args args = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x09D26, &args, "clear_block_d16c");
        begin_c();
        check_ret("clear_block_d16c", rom_ret, clear_block_d16c(after_c));
        check_mem("clear_block_d16c", 0xD160, 0xD190);
        check_mem_all("clear_block_d16c");
    }
}
