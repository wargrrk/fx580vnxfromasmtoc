/* t_hw.c - differential tests for the hardware-register helpers. */
#include "batch.h"
#include "harness.h"

#include "../src/hw.h"
#include "../src/runtime.h"

W0(set_flag_0F810)
W0(set_flag_0F812)
W0(set_flag_0F811)
W0(read_0F050)
W0(screen_state_clear)
W1(screen_mode_select)
W0(key_filter_all)
W0(flags_set_four)
W0(lcd_timer_poke)
W0(display_flags_from_modifiers)
W0(mode136_screen_state)
W0(cursor_rows_init)
W0(row_limit_for_mode)
W1(distance_from_0D010)
W1(mode12_screen_reset)
W1(mode3_screen_reset)
W1(mode136_row_clear)
W0(editor_buffers_reset)
W0(flag_13A_arm)
W0(flag_13A_bit7)
W0(state_predicate_2B3E2)
W1(byte_high_nibble)
W0(state_high_nibble)
W0(state_low_nibble)
W1(state_store)
W1(offset_minus_cc)
W0(read_0F014)
W0(constant_four)
W1(state_nibble_merge)
W1(value_is_two)
W1(value_is_three)
W1(modifier_bit7)
W0(counter_sum_0D156)
W1(record_10b_0DA40)
W0(state_reset_0D116)
W1(table_byte_10b)

W0(state_swap_nibbles)
W1(state_merge_high)
W1(modifier_clear_bit7)
W0(dot_or_prime)
W0(clear_call_registers)
W1(no_op_0CB0E)
W1(display_enable_set)
W1(record_tail_over_14)

/* 0x1FFFE is a bare `pop pc`.  Called through `bl` it pops the return address
 * the call pushed, so to run it the harness has to put one on the stack - and
 * `pop pc` pops *four* bytes (pc then csr), so the word after the sentinel has
 * to be a zero csr or the core keeps running in segment garbage.  This is why
 * the case cannot go through the batch runner: no shape can describe it. */
static void test_pop_pc_return(void)
{
    static const uint16_t args[] = {0, 1, 0x1234, 0xFFFF};

    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
        random_image((uint32_t)(0xC7000 + i));
        Args a = {.er = {args[i], 0}, .stk = {NX8_SENTINEL, 0}, .nstk = 2};
        uint16_t rom_ret = run_rom(0x1FFFE, &a, "return_immediately_1FFFE");
        begin_c();
        uint16_t c_ret = return_immediately_1FFFE(after_c, args[i]);
        check_ret("return_immediately_1FFFE", rom_ret, c_ret);
        check_mem_all("return_immediately_1FFFE");
    }
}

/* 0x0AF1A, 0x0C34E, 0x2AF28 and 0x2D4D8 are byte predicates that answer in r0
 * and leave r1 alone, so each one is run over the interesting values of its
 * state byte and a high half that has to survive. */
struct pred_case {
    uint32_t addr;
    const char *name;
    uint16_t (*fn)(uint8_t *, uint16_t);
    uint16_t cell;
};

static void one_predicate(const struct pred_case *c, uint8_t value,
                          uint16_t arg, uint8_t other)
{
    static const uint16_t other_cells[] = {0xD114, 0xD116};

    random_image(0xF6000u + (unsigned)value * 16u + (unsigned)other);
    /* the cell under test gets the sweept value, the other two bytes (0xD114
     * and 0xD116) get a value that is deliberately not always equal to it */
    image[c->cell] = value;
    image[other_cells[0]] = other;
    image[other_cells[1]] = other;
    Args args = {.er = {arg, 0}, .nstk = 0};
    uint16_t rom_ret = run_rom(c->addr, &args, c->name);
    begin_c();
    check_ret(c->name, rom_ret, c->fn(after_c, arg));
    check_mem_all(c->name);
}

static void test_state_predicates(void)
{
    static const uint16_t args[] = {0, 0x0023, 0x2300, 0xFFFF};
    static const uint8_t others[] = {0, 1, 2};
    static const struct pred_case cases[] = {
        {0x0AF1A, "is_zero_or_35", is_zero_or_35, 0xD015},
        {0x0C34E, "submode_is_19_or_20", submode_is_19_or_20, 0xD114},
        {0x2AF28, "state_is_1_or_3", state_is_1_or_3, 0xD015},
        {0x2D4D8, "flag4_and_substate_1", flag4_and_substate_1, 0xD114},
    };
    static const uint8_t values[] = {0, 1, 2, 3, 19, 20, 21, 0x10, 0x11, 0xFF};

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
            for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); a++) {
                for (size_t o = 0; o < sizeof(others); o++)
                    one_predicate(&cases[k], values[v], args[a], others[o]);
            }
        }
    }
}

