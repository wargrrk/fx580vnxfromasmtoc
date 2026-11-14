/* hw.c - hardware-register helpers of the fx-580VN X firmware.
 *
 * Every function here is a byte-for-byte translation of one ROM routine.  The
 * ROM origin is in the comment above it; `tests/t_hw.c` compares each one with
 * the real ROM on random state.
 */
#include "hw.h"

#include "runtime.h"

/* 0x099FC - `mov r0, 1; st r0, [0xF810]; rt` */
uint16_t set_flag_0F810(uint8_t *ds)
{
    ds[SFR_0F810] = 1;
    return 1;
}

/* 0x09A04 */
uint16_t set_flag_0F812(uint8_t *ds)
{
    ds[SFR_0F812] = 1;
    return 1;
}

/* 0x09A0C */
uint16_t set_flag_0F811(uint8_t *ds)
{
    ds[SFR_0F811] = 1;
    return 1;
}

/* 0x09A24 - the load only touches r0, so the high half of the result word is
 * the caller's r1; with a clean register file that is zero. */
uint16_t read_0F050(uint8_t *ds)
{
    return ds[SFR_0F050];
}

/* 0x0A128 - `mov r0, 0` then six stores, so er0 keeps a zero low byte. */
uint16_t screen_state_clear(uint8_t *ds)
{
    ds[SFR_DISP] = 0;
    ds[SFR_DISP1] = 0;
    ds[SFR_DISP2] = 0;
    ds[SFR_0F812] = 0;
    ds[SFR_0F811] = 0;
    ds[SFR_DISP3] = 0;
    return 0;
}

/* 0x0A10A - `push xr0` / `pop xr0` bracket the work, so the caller's er0 comes
 * back even though the routine reuses r0. */
uint16_t screen_mode_select(uint8_t *ds, uint16_t arg)
{
    if ((ds[RAM_0D114] & 0x10) == 0)
        ds[SFR_SCREEN_MODE] = 22;
    screen_state_clear(ds);
    return arg;
}

/* 0x0A152 */
uint16_t key_filter_all(uint8_t *ds)
{
    ds[0xF042] = 255;
    return 255;
}

/* 0x0A532 */
uint16_t flags_set_four(uint8_t *ds)
{
    ds[0xD134] = 1;
    ds[0xD135] = 1;
    ds[0xD136] = 1;
    ds[0xD16A] = 1;
    return 1;
}

/* 0x09FB8 - clear interrupt-enable bit 1 of 0xF014, then rewrite the timer
 * pair.  Two details the earlier reading got wrong:
 *
 *   `and r0, 253` is `& 0xFD`, so the bit that goes is bit 1, not bit 2.
 *   `st r0, [ea]` does *not* post-increment, so 80 at 0xF008 is overwritten by
 *   the 160 before `[ea+]` moves the pointer on: the surviving pair is
 *   0xF008 = 160 and 0xF009 = 2, and 0xF00A is never touched.
 *
 * The routine ends with r1 = 160 and r0 = 2, i.e. 0xA002. */
uint16_t lcd_timer_poke(uint8_t *ds)
{
    ds[0xF014] &= (uint8_t)253u;
    ds[0xF008] = 160;
    ds[0xF009] = 2;
    return (uint16_t)((160u << 8) | 2u);
}

/* 0x094FE - the two `st er0, [ea+]` stores clear 0xF800..0xF803, then the
 * modifier byte at 0xD110 decides which of the three flags comes back. */
uint16_t display_flags_from_modifiers(uint8_t *ds)
{
    uint8_t modifiers = ds[0xD110];

    ds[SFR_DISP] = 0;
    ds[SFR_DISP1] = 0;
    ds[0xF802] = 0;
    ds[SFR_DISP2] = 0;

    if (modifiers & 0x08)
        ds[SFR_DISP] = 1;
    if (modifiers & 0x04)
        ds[SFR_DISP1] = 1;
    if (modifiers & 0x01)
        ds[SFR_DISP2] = 1;
    return 1;
}

/* 0x0A878 */
uint16_t mode136_screen_state(uint8_t *ds)
{
    if (ds[0xD111] == 136 && ds[RAM_0D114] == 1)
        return 1;
    return 0;
}

/* 0x0AAD4 - r1 is left holding the font size when the mode 2 path runs, so the
 * result word carries it in the high byte. */
uint16_t cursor_rows_init(uint8_t *ds)
{
    ds[0xD157] = 1;
    ds[0xD15A] = 0;
    ds[0xD15B] = 0;

    if (ds[0xD111] != 2)
        return ds[0xD111];

    uint8_t font = ds[0xD137];
    ds[0xD157] = (uint8_t)(ds[0xD157] + font);
    uint8_t second = (uint8_t)(ds[0xD159] + font);
    ds[0xD159] = second;
    return (uint16_t)(((uint16_t)font << 8) | second);
}

