/*
 * runtime.h - C reimplementation of the fx-580VN X runtime/support routines.
 *
 * The nX-U8/100 has a 16-bit data address space selected by the DSR register.
 * The firmware keeps DSR = 0 everywhere, so memory is one flat 64 KiB window.
 * Pointers in the ROM are plain 16-bit offsets into that window, and address
 * arithmetic wraps at 64 KiB.  These functions therefore take the window as
 * `ds` plus 16-bit offsets, which is the faithful model; all the pointer
 * dereferences happen through `ds[offset]`.
 *
 * ABI (Casio's compiler): 16-bit arguments travel in er0, er2, er4, er6, more
 * arguments are pushed on the stack, and the result comes back in er0.  The
 * test harness in tests/ adapts those register-level conventions.
 */
#ifndef NX_RUNTIME_H
#define NX_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* string.c - 0x29A2C, 0x2AB26, 0x0E5C8, 0x2EDA0, 0x203B8, 0x203C2, 0x203D6 */
uint16_t strlen_n(uint8_t *ds, uint16_t s);
uint8_t byte_strlen_n(uint8_t *ds, uint16_t s);
uint16_t strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t smart_strlen_n(uint8_t *ds, uint16_t s);
uint16_t smart_strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t smart_strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src);

/* string.c - 0x24BD6 (pointer table) and 0x2AC9E (expanding copy) */
uint16_t get_string_constant(uint8_t *ds, uint16_t index);
uint16_t copy_string_constant(uint8_t *ds, uint16_t index, uint16_t dst);
uint16_t hex_byte(uint8_t value);
uint16_t format_number_token(uint8_t *ds, uint16_t source, uint16_t output);
uint8_t unpack_decimal_record(uint8_t *ds, uint16_t source, uint16_t output);
uint16_t replace_trailing_token(uint8_t *ds, uint16_t string, uint8_t value);
uint8_t expand_string_token(uint8_t *ds, uint16_t code, uint16_t output);
uint16_t string_copy_with_nul(uint8_t *ds, uint16_t dst, uint16_t src);

/* mem.c - 0x26FFA, 0x0A8AA, 0x1EB94, 0x09D34 */
uint16_t memcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n);
uint16_t memmove_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n);
uint16_t memset_n(uint8_t *ds, uint16_t dst, uint8_t value, uint16_t n);
uint16_t memzero(uint8_t *ds, uint16_t dst, uint16_t n);
void shift_range_offsets(uint8_t *ds, uint16_t delta, uint8_t lower,
						 uint8_t start);
uint16_t record_template_06(uint8_t *ds, uint16_t dst);
uint16_t record_template_03(uint8_t *ds, uint16_t dst);
uint16_t table_slot_store(uint8_t *ds, uint16_t offset, uint16_t value,
                          uint8_t index);
uint8_t compare_bcd_records(uint8_t *ds);
uint8_t decimal_record_predicate(uint8_t *ds);

/* mem.c - 0x09DE6: variable-slot save (plus its matrix/vector shadow) */
uint16_t assign_var(uint8_t *ds, uint8_t slot, uint16_t src);

/* mem.c - small leaf accessors of the mode/variable cluster */
void store_0D10E(uint8_t *ds, uint16_t v);
uint16_t mode193_value(uint8_t *ds, uint16_t arg);
uint8_t var_mode_attr(uint8_t *ds);
void init_numeric_state(uint8_t *ds);

#endif /* NX_RUNTIME_H */
