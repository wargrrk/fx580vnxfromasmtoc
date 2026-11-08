/* t_diagnostic.c - differential tests for diagnostic routines. */
#include "harness.h"

#include <string.h>

#include "../src/diagnostic.h"

void test_diagnostic(void)
{
    static const uint8_t serial_cases[][6] = {
        {0, 0, 0, 0, 0, 0},
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    uint8_t *rom = harness_rom();
    uint8_t saved_serial[6];
    memcpy(saved_serial, rom + 0x3FFF8u, sizeof(saved_serial));
    for (size_t sample = 0; sample < sizeof(serial_cases) / sizeof(serial_cases[0]); sample++) {
        uint16_t output = 0xD400;
        memcpy(rom + 0x3FFF8u, serial_cases[sample], sizeof(serial_cases[sample]));
        random_image(0xD1A500u + (uint32_t)sample);
        Args args = {.er = {output}, .nstk = 0};
        run_rom(0x10E5E, &args, "get_serial_num");
        begin_c();
        get_serial_num(after_c, rom, output);
        check_mem("get_serial_num", output, output + 12);
    }
    memcpy(rom + 0x3FFF8u, saved_serial, sizeof(saved_serial));

    {
        random_image(0xD1A5FFu);
        Args args = {.nstk = 0};
        run_rom(0x10E8A, &args, "copy_rom_identification");
        begin_c();
        copy_rom_identification(after_c, rom);
        check_mem("copy_rom_identification", 0xD11A, 0xD132);
    }

    {
        random_image(0xD1A5FEu);
        Args args = {.nstk = 0};
        run_rom(0x09E90, &args, "init_diagnostic_table");
        begin_c();
        init_diagnostic_table(after_c);
        check_mem("init_diagnostic_table", 0xDBD0, 0xDBDF);
    }

    {
        random_image(0xD1A5FDu);
        Args args = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x2C57E, &args, "clear_input_range");
        begin_c();
        uint16_t c_ret = clear_input_range(after_c);
        check_ret("clear_input_range", rom_ret, c_ret);
        check_mem("clear_input_range", 0xD180, 0xD248);
    }

    {
        static const uint8_t modes[] = {0, 2, 137, 193, 196, 255};
        for (size_t i = 0; i < sizeof(modes); i++) {
            random_image(0xD1AA00u + (uint32_t)i);
            image[0xD111] = modes[i];
            Args args = {.nstk = 0};
            run_rom(0x2F73E, &args, "reset_mode_state");
            begin_c();
            reset_mode_state(after_c);
            check_mem("reset_mode_state", 0xD140, 0xD530);
        }
    }

    for (uint32_t seed = 0; seed < 3; seed++) {
        random_image(0xD1A600u + seed);
        Args args = {.nstk = 0};
        uint16_t rom_ret = run_rom(0x23F9A, &args, "diag_calc_checksum");
        begin_c();
        uint16_t c_ret = diag_calc_checksum(after_c, harness_rom());
        check_ret("diag_calc_checksum", rom_ret, c_ret);
        check_mem("diag_calc_checksum", 0xF000, 0xF020);
    }

    static const uint8_t values[] = {0x00, 0x09, 0x10, 0x39, 0xA5, 0xFF};
    for (size_t i = 0; i < sizeof(values); i++) {
        uint16_t buffer = STACK_TOP;
        random_image(0xD1A700u + (uint32_t)i);
        image[0xF050] = values[i];
        image[0xD137] = 14;
        image[0xD138] = 0;
        image[0xD139] = (uint8_t)(i & 1);
        Args args = {.er = {[7] = (uint16_t)(buffer + 16)}, .nstk = 0};
        run_rom(0x23EC8, &args, "diag_print_pd");
        begin_c();
        diag_print_pd(after_c, buffer);
        check_mem("diag_print_pd", 0xDDD4, 0xE9D4);
    }

    for (uint32_t seed = 0; seed < 3; seed++) {
        uint16_t buffer = STACK_TOP;
        random_image(0xD1A800u + seed);
        image[0xD138] = 0;
        image[0xD139] = 1;
        Args args = {.er = {[7] = (uint16_t)(buffer + 16)}, .nstk = 0};
        run_rom(0x23E92, &args, "diag_print_ver");
        begin_c();
        diag_print_ver(after_c, harness_rom(), buffer);
        check_mem("diag_print_ver", 0xDDD4, 0xE9D4);
    }

    {
        uint8_t *rom = harness_rom();
        uint8_t saved_low = rom[0x3FFF6u];
        uint8_t saved_high = rom[0x3FFF7u];
        uint16_t buffer = STACK_TOP - 36;
        uint16_t checksum = diag_calc_checksum(image, rom);

        for (uint16_t mismatch = 0; mismatch < 2; mismatch++) {
            uint16_t expected = (uint16_t)(checksum + mismatch);
            random_image(0xD1A900u + mismatch);
            rom[0x3FFF6u] = (uint8_t)expected;
            rom[0x3FFF7u] = (uint8_t)(expected >> 8);
            image[0xD138] = 0;
            image[0xD139] = 0;
            image[0xD0F5] = 0;
            image[0xD0F7] = 1;
            image[0xF040] = 0;
            image[0xF050] = 0x5A;

            Args args = {.nstk = 0};
            run_rom(0x23EEE, &args, "diag_checksum");
            begin_c();
            diag_checksum(after_c, rom, buffer);
            check_mem("diag_checksum ram", 0xD000, CMP_END);
            check_mem("diag_checksum sfr", 0xF000, 0xF060);
            check_mem("diag_checksum screen", 0xDDD4, 0xE9D4);
            check_mem("diag_checksum lcd", 0xF800, 0x10000);
        }

        rom[0x3FFF6u] = saved_low;
        rom[0x3FFF7u] = saved_high;
    }
}