/* 0x0A9DE */
uint16_t row_limit_for_mode(uint8_t *ds)
{
    uint8_t limit = 200;
    uint8_t mode = ds[0xD111];

    if (mode == 13) {
        limit = 50;
        if (ds[0xDDB0] == 1 && ds[0xD115] == 1)
            limit = 49;
    } else if (mode == 14) {
        limit = 50;
    }
    /* r1 still holds the limit when `mov r0, r1` runs, so both halves match. */
    return (uint16_t)(((uint16_t)limit << 8) | limit);
}

/* 0x0AC40 - the ROM compares the two bytes with `cmp r8, r1` (signed) and
 * subtracts in one direction or the other.  r1 is loaded from 0xD010 again
 * before the compare, so the result word carries that byte as its high half. */
uint16_t distance_from_0D010(uint8_t *ds, uint16_t value)
{
    uint8_t r8 = (uint8_t)value;
    uint8_t base = ds[0xD010];
    uint8_t result;

    /* `bc lt` tests the carry flag, i.e. an unsigned compare. */
    if (r8 < base)
        result = (uint8_t)(base - r8);
    else
        result = (uint8_t)(r8 - base);

    return (uint16_t)(((uint16_t)base << 8) | result);
}

/* 0x0E48C - see hw.h.  The ROM re-uses er0 for the `memzero` call, so the
 * cleared destination is what comes back in mode 12. */
uint16_t mode12_screen_reset(uint8_t *ds, uint16_t arg)
{
    /* Outside mode 12 the routine still falls through `l r0, [mode]`, so the
     * low half of the result is the mode byte, not the argument byte. */
    if (ds[MEM_MODE] != 12)
        return (uint16_t)((arg & 0xFF00u) | ds[MEM_MODE]);

    ds[0xD0F6] = 0;
    flags_set_four(ds);
    return memzero(ds, 0xD400u, 0x0640u);
}

/* 0x0C3B8 */
uint16_t mode3_screen_reset(uint8_t *ds, uint16_t arg)
{
    if (ds[MEM_MODE] != 3)
        return (uint16_t)((arg & 0xFF00u) | ds[MEM_MODE]);

    ds[0xD0F6] = 0;
    flags_set_four(ds);
    memzero(ds, 0xD400u, 0x0640u);
    ds[0xD142] = 0;
    ds[0xD164] = 0;
    ds[0xD165] = 0;
    return 0;
}

/* 0x2980E */
uint16_t mode136_row_clear(uint8_t *ds, uint16_t arg)
{
    if (ds[MEM_MODE] != 136)
        return (uint16_t)((arg & 0xFF00u) | ds[MEM_MODE]);

    ds[0xD152] = 0;
    return memzero(ds, 0xD84Cu, 200u);
}

/* 0x246D4 */
uint16_t editor_buffers_reset(uint8_t *ds)
{
    flags_set_four(ds);
    memzero(ds, 0xD0F8u, 0x0016u);
    return memzero(ds, 0xD392u, 0x03C0u);
}

/* 0x09FE0 - arm the 0xD13A flag byte: unless bit 7 is already set it becomes
 * 136 (0x88).  The result is 1 when the bit was set, 136 when it was written. */
uint16_t flag_13A_arm(uint8_t *ds)
{
    if (ds[0xD13A] & 0x80)
        return 1;
    ds[0xD13A] = 136;
    return 136;
}

/* 0x09FFE - just bit 7 of 0xD13A. */
uint16_t flag_13A_bit7(uint8_t *ds)
{
    return (ds[0xD13A] & 0x80) ? 1 : 0;
}

/* 0x2B3E2 - true when the three state bytes are 0, 0 and 1. */
uint16_t state_predicate_2B3E2(uint8_t *ds)
{
    if (ds[0xD0F5] != 0)
        return 0;
    if (ds[0xD113] != 0)
        return 0;
    return ds[0xD116] == 1 ? 1 : 0;
}

/* 0x1D94A - the high nibble of the byte at `addr`.  The mask is an 8-bit
 * `and r0, 240`, so only the low half of er0 is rewritten: the high byte of
 * the address argument survives into the result. */
uint16_t byte_high_nibble(uint8_t *ds, uint16_t addr)
{
    return (uint16_t)((addr & 0xFF00u) | (uint16_t)(ds[addr] & 0xF0u));
}

/* 0x2082C - the high nibble of the state byte at 0xD118. */
uint16_t state_high_nibble(uint8_t *ds)
{
    return (uint16_t)(ds[0xD118] >> 4);
}

