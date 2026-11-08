/* diagnostic.h - low-level diagnostic firmware routines. */
#ifndef NX_DIAGNOSTIC_H
#define NX_DIAGNOSTIC_H

#include <stdint.h>

/* 0x23F9A - checksum selected ROM windows and arm diagnostic status. */
uint16_t diag_calc_checksum(uint8_t *ds, const uint8_t *rom);

/* 0x23EC8 - draw the diagnostic byte stored at 0xF050. */
void diag_print_pd(uint8_t *ds, uint16_t buffer);

/* 0x23E92 - draw the version banner from the ROM metadata. */
void diag_print_ver(uint8_t *ds, const uint8_t *rom, uint16_t buffer);

/* 0x23DC6 - set the 14-pixel font and print at column zero. */
void line_print_font14_col0(uint8_t *ds, uint8_t row, uint16_t string);

/* 0x23EEE - show checksum diagnostics and wait for a confirmed key press. */
void diag_checksum(uint8_t *ds, const uint8_t *rom, uint16_t buffer);

/* 0x10E5E - format the six ROM serial bytes as twelve uppercase hex digits. */
void get_serial_num(uint8_t *ds, const uint8_t *rom, uint16_t output);

/* 0x10E8A - copy the 24-byte ROM identification block to data memory. */
void copy_rom_identification(uint8_t *ds, const uint8_t *rom);

/* 0x09E90 - initialize the 15-byte diagnostic table at 0xDBD0. */
void init_diagnostic_table(uint8_t *ds);

/* 0x2C57E - clear the 200-byte input range beginning at 0xD180. */
uint16_t clear_input_range(uint8_t *ds);

/* 0x0D736 - clear the 200-byte secondary input range at 0xD248. */
uint16_t clear_secondary_input_range(uint8_t *ds);

/* 0x2F73E - clear mode-dependent temporary state. */
void reset_mode_state(uint8_t *ds);

#endif