/* t_record.c - differential tests for the small record/table helpers in mem.c */
#include "batch.h"

#include "../src/runtime.h"

W1(record_template_06)
W1(record_template_03)
W2S1(table_slot_store)
W2(string_copy_with_nul)
W2S1(expand_glyph_mask)
W2S2(table_slot_fold)

void test_record(void)
{
    BATCH(
        {"record_template_06", 0x0E5A0, "p", a_record_template_06, 0},
        {"record_template_03", 0x2F0B4, "p", a_record_template_03, 0},
        {"table_slot_store", 0x2502A, "wp|b", a_table_slot_store, 0},
        {"string_copy_with_nul", 0x0E1E8, "pp", a_string_copy_with_nul, 0},
        {"expand_glyph_mask", 0x2B334, "pp|p", a_expand_glyph_mask, 0},
        {"table_slot_fold", 0x0CD86, "ww|ww!", a_table_slot_fold, 0},
    );
}
