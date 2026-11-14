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

/* 0x09FB8 - the routine clears the interrupt enable bit of 0xF014 and
 * rewrites the timer setup.  It ends with r1 = 160 and r0 = 2. */
uint16_t lcd_timer_poke(uint8_t *ds)
{
    ds[0xF014] &= (uint8_t)~0x04;
    ds[0xF008] = 80;
    ds[0xF009] = 160;
    ds[0xF00A] = 2;
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
