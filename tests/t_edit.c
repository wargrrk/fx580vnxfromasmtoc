/* t_edit.c - differential tests for the edit-box support routines. */
#include "harness.h"

#include "../src/edit.h"

static void test_prev_unit_off(void)
{
    /* the single byte sitting at probe-2 decides between the one-byte unit
     * result (probe-1) and the marker-byte result (probe-2) */
    static const uint8_t tails[] = {0x00, 0x24, 0x7F, 0xEF, 0xF0, 0xFF, 0xA6, 0x80};
    static const uint16_t probes[] = {0xD004u, 0xD005u, 0xD008u, 0xD00Bu, 0xD010u, 0xD011u};
    static const uint16_t los[] = {0xD000u, 0xD001u, 0xD002u, 0x0000u, 0xFFF1u, 0x8010u, 0xD011u};

    for (size_t p = 0; p < sizeof(probes) / sizeof(probes[0]); p++) {
        for (size_t li = 0; li < sizeof(los) / sizeof(los[0]); li++) {
            for (size_t t = 0; t < sizeof(tails); t++) {
                uint16_t probe = probes[p];
                uint16_t lo = los[li];
                uint16_t rc, cc;

                random_image(0x40000u + (unsigned)(p * 80 + li * 9 + t));
                image[probe - 2] = tails[t];
                image[probe - 1] = (uint8_t)~tails[t];   /* never read */
                Args args = {.er = {lo, probe}};
                rc = run_rom(0x0AC60, &args, "prev_unit_off");
                begin_c();
                cc = prev_unit_off(after_c, lo, probe);
                check_ret("prev_unit_off", rc, cc);
                check_mem("prev_unit_off", 0xD000, 0xD020);
            }
        }
    }
}

static void test_encode_editor_token(void)
{
    static const uint8_t modes[] = {0, 16, 17, 18, 19, 20, 21, 255};
    static const uint8_t flags[] = {0, 1};
    static const uint8_t values[] = {0, 1, 0xFF};
    const uint16_t output = 0xD400;

    for (size_t m = 0; m < sizeof(modes); m++) {
        for (size_t f = 0; f < sizeof(flags); f++) {
            for (size_t v = 0; v < sizeof(values); v++) {
                random_image(0x41000u + (uint32_t)(m * 20 + f * 5 + v));
                image[0xD117] = modes[m];
                image[0xD13F] = flags[f];
                image[0xD5EA] = (uint8_t)(0x30 + m);
                Args args = {.er = {values[v], output}, .nstk = 0};
                uint16_t rom_ret = run_rom(0x20AA8, &args, "encode_editor_token");
                begin_c();
                uint16_t c_ret = encode_editor_token(after_c, values[v], output);
                check_ret("encode_editor_token", rom_ret, c_ret);
                check_mem("encode_editor_token", output, output + 5);
            }
        }
    }
}

static void test_adjust_editor_state(void)
{
    static const uint16_t commands[] = {0xFC1C, 0xFC1D, 0xFC1E, 0xFC1F, 0xFC2D, 0xFC20};
    static const uint8_t positions[] = {0, 1, 2, 3, 5};
    static const uint8_t layers[] = {0, 1, 2, 3};

    for (uint8_t submode = 1; submode <= 7; submode++) {
        for (size_t c = 0; c < sizeof(commands) / sizeof(commands[0]); c++) {
            for (size_t p = 0; p < sizeof(positions); p++) {
                for (size_t l = 0; l < sizeof(layers); l++) {
                    random_image(0x42000u + submode * 1000u + c * 100u + p * 10u + l);
                    image[0xD112] = submode;
                    image[0xD135] = positions[p];
                    image[0xD136] = layers[l];
                    Args args = {.er = {commands[c]}, .nstk = 0};
                    uint16_t rom_ret = run_rom(0x0DC1E, &args, "adjust_editor_state");
                    begin_c();
                    uint8_t c_ret = adjust_editor_state(after_c, commands[c]);
                    check_ret("adjust_editor_state", (uint16_t)(rom_ret & 0xFF), c_ret);
                    check_mem("adjust_editor_state", 0xD110, 0xD14A);
                }
            }
        }
    }
}

