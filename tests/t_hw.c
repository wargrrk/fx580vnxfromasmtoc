/* t_hw.c - differential tests for the hardware-register helpers. */
#include "batch.h"

#include "../src/hw.h"

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

void test_hw(void)
{
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
    );
}