/* 0x20834 - the low nibble of the state byte at 0xD118. */
uint16_t state_low_nibble(uint8_t *ds)
{
    return (uint16_t)(ds[0xD118] & 0x0Fu);
}

/* 0x20860 - store the low byte into 0xD118 and clear 0xD148; the routine then
 * zeroes er0 itself, so the caller always sees 0. */
uint16_t state_store(uint8_t *ds, uint16_t value)
{
    ds[0xD118] = (uint8_t)value;
    ds[0xD148] = 0;
    return 0;
}

/* 0x14C9C - add 0xFF34, i.e. subtract 0xCC, from the argument. */
uint16_t offset_minus_cc(uint8_t *ds, uint16_t value)
{
    (void)ds;
    return (uint16_t)(value + 0xFF34u);
}

/* 0x09FD8 - the byte at 0xF014, widened into er0. */
uint16_t read_0F014(uint8_t *ds)
{
    return ds[0xF014];
}

/* 0x29978 - always returns 4 and touches nothing. */
uint16_t constant_four(uint8_t *ds)
{
    (void)ds;
    return 4;
}

/* 0x2083C - fold the low nibble of the argument into the high nibble of the
 * state byte, keeping the byte's existing low nibble.  Every step is 8-bit
 * (`and r1, 15`, `sll r0, 4`, `or r1, r0`), so the result word is the shifted
 * argument in the low half and the merged byte in the high half. */
uint16_t state_nibble_merge(uint8_t *ds, uint16_t value)
{
    uint8_t merged = (uint8_t)((ds[0xD118] & 0x0Fu) | ((uint8_t)value << 4));

    ds[0xD118] = merged;
    return (uint16_t)(((uint16_t)merged << 8) | (uint8_t)((uint8_t)value << 4));
}

/* 0x28FB0 - 1 when the low byte of the argument is 2, else 0. */
uint16_t value_is_two(uint8_t *ds, uint16_t value)
{
    (void)ds;
    return ((uint8_t)value == 2) ? 1 : 0;
}

/* 0x2F622 - 1 when the low byte of the argument is 3, else 0. */
uint16_t value_is_three(uint8_t *ds, uint16_t value)
{
    (void)ds;
    return ((uint8_t)value == 3) ? 1 : 0;
}

/* 0x2F9F6 - bit 7 of the modifier byte at 0xD110.  The answer is built with
 * `mov r0, ...` alone, so r1 keeps the caller's high half: with a non-zero
 * high byte the returned word is 0xNN00 or 0xNN01, not 0 or 1. */
uint16_t modifier_bit7(uint8_t *ds, uint16_t arg)
{
    uint16_t flag = ((ds[0xD110] & 0x80u) != 0) ? 1u : 0u;

    return (uint16_t)((arg & 0xFF00u) | flag);
}

/* 0x2FA02 - the byte at 0xD156 added to the word at 0xD00C. */
uint16_t counter_sum_0D156(uint8_t *ds)
{
    uint16_t base = (uint16_t)(ds[0xD00C] | ((uint16_t)ds[0xD00D] << 8));

    return (uint16_t)(ds[0xD156] + base);
}

/* 0x18940 - the argument times ten, walked from the 0xDA40 table base.
 * `mul er0, r2` multiplies the *low bytes* only: 0x0100 gives a product of 0
 * and 0x7FFF gives the same table entry as 0xFF. */
uint16_t record_10b_0DA40(uint8_t *ds, uint16_t value)
{
    (void)ds;
    return (uint16_t)((uint16_t)((uint8_t)value * 10u) + 0xDA40u);
}

/* 0x09D42 - zero 0xD116, 0xD117, 0xD118, 0xD119 and 0xD148; er0 comes back 0. */
uint16_t state_reset_0D116(uint8_t *ds)
{
    ds[0xD116] = 0;
    ds[0xD117] = 0;
    ds[0xD119] = 0;
    ds[0xD118] = 0;
    ds[0xD148] = 0;
    return 0;
}

/* 0x1826E - the byte of the ten-byte record at 0xDA40 for this index. */
uint16_t table_byte_10b(uint8_t *ds, uint16_t value)
{
    uint16_t addr = record_10b_0DA40(ds, value);

    /* `l r0, [er0]` loads a byte and leaves r1 alone, so the high half of the
     * record address survives into the result (0xDE11, not 0x0011). */
    return (uint16_t)((addr & 0xFF00u) | ds[addr]);
}

/* 0x0E1D2 - swap the two nibbles of the state byte at 0xD118.  The `mov r1,
 * r0` between the shift and the merge (not `mov r1, 0`) is what puts the old
 * high nibble in the returned word's high half, so the result is the moved
 * nibble above and the new state byte below. */
