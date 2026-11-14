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

/* mem.c - ten-byte record moves between 0xD000..0xD05F (0x19E54..0x19F62) */
uint16_t record10_d046_to_d010(uint8_t *ds);
uint16_t record10_d010_to_d046(uint8_t *ds);
uint16_t record10_d000_to_d046(uint8_t *ds);
uint16_t record10_d03c_to_d000(uint8_t *ds);
uint16_t record10_d03c_to_d010(uint8_t *ds);
uint16_t record10_d000_to_d03c(uint8_t *ds);
uint16_t record10_d050_to_d010(uint8_t *ds);
uint16_t record10_d000_to_d050(uint8_t *ds);
uint16_t record10_d030_to_d010(uint8_t *ds);
uint16_t record10_d000_to_d030(uint8_t *ds);
uint16_t record10_d000_to_d010(uint8_t *ds);
uint16_t record10_d010_to_d000(uint8_t *ds);
uint16_t record10_d010_to_d050(uint8_t *ds);
uint16_t record10_d050_to_d000(uint8_t *ds);
uint16_t record10_d030_to_d000(uint8_t *ds);
uint16_t record10_d010_to_d030(uint8_t *ds);

/* mem.c - 0x17CA6: the same move with er12/er14 as the addresses */
uint16_t record10_from_frame(uint8_t *ds, uint16_t dst, uint16_t src);

/* mem.c - packed-digit helpers 0x1B286, 0x1C6AA, 0x1DAE2 */
uint16_t record_high_byte_add5(uint8_t *ds);
uint16_t digit_0D009(uint8_t *ds, uint16_t arg);
uint16_t digit_at(uint8_t *ds, uint16_t ptr);

/* mem.c - 0x13B86/0x14BCC record index math, 0x16070, 0x09E84, 0x143D6 */
uint16_t record10_index_4x(uint8_t *ds, uint16_t base, uint16_t bias,
                           uint16_t *offset);
uint16_t record10_index_5x(uint8_t *ds, uint16_t base, uint16_t bias,
                           uint16_t *offset);
uint16_t base_plus_10x_index(uint8_t *ds, uint8_t index, uint16_t er6,
                             uint16_t *er10_out);
uint16_t word_at_2x_index(uint8_t *ds, uint16_t base, uint8_t index,
                          uint8_t *r2_out);
uint16_t nibble_pair_shifted(uint8_t *ds, uint16_t base, uint8_t *r10_out,
                             uint8_t *r11_out);
uint16_t record10_copy_if_present(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t complement32(uint8_t *ds, uint16_t low, uint16_t high,
                      uint16_t *out_high);
uint16_t stack_bytes_used(uint8_t *ds, uint16_t sp);

/* mem.c - 0x23F8A frame spill, 0x143D6 register-only shift */
uint16_t store_reg_to_stack(uint8_t *ds, uint16_t frame, const uint8_t *regs);
uint16_t high_nibble_into_r5(uint8_t *ds, uint16_t arg, uint8_t r4,
                             uint8_t *r5, uint8_t *r2);

/* mem.c - the 0xD000/0xD010 record handover */
uint16_t record_reset_flag38(uint8_t *ds);
uint16_t records_from_pointers(uint8_t *ds, uint16_t src_a, uint16_t src_b);
uint16_t record_to_pointer(uint8_t *ds);
uint16_t record10_er8_from_er4(uint8_t *ds, uint16_t src, uint16_t dst);
uint16_t init_struct_30(uint8_t *ds, uint16_t frame);
uint16_t record_kind_flag(uint8_t *ds, uint16_t arg);
uint16_t digit_or_next_byte(uint8_t *ds, uint16_t ptr);
uint16_t record10_index_5x_saved(uint8_t *ds, uint16_t base, uint16_t bias);
uint16_t memcpy_200(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t memcpy_50(uint8_t *ds, uint16_t dst, uint16_t src);
uint16_t mode_is_75_1(uint8_t *ds, uint16_t arg);
uint16_t mode_is_69_1(uint8_t *ds, uint16_t arg);

/* mem.c - small leaf accessors of the mode/variable cluster */
void store_0D10E(uint8_t *ds, uint16_t v);
uint16_t mode193_value(uint8_t *ds, uint16_t arg);
uint8_t var_mode_attr(uint8_t *ds);
void init_numeric_state(uint8_t *ds);

/* arith.c - the arithmetic core at 0x27000-0x31000 */
uint16_t row_count_over_limit(uint8_t *ds, uint8_t add);
uint16_t init_descriptor_record(uint8_t *ds, uint16_t dst);
uint16_t mul16(uint8_t *ds, uint16_t x, uint16_t y);

/* 0x2406C - how far a byte has to be shifted before its top bit leaves; the
 * count comes back in both halves of er0. */
uint16_t shift_until_carry(uint8_t *ds, uint16_t arg);
void draw_bar_frames(uint8_t *ds, uint8_t index);
uint16_t pitch_from_state(uint8_t *ds, uint16_t arg);
uint16_t key_repeat_gate(uint8_t *ds, uint16_t arg, const uint16_t *stk);
void print_indexed_rows(uint8_t *ds, uint16_t buffer);
uint16_t tall_metric_from_font(uint8_t *ds, uint16_t arg);
uint16_t small_metric_from_font(uint8_t *ds, uint16_t arg);
uint16_t reset_flags_and_state(uint8_t *ds, uint16_t arg);
void draw_submode_box(uint8_t *ds);

#endif /* NX_RUNTIME_H */
