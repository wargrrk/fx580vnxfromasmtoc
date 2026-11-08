/* diagnostic.c - diagnostic routines reconstructed from the firmware. */
#include "diagnostic.h"

#include <stddef.h>

#include "display.h"
#include "keyboard.h"
#include "print.h"

#define FONT_SIZE_ADDR 0xD137u

static uint16_t subtract_words(uint16_t checksum, const uint8_t *rom,
                               size_t offset, size_t length)
{
    for (size_t i = 0; i < length; i += 2) {
        uint16_t word = (uint16_t)(rom[offset + i] |
                                   ((uint16_t)rom[offset + i + 1] << 8));
        checksum = (uint16_t)(checksum - word);
    }
    return checksum;
}

/* 0x23F9A - subtract each 16-bit ROM word into the 16-bit accumulator. */
uint16_t diag_calc_checksum(uint8_t *ds, const uint8_t *rom)
{
    uint16_t checksum = 0;

    ds[0xF004] = 1;
    checksum = subtract_words(checksum, rom, 0x00000, 0x0FC00);
    checksum = subtract_words(checksum, rom, 0x10000, 0x10000);
    checksum = subtract_words(checksum, rom, 0x20000, 0x10000);
    checksum = subtract_words(checksum, rom, 0x30000, 0x0FFF6);
    return checksum;
}

void diag_print_pd(uint8_t *ds, uint16_t buffer)
{
    uint8_t value = ds[0xF050];

    ds[buffer] = 'P';
    ds[(uint16_t)(buffer + 1)] = (uint8_t)((value >> 4) | 0x30);
    ds[(uint16_t)(buffer + 2)] = (uint8_t)((value & 0x0F) | 0x30);
    ds[(uint16_t)(buffer + 3)] = 0;
    line_print(ds, 0x2100, buffer, NULL);
}

void diag_print_ver(uint8_t *ds, const uint8_t *rom, uint16_t buffer)
{
    buffer_clear(ds);
    ds[FONT_SIZE_ADDR] = 14;

    for (uint16_t i = 0; i < 6; i++)
        ds[(uint16_t)(buffer + i)] = rom[0x3FFEEu + i];
    ds[(uint16_t)(buffer + 6)] = ' ';
    ds[(uint16_t)(buffer + 7)] = 'V';
    ds[(uint16_t)(buffer + 8)] = 'e';
    ds[(uint16_t)(buffer + 9)] = 'r';
    ds[(uint16_t)(buffer + 10)] = rom[0x3FFF4u];
    ds[(uint16_t)(buffer + 11)] = rom[0x3FFF5u];
    ds[(uint16_t)(buffer + 12)] = 0;

    line_print(ds, 0x0100, buffer, NULL);
}

void line_print_font14_col0(uint8_t *ds, uint8_t row, uint16_t string)
{
    ds[FONT_SIZE_ADDR] = 14;
    line_print(ds, (uint16_t)(row << 8), string, NULL);
}

static void write_text(uint8_t *ds, uint16_t address, const char *text)
{
    while (*text)
        ds[address++] = (uint8_t)*text++;
    ds[address] = 0;
}

void diag_checksum(uint8_t *ds, const uint8_t *rom, uint16_t buffer)
{
    uint16_t checksum, expected;
    const char *status;

    buffer_clear(ds);
    diag_print_ver(ds, rom, buffer);
    render_ddd4(ds);

    checksum = diag_calc_checksum(ds, rom);
    expected = (uint16_t)(rom[0x3FFF6u] | ((uint16_t)rom[0x3FFF7u] << 8));
    status = checksum == expected ? " OK" : " NG";

    write_text(ds, buffer, "SUM");
    write_text(ds, (uint16_t)(buffer + 3), " BB26");
    write_text(ds, (uint16_t)(buffer + 8), status);
    line_print(ds, 0x1100, buffer, NULL);

    diag_print_pd(ds, buffer);
    write_text(ds, buffer, "Read OK");
    line_print(ds, 0x212C, buffer, NULL);
    line_print_font14_col0(ds, 49, 0x5830);

    ds[0xF049] = 1;
    ds[0xF04A] = 1;
    ds[0xF04B] = 1;
    ds[0xF04C] = 49;

    do {
        render_ddd4(ds);
    } while (!poll_on_key(ds));
    hw_clear_0F048(ds);
}

void get_serial_num(uint8_t *ds, const uint8_t *rom, uint16_t output)
{
    for (uint16_t i = 0; i < 6; i++) {
        uint16_t chars = hex_byte(rom[0x3FFF8u + i]);
        uint16_t pos = (uint16_t)(output + 10 - 2 * i);
        ds[pos] = (uint8_t)chars;
        ds[(uint16_t)(pos + 1)] = (uint8_t)(chars >> 8);
    }
}

void copy_rom_identification(uint8_t *ds, const uint8_t *rom)
{
    for (uint16_t i = 0; i < 24; i++)
        ds[(uint16_t)(0xD11Au + i)] = rom[0x3FFC0u + i];
}

void init_diagnostic_table(uint8_t *ds)
{
    for (uint8_t value = 15; value > 0; value--)
        ds[(uint16_t)(0xDBD0u + 15 - value)] = value;
}

uint16_t clear_input_range(uint8_t *ds)
{
    memset_n(ds, 0xD180, 0, 200);
    return 0xD180;
}

uint16_t clear_secondary_input_range(uint8_t *ds)
{
    memset_n(ds, 0xD248, 0, 200);
    return 0xD248;
}

void reset_mode_state(uint8_t *ds)
{
    uint8_t mode = ds[0xD111];

    ds[0xD141] = 0;
    if (mode == 193 || mode == 2 || mode == 196 || mode == 137)
        memset_n(ds, 0xD392, 0, 400);
    clear_secondary_input_range(ds);
}