uint16_t state_swap_nibbles(uint8_t *ds)
{
    uint8_t old = ds[0xD118];
    uint8_t high = (uint8_t)(old >> 4);
    uint8_t merged = (uint8_t)((old & 0xF0u) | high);

    ds[0xD118] = merged;
    return (uint16_t)(((uint16_t)high << 8) | merged);
}

/* 0x2084C - fold the argument's low byte into the state byte at 0xD118, keep
 * that byte's high nibble, and clear 0xD148.  The final `mov r0, 0` only zeroes
 * the low half of er0, so the returned word is the *kept* high nibble shifted
 * up (0xF000 for a 0xF1 state byte), not 0. */
uint16_t state_merge_high(uint8_t *ds, uint16_t value)
{
    uint8_t old = ds[0xD118];
    uint8_t kept = (uint8_t)(old & 0xF0u);

    ds[0xD118] = (uint8_t)((uint8_t)value | kept);
    ds[0xD148] = 0;
    return (uint16_t)((uint16_t)kept << 8);
}

/* 0x20DD2 - drop a latched shift/repeat bit from the modifier byte at 0xD110:
 * `add r0, 128` wraps a set bit 7 away and `rb r0, 3` clears bit 3.  A byte
 * whose bit 7 is already clear is left alone and not even written back. */
uint16_t modifier_clear_bit7(uint8_t *ds, uint16_t arg)
{
    uint8_t value = ds[0xD110];

    if (value & 0x80u) {
        value = (uint8_t)(value + 0x80u);
        value = (uint8_t)(value & (uint8_t)~0x08u);
        ds[0xD110] = value;
    }
    return (uint16_t)((arg & 0xFF00u) | value);
}

/* 0x206FA - the separator glyph for the current display state: 0x2E ('.')
 * while either the extended-mode byte at 0xD14E or the flag at 0xD11C is set,
 * 0x2C (',') when neither is.  r1 keeps the byte that decided it, so the
 * returned *high* half is that byte (0x0046 vs 0x0146 for 0xD14E = 1). */
uint16_t dot_or_prime(uint8_t *ds)
{
    uint8_t high = ds[0xD14E];

    if (high == 0)
        high = ds[0xD11C];
    return (uint16_t)(((uint16_t)high << 8) | (high != 0 ? 0x2Eu : 0x2Cu));
}

/* 0x180D0 - the "nothing to report" prologue: er0, er2, er4 and er6 come back
 * zero and the r8/r9 pair is loaded with 0 and 1.  Only er0 is observable
 * through the calling convention - the rest are register-only side effects the
 * differential test cannot see - so the C body only has to answer 0. */
uint16_t clear_call_registers(uint8_t *ds)
{
    (void)ds;
    return 0;
}

/* 0x0A146 - display enable: the byte at 0xF815 becomes 1 when the argument is
 * non-zero and 0 when it is, and er0 answers with the flag in its low half.  It
 * is the label inside 0x0A128's body (`.l_01E`), reached by `bl` from the
 * routines that switch the panel on and off around a delay.
 *
 * `cmp r0, 0` is an *eight-bit* compare, so only the low half of the argument
 * decides - 0x0100 switches the display off, not on. */
uint16_t display_enable_set(uint8_t *ds, uint16_t arg)
{
    uint8_t on = (uint8_t)((uint8_t)arg != 0);

    ds[0xF815] = on;
    return (uint16_t)((arg & 0xFF00u) | on);
}

/* 0x1844E - "is the byte at offset 8 of this record above 14?".  The record is
 * addressed through 0x18940, and the answer goes into the low half of er0 while
 * the high half keeps the top byte of the record pointer. */
uint16_t record_tail_over_14(uint8_t *ds, uint16_t value)
{
    uint16_t record = record_10b_0DA40(ds, value);
    uint8_t tail = ds[(uint16_t)(record + 8)];

    return (uint16_t)((record & 0xFF00u) | (tail > 14 ? 1u : 0u));
}

/* 0x0CB0E - a body that is nothing but `rt`.  Six call sites reach it through
 * `bl`, so it is a real entry and not a hole: it does nothing and hands the
 * caller's er0 straight back, which is exactly what the identity does. */
uint16_t no_op_0CB0E(uint8_t *ds, uint16_t arg)
{
    (void)ds;
    return arg;
}

/* 0x1FFFE - the same thing written as `pop pc`.  Five `bl` sites call it as a
 * plain "do nothing and return" helper, and since `bl` pushes the return
 * address the pop goes back to the caller. */
uint16_t return_immediately_1FFFE(uint8_t *ds, uint16_t arg)
{
    (void)ds;
    return arg;
}
