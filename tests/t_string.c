/* t_string.c - differential tests for the string cluster. */
#include "harness.h"

#include <string.h>

#include "../src/runtime.h"

static void test_strlen(void)
{
    static const uint16_t lens[] = {0, 1, 2, 3, 15, 16, 63, 64, 255, 256, 400};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        for (int rep = 0; rep < 3; rep++) {
            random_image((uint32_t)(t * 7 + rep + 1));
            uint16_t p = 0xD200;
            uint16_t n = lens[t];
            for (uint16_t i = 0; i < n; i++)
                image[p + i] = (uint8_t)(1 + (rnd() % 255));
            image[p + n] = 0;
            Args a = {.er = {p}, .nstk = 0};
            uint16_t r = run_rom(0x29A2C, &a, "strlen_n");
            begin_c();
            uint16_t c = strlen_n(after_c, p);
            check_ret("strlen_n", r, c);
            check_mem("strlen_n", RAM_BASE, CMP_END);
        }
    }
}

static void test_byte_strlen(void)
{
    static const uint16_t lens[] = {0, 1, 2, 5, 100, 255, 256, 300};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        random_image((uint32_t)(t + 100));
        uint16_t p = 0xD300;
        uint16_t n = lens[t];
        for (uint16_t i = 0; i < n; i++)
            image[p + i] = (uint8_t)(1 + (rnd() % 255));
        image[p + n] = 0;
        Args a = {.er = {p}, .nstk = 0};
        uint16_t r = run_rom(0x2AB26, &a, "byte_strlen_n");
        begin_c();
        uint8_t c = byte_strlen_n(after_c, p);
        check_ret("byte_strlen_n", (uint16_t)(r & 0xFF), c);
        check_mem("byte_strlen_n", RAM_BASE, CMP_END);
    }
}

static void test_strcpy(void)
{
    static const uint16_t lens[] = {0, 1, 2, 9, 64, 200, 500};
    for (size_t t = 0; t < sizeof(lens) / sizeof(lens[0]); t++) {
        random_image((uint32_t)(t + 200));
        uint16_t dst = 0xD200, src = 0xD700;
        uint16_t n = lens[t];
        for (uint16_t i = 0; i < n; i++)
            image[src + i] = (uint8_t)(1 + (rnd() % 255));
        image[src + n] = 0;
        Args a = {.er = {dst, src}, .nstk = 0};
        uint16_t r = run_rom(0x0E5C8, &a, "strcpy_nn");
        begin_c();
        uint16_t c = strcpy_nn(after_c, dst, src);
        check_ret("strcpy_nn", r, c);
        check_mem("strcpy_nn", RAM_BASE, CMP_END);
    }
}

static void test_strcat(void)
{
    static const uint16_t plens[] = {0, 1, 5, 40, 120};
    static const uint16_t slens[] = {0, 1, 3, 30, 90};
    for (size_t i = 0; i < sizeof(plens) / sizeof(plens[0]); i++) {
        for (size_t j = 0; j < sizeof(slens) / sizeof(slens[0]); j++) {
            random_image((uint32_t)(i * 10 + j + 300));
            uint16_t dst = 0xD200, src = 0xD700;
            for (uint16_t k = 0; k < plens[i]; k++)
                image[dst + k] = (uint8_t)(1 + (rnd() % 255));
            image[dst + plens[i]] = 0;
            for (uint16_t k = 0; k < slens[j]; k++)
                image[src + k] = (uint8_t)(1 + (rnd() % 255));
            image[src + slens[j]] = 0;
            Args a = {.er = {dst, src}, .nstk = 0};
            uint16_t r = run_rom(0x2EDA0, &a, "strcat_nn");
            begin_c();
            uint16_t c = strcat_nn(after_c, dst, src);
            check_ret("strcat_nn", r, c);
            check_mem("strcat_nn", RAM_BASE, CMP_END);
        }
    }
}

static void test_smart(void)
{
    random_image(401);
    Args a0 = {.nstk = 0};
    uint16_t r = run_rom(0x203B8, &a0, "smart_strlen_n(NULL)");
    begin_c();
    check_ret("smart_strlen_n(NULL)", r, smart_strlen_n(after_c, 0));
    check_mem("smart_strlen_n(NULL)", RAM_BASE, CMP_END);

    struct { uint16_t dst, src; const char *name; } cases[] = {
        {0x0000, 0xD700, "smart_strcpy(dst=NULL)"},
        {0xD200, 0x0000, "smart_strcpy(src=NULL)"},
        {0x0000, 0x0000, "smart_strcpy(both NULL)"},
        {0xD200, 0xD700, "smart_strcpy(normal)"},
    };
    for (int i = 0; i < 4; i++) {
        random_image((uint32_t)(402 + i));
        memcpy(image + 0xD700, "hello", 6);
        memcpy(image + 0xD200, "XXXXXXXX", 8);
        Args a = {.er = {cases[i].dst, cases[i].src}, .nstk = 0};
        uint16_t rr = run_rom(0x203C2, &a, cases[i].name);
        begin_c();
        uint16_t c = smart_strcpy_nn(after_c, cases[i].dst, cases[i].src);
        check_ret(cases[i].name, rr, c);
        check_mem(cases[i].name, RAM_BASE, CMP_END);
    }

    for (int i = 0; i < 4; i++) {
        random_image((uint32_t)(410 + i));
        memcpy(image + 0xD700, "world", 6);
        memcpy(image + 0xD200, "abc", 4);
        Args a = {.er = {cases[i].dst, cases[i].src}, .nstk = 0};
        uint16_t rr = run_rom(0x203D6, &a, cases[i].name);
        begin_c();
        uint16_t c = smart_strcat_nn(after_c, cases[i].dst, cases[i].src);
        check_ret(cases[i].name, rr, c);
        check_mem(cases[i].name, RAM_BASE, CMP_END);
    }
}

static void test_format_number_token(void)
{
    static const uint16_t values[] = {0, 1, 9, 10, 59, 99, 100, 32767, 0x8000, 0x8001, 0xFFFF};
    static const uint8_t alternate[] = {0, 1};
    static const uint8_t extended[] = {0, 1};
    static const uint8_t fonts[] = {8, 10, 14};
    const uint16_t source = 0xD200;
    const uint16_t output = 0xD300;

    for (size_t a = 0; a < sizeof(alternate); a++) {
        for (size_t e = 0; e < sizeof(extended); e++) {
            for (size_t f = 0; f < sizeof(fonts); f++) {
                for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
                    random_image(0x43000u + (uint32_t)(a * 1000 + e * 500 + f * 100 + v));
                    image[0xD14E] = alternate[a];
                    image[0xD13F] = extended[e];
                    image[0xD137] = fonts[f];
                    image[source + 16] = (uint8_t)values[v];
                    image[source + 17] = (uint8_t)(values[v] >> 8);
                    Args args = {.er = {source, output}, .nstk = 0};
                    run_rom(0x20462, &args, "format_number_token");
                    begin_c();
                    format_number_token(after_c, source, output);
                    check_mem("format_number_token", output, output + 12);
                }
            }
        }
    }
}

static void test_unpack_decimal_record(void)
{
    static const uint8_t tags[] = {0, 1, 9, 0x10, 0x80};
    static const uint8_t exponents[] = {0, 1, 4, 5, 6, 9, 255};
    const uint16_t source = 0xD500;
    const uint16_t output = 0xD600;

    for (size_t t = 0; t < sizeof(tags); t++) {
        for (size_t e = 0; e < sizeof(exponents); e++) {
            random_image(0x43000u + (uint32_t)(t * 20 + e));
            image[source] = tags[t];
            image[source + 8] = (uint8_t)(0x26 + e * 17);
            image[source + 9] = exponents[e];
            for (uint16_t i = 1; i < 8; i++)
                image[source + i] = (uint8_t)(i * 0x13 + e * 7);
            Args args = {.er = {source, output}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x203E6, &args, "unpack_decimal_record");
            begin_c();
            uint8_t c_ret = unpack_decimal_record(after_c, source, output);
            check_ret("unpack_decimal_record", (uint16_t)(rom_ret & 0xFF), c_ret);
            check_mem("unpack_decimal_record", output, output + 24);
        }
    }
}

static void test_replace_trailing_token(void)
{
    static const uint8_t strings[][5] = {
        {49, 0, 0, 0, 0},
        {96, 49, 0, 0, 0},
        {192, 49, 0, 0, 0},
        {'A', 'B', 'C', 0, 0},
        {'A', 49, 'B', 0, 0},
    };
    const uint16_t string = 0xD700;
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        random_image(0x43100u + (uint32_t)i);
        memcpy(image + string, strings[i], sizeof(strings[i]));
        Args args = {.er = {string, 0x00E7}, .nstk = 0};
        uint16_t rom_ret = run_rom(0x208A6, &args, "replace_trailing_token");
        begin_c();
        uint16_t c_ret = replace_trailing_token(after_c, string, 0xE7);
        check_ret("replace_trailing_token", rom_ret, c_ret);
        check_mem("replace_trailing_token", string - 1, string + sizeof(strings[i]));
    }
}

static void test_expand_string_token(void)
{
    static const uint8_t markers[] = {0, 250, 251, 253, 254};
    const uint16_t output = 0xD300;
    random_image(0x48000u);
    for (size_t m = 0; m < sizeof(markers); m++) {
        for (unsigned index = 0; index < 256; index++) {
            uint16_t code = (uint16_t)(((uint16_t)markers[m] << 8) | index);
            Args args = {.er = {code, output}, .nstk = 0};
            uint16_t rom_ret = run_rom(0x208F2, &args, "expand_string_token");
            begin_c();
            uint8_t c_ret = expand_string_token(after_c, code, output);
            check_ret("expand_string_token", (uint16_t)(rom_ret & 0xFF), c_ret);
            check_mem("expand_string_token", output, output + 34);
        }
    }
}

void test_string(void)
{
    test_strlen();
    test_byte_strlen();
    test_strcpy();
    test_strcat();
    test_smart();
    test_format_number_token();
    test_unpack_decimal_record();
    test_replace_trailing_token();
    test_expand_string_token();
    for (unsigned value = 0; value < 256; value++) {
        Args args = {.er = {(uint16_t)value}, .nstk = 0};
        uint16_t rom_ret = run_rom(0x24010, &args, "hex_byte");
        check_ret("hex_byte", rom_ret, hex_byte((uint8_t)value));
    }
}