void test_hw(void)
{
    test_state_predicates();

    BATCH(
        {"set_flag_0F810", 0x099FC, "", a_set_flag_0F810, 0},
        {"set_flag_0F812", 0x09A04, "", a_set_flag_0F812, 0},
        {"set_flag_0F811", 0x09A0C, "", a_set_flag_0F811, 0},
        {"read_0F050", 0x09A24, "", a_read_0F050, 0},
        {"screen_state_clear", 0x0A128, "", a_screen_state_clear, 0},
        {"screen_mode_select", 0x0A10A, "w", a_screen_mode_select, 0},
        {"key_filter_all", 0x0A152, "", a_key_filter_all, 0},
        {"flags_set_four", 0x0A532, "", a_flags_set_four, 0},
        {"lcd_timer_poke", 0x09FB8, "", a_lcd_timer_poke, 0},
        {"display_flags", 0x094FE, "", a_display_flags_from_modifiers, 0},
        {"mode136_screen_state", 0x0A878, "", a_mode136_screen_state, 0},
        {"cursor_rows_init", 0x0AAD4, "", a_cursor_rows_init, 0},
        {"row_limit_for_mode", 0x0A9DE, "", a_row_limit_for_mode, 0},
        {"distance_from_0D010", 0x0AC40, "b", a_distance_from_0D010, 0},
        {"mode12_screen_reset", 0x0E48C, "w", a_mode12_screen_reset, 0},
        {"mode3_screen_reset", 0x0C3B8, "w", a_mode3_screen_reset, 0},
        {"mode136_row_clear", 0x2980E, "w", a_mode136_row_clear, 0},
        {"editor_buffers_reset", 0x246D4, "", a_editor_buffers_reset, 0},
        {"flag_13A_arm", 0x09FE0, "", a_flag_13A_arm, 0},
        {"flag_13A_bit7", 0x09FFE, "", a_flag_13A_bit7, 0},
        {"state_predicate_2B3E2", 0x2B3E2, "", a_state_predicate_2B3E2, 0},
        {"byte_high_nibble", 0x1D94A, "p", a_byte_high_nibble, 0},
        {"state_high_nibble", 0x2082C, "", a_state_high_nibble, 0},
        {"state_low_nibble", 0x20834, "", a_state_low_nibble, 0},
        {"state_store", 0x20860, "b", a_state_store, 0},
        {"offset_minus_cc", 0x14C9C, "w", a_offset_minus_cc, 0},
        {"read_0F014", 0x09FD8, "", a_read_0F014, 0},
        {"constant_four", 0x29978, "", a_constant_four, 0},
        {"state_nibble_merge", 0x2083C, "b", a_state_nibble_merge, 0},
        {"value_is_two", 0x28FB0, "b", a_value_is_two, 0},
        {"value_is_three", 0x2F622, "b", a_value_is_three, 0},
        {"modifier_bit7", 0x2F9F6, "w", a_modifier_bit7, 0},
        {"counter_sum_0D156", 0x2FA02, "", a_counter_sum_0D156, 0},
        {"record_10b_0DA40", 0x18940, "w", a_record_10b_0DA40, 0},
        {"state_reset_0D116", 0x09D42, "", a_state_reset_0D116, 0},
        {"table_byte_10b", 0x1826E, "w", a_table_byte_10b, 0},
        {"state_swap_nibbles", 0x0E1D2, "", a_state_swap_nibbles, 0},
        {"state_merge_high", 0x2084C, "b", a_state_merge_high, 0},
        {"modifier_clear_bit7", 0x20DD2, "b", a_modifier_clear_bit7, 0},
        {"dot_or_prime", 0x206FA, "", a_dot_or_prime, 0},
        {"clear_call_registers", 0x180D0, "", a_clear_call_registers, 0},
        {"no_op_0CB0E", 0x0CB0E, "w", a_no_op_0CB0E, 0},
        {"display_enable_set", 0x0A146, "w", a_display_enable_set, 0},
        {"record_tail_over_14", 0x1844E, "w", a_record_tail_over_14, 0},
    );

    test_pop_pc_return();
}