static void test_editor_special_to_ascii(void)
{
    static const uint8_t high_bytes[] = {0, 1, 0xFC, 0xFF};

    random_image(0x44000u);
    for (size_t h = 0; h < sizeof(high_bytes); h++) {
        for (unsigned low = 0; low < 256; low++) {
            uint16_t code = (uint16_t)(((uint16_t)high_bytes[h] << 8) | low);
            Args args = {.er = {code}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x2ABE4, &args, "editor_special_to_ascii");
            check_ret("editor_special_to_ascii", rom_ret, editor_special_to_ascii(code));
        }
    }
}

static void test_editor_classify_code(void)
{
    random_image(0x45000u);
    for (unsigned code = 0; code < 256; code++) {
        Args args = {.er = {(uint16_t)code}, .nstk = 0};
        uint16_t rom_ret = run_rom(0x2035C, &args, "editor_classify_code");
        check_ret("editor_classify_code", (uint16_t)(rom_ret & 0xFF),
                  editor_classify_code((uint8_t)code));
    }
}

static void test_encode_editor_character(void)
{
    const uint16_t output = 0xD400;
    random_image(0x46000u);
    for (uint8_t a = 0; a < 2; a++) {
        for (uint8_t e = 0; e < 2; e++) {
            for (unsigned index = 0; index < 256; index++) {
                image[0xD13F] = a;
                image[0xD14E] = e;
                Args args = {.er = {output, (uint16_t)index}, .nstk = 0};
                uint16_t rom_ret = run_rom(0x0E7A8, &args, "encode_editor_character");
                begin_c();
                uint16_t c_ret = encode_editor_character(after_c, output, (uint8_t)index);
                check_ret("encode_editor_character", rom_ret, c_ret);
                check_mem("encode_editor_character", output, output + 3);
            }
        }
    }
}

static void test_classify_editor_token(void)
{
    static const uint8_t markers[] = {0, 1, 250, 251, 252, 253, 254, 255};
    random_image(0x47000u);
    for (size_t m = 0; m < sizeof(markers); m++) {
        for (unsigned value = 0; value < 256; value++) {
            uint16_t code = (uint16_t)(((uint16_t)markers[m] << 8) | value);
            Args args = {.er = {code}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x15FE4, &args, "classify_editor_token");
            uint8_t c_ret = classify_editor_token((uint8_t)value, markers[m]);
            check_ret("classify_editor_token", (uint16_t)(rom_ret & 0xFF),
                      c_ret);
        }
    }
}

static void test_clamp_editor_offset(void)
{
    static const uint16_t starts[] = {0, 1, 100, 0x7FFF, 0x8000, 0xFFF0};
    static const uint16_t probes[] = {0, 1, 50, 100, 0x7FFF, 0x8000, 0xFFFF};
    static const uint16_t limits[] = {0, 1, 10, 100, 0x7FFF, 0xFFFF};

    for (size_t s = 0; s < sizeof(starts) / sizeof(starts[0]); s++) {
        for (size_t p = 0; p < sizeof(probes) / sizeof(probes[0]); p++) {
            for (size_t l = 0; l < sizeof(limits) / sizeof(limits[0]); l++) {
                random_image(0x49000u + (uint32_t)(s * 100 + p * 10 + l));
                Args args = {.er = {starts[s], probes[p]},
                             .stk = {limits[l]}, .nstk = 1};
                uint16_t rom_ret = run_rom(0x0D742, &args, "clamp_editor_offset");
                uint16_t c_ret = clamp_editor_offset(starts[s], probes[p], limits[l]);
                check_ret("clamp_editor_offset", rom_ret, c_ret);
            }
        }
    }
}

void test_edit(void)
{
    test_prev_unit_off();
    test_encode_editor_token();
    test_adjust_editor_state();
    test_editor_special_to_ascii();
    test_editor_classify_code();
    test_encode_editor_character();
    test_classify_editor_token();
    test_clamp_editor_offset();
}
