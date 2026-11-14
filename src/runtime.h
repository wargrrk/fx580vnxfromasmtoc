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
 *
 * Trap worth remembering: `mul erN, rM` multiplies the *low bytes* only.  The
 * ROM returns a product of 0 for 0x0100 and treats 0x7FFF exactly like 0xFF,
 * so a translation must cast the operand to uint8_t before multiplying - the
 * 16-bit product of the two bytes is still a full word.
 */
#ifndef NX_RUNTIME_H
#define NX_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* The data space only latches stores in three windows: RAM (0xD000-0xECFF),
 * the SFRs (0xF000-0xF0FF) and the LCD framebuffer (0xF800-0xFFFF).  Everywhere
 * else - the ROM image, the 0xF100-0xF7FF open bus, and the stack window the
 * firmware keeps at 0xEC00-0xEFFF - a store is silently dropped by the chip,
 * and the firmware depends on that (see the "table-driven stores" note in
 * emu/nx8.c).  A C body that writes without this test therefore diverges from
 * the ROM in exactly the place the ROM relies on the hardware to disagree. */
#define DS_RAM_LO   0xD000u
#define DS_SFR_HI   0xF0FFu
#define DS_LCD_LO   0xF800u

static inline int ds_writable(uint16_t addr)
{
    return (addr >= DS_RAM_LO && addr <= DS_SFR_HI) || addr >= DS_LCD_LO;
}

static inline void ds_put(uint8_t *ds, uint16_t addr, uint8_t value)
{
    if (ds_writable(addr))
        ds[addr] = value;
}

/* string.c - 0x29A2C, 0x2AB26, 0x0E5C8, 0x2EDA0, 0x203B8, 0x203C2, 0x203D6 */
uint16_t strlen_n(uint8_t *ds, uint16_t s);
uint8_t byte_strlen_n(uint8_t *ds, uint16_t s);
uint16_t strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t smart_strlen_n(uint8_t *ds, uint16_t s);
uint16_t smart_strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t smart_strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src);

/* string.c - 0x0ED88, 0x20EA4 */
uint16_t byte_is_lead(uint8_t *ds, uint16_t value);
uint16_t append_string_1de5(uint8_t *ds, uint16_t dst);

/* string.c - 0x24BD6 (pointer table) and 0x2AC9E (expanding copy) */
uint16_t get_string_constant(uint8_t *ds, uint16_t index);
uint16_t copy_string_constant(uint8_t *ds, uint16_t index, uint16_t dst);
uint16_t hex_byte(uint8_t value);
uint16_t format_number_token(uint8_t *ds, uint16_t source, uint16_t output);
uint8_t unpack_decimal_record(uint8_t *ds, uint16_t source, uint16_t output);
/* string.c - 0x0EA4A, 0x19808 */
uint16_t decode_char_code(uint8_t *ds, uint16_t p);
uint16_t to_packed_bcd(uint8_t value);
uint16_t replace_trailing_token(uint8_t *ds, uint16_t string, uint8_t value);
uint8_t expand_string_token(uint8_t *ds, uint16_t code, uint16_t output);
uint16_t string_copy_with_nul(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t expand_glyph_mask(uint8_t *ds, uint16_t src, uint16_t out,
                           uint16_t length);

/* mem.c - 0x26FFA, 0x0A8AA, 0x1EB94, 0x09D34 */
uint16_t memcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n);
uint16_t memmove_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n);
uint16_t memset_n(uint8_t *ds, uint16_t dst, uint8_t value, uint16_t n);
uint16_t memzero(uint8_t *ds, uint16_t dst, uint16_t n);

/* mem.c - 0x09D26, 0x2ABDA, 0x143CA, 0x14C18 */
uint16_t clear_block_d16c(uint8_t *ds);
uint16_t max_u16(uint8_t *ds, uint16_t a, uint16_t b);
uint16_t clear_160_bytes(uint8_t *ds, uint16_t dst);
uint16_t clear_200_bytes(uint8_t *ds, uint16_t dst);
uint16_t clear_block_d6b2(uint8_t *ds);
void shift_range_offsets(uint8_t *ds, uint16_t delta, uint8_t lower,
						 uint8_t start);
uint16_t record_template_06(uint8_t *ds, uint16_t dst);
uint16_t record_template_03(uint8_t *ds, uint16_t dst);
uint16_t table_slot_store(uint8_t *ds, uint16_t offset, uint16_t value,
                          uint8_t index);
uint8_t compare_bcd_records(uint8_t *ds);
uint8_t decimal_record_predicate(uint8_t *ds);
uint16_t table_slot_fold(uint8_t *ds, uint16_t delta, uint16_t extra,
                         uint16_t apply_global, uint16_t start);

/* mem.c - 0x09DE6: variable-slot save (plus its matrix/vector shadow) */
uint16_t assign_var(uint8_t *ds, uint8_t slot, uint16_t src);

/* mem.c - record/table helpers 0x1D950, 0x1DCCE, 0x2E2BC, 0x0E4AE */
uint16_t record_init_kind(uint8_t *ds, uint16_t dst, uint8_t kind);
uint16_t record_pair_copy(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t table_index_scan(uint8_t *ds, uint16_t key);
uint16_t fixup_0D10E(uint8_t *ds, uint16_t arg);

/* mem.c - small leaf accessors of the mode/variable cluster */
void store_0D10E(uint8_t *ds, uint16_t v);
uint16_t mode193_value(uint8_t *ds, uint16_t arg);
uint8_t var_mode_attr(uint8_t *ds);
void init_numeric_state(uint8_t *ds);

/* arith.c - the arithmetic core at 0x27000-0x31000 */
uint16_t row_count_over_limit(uint8_t *ds, uint8_t add);
uint16_t init_descriptor_record(uint8_t *ds, uint16_t dst);
void draw_bar_frames(uint8_t *ds, uint8_t index);
uint16_t pitch_from_state(uint8_t *ds, uint16_t arg);
uint16_t key_repeat_gate(uint8_t *ds, uint16_t arg, const uint16_t *stk);
void print_indexed_rows(uint8_t *ds, uint16_t buffer);
uint16_t tall_metric_from_font(uint8_t *ds, uint16_t arg);
uint16_t small_metric_from_font(uint8_t *ds, uint16_t arg);
uint16_t reset_flags_and_state(uint8_t *ds, uint16_t arg);
void draw_submode_box(uint8_t *ds);

#endif /* NX_RUNTIME_H */
