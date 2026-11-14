/*
 * arith.c - the arithmetic core at 0x27000-0x31000.
 *
 * This region is the packed-BCD number engine: digit loops over the ten-byte
 * records at 0xD000/0xD010, the table of constants, and the helpers that decide
 * how a number is laid out.  There is no DAA/DAS in the region at all - the
 * digit work is done with byte-decrement loops and carry chains (`addc`,
 * `cmpc`, `sllc`), which is why the C bodies here are plain byte loops rather
 * than decimal adjust emulation.
 *
 * Conventions already established elsewhere in the project apply unchanged:
 * the data space is one flat 64 KiB window, only 0xD000 and above latches a
 * store, and multi-byte loads/stores force word alignment by ignoring address
 * bit 0.
 */
#include "runtime.h"

#include "draw.h"
#include "hw.h"

#define RAM_START 0xD000u

static void ds_store(uint8_t *ds, uint16_t addr, uint8_t value)
{
    /* the full map, not just the RAM base: a count that runs past 0xF0FF walks
     * into open bus and past 0xFFFF wraps into ROM, and the chip drops both */
    ds_put(ds, addr, value);
}

/* 0x27E60 - "is the running count still under the limit?".  The count byte at
 * 0xD143 is widened to a word, the argument is added, and the result is
 * compared against 181 with `bc le`, which is an *unsigned* test.
 *
 * The return is the two halves of different things, exactly as the ROM leaves
 * them: `mov r0, r2` writes only the low byte, so r1 still holds the high byte
 * of the 16-bit sum while r0 carries the flag.  For counts that overflow the
 * word (only possible with a byte count plus a byte argument, i.e. 0x0100 and
 * up) the returned word is 0x0100 or 0x0101 rather than 0 or 1. */
uint16_t row_count_over_limit(uint8_t *ds, uint8_t add)
{
    uint16_t sum = (uint16_t)(ds[0xD143] + add);

    return (uint16_t)(((sum >> 8) << 8) | (sum > 181u ? 1u : 0u));
}

/* 0x2F1C4 - initialise the ten-byte descriptor record at `dst`: zero it, then
 * put 4 / 41 / 73 / 103 / 41 / 96 in the first six bytes and the word 0x0109 at
 * offset 8, leaving 6 and 7 zero.
 *
 * The last store is word-wide, so on an odd `dst` it lands at dst+7 instead of
 * dst+8 - the alignment rule applies to the effective address after the
 * offset.  The returned word is 0x0109 because r0 and r1 still hold the 9 and
 * 1 that the final store wrote. */
uint16_t init_descriptor_record(uint8_t *ds, uint16_t dst)
{
    static const uint8_t head[6] = {4, 41, 73, 103, 41, 96};

    memzero(ds, dst, 10);
    for (int i = 0; i < 6; i++)
        ds_store(ds, (uint16_t)(dst + i), head[i]);

    uint16_t word_at = (uint16_t)((uint16_t)(dst + 8) & 0xFFFEu);
    ds_store(ds, word_at, 0x09);
    ds_store(ds, (uint16_t)(word_at + 1), 0x01);
    return 0x0109;
}

/* 0x2E812 - lay out the one-pixel frames of a bar gauge: a stroke at x = 19
 * and one at x = 61 always, a third at 103 from index 2 and a fourth at 145
 * from index 3.  Every stroke starts on row 1 and runs to row 48, so the
 * argument the ROM passes is xy = 0x0113 / 0x013D / 0x0167 / 0x0191 and
 * spec = 0x0030.
 *
 * `cmp r8, 2 ; bc lt` is unsigned, so the threshold tests are plain `<`.  The
 * routine has no result of its own - the caller sees whatever the frame
 * routine left in er0 - which is why the manifest marks it with `!`. */
void draw_bar_frames(uint8_t *ds, uint8_t index)
{
    static const uint8_t spec = 0x30;   /* vertical extent, rows 1..48 */

    box_frame_thin(ds, 0x0113, spec);
    box_frame_thin(ds, 0x013D, spec);
    if (index >= 2)
        box_frame_thin(ds, 0x0167, spec);
    if (index >= 3)
        box_frame_thin(ds, 0x0191, spec);
}

/* 0x2EB00 - a constant chosen by "is the byte it reads 10?", written as the
 * `r1 = 11 (or 6); r0 = r1` pair the ROM uses.  Because only r0 is
 * overwritten at the end, both halves of the result carry the constant:
 * 0x0B0B / 0x0606.  See 0x27E86 for the same computation on another byte. */
uint16_t pitch_from_state(uint8_t *ds, uint16_t arg)
{
    (void)arg;
    uint8_t value = ds[0xD133] == 10 ? 6 : 11;

    return (uint16_t)(((uint16_t)value << 8) | value);
}

/* 0x27E86 - the 0x2EB00 computation with 0x0707 / 0x0505 as its constants. */
uint16_t tall_metric_from_font(uint8_t *ds, uint16_t arg)
{
    (void)arg;
    uint8_t value = ds[0xD137] == 10 ? 6 : 11;

    return (uint16_t)(((uint16_t)value << 8) | value);
}

/* 0x2805E - the same shape with 7 / 5 and the font-size byte at 0xD137. */
uint16_t small_metric_from_font(uint8_t *ds, uint16_t arg)
{
    (void)arg;
    uint8_t value = ds[0xD137] == 10 ? 5 : 7;

    return (uint16_t)(((uint16_t)value << 8) | value);
}

/* 0x2AB3C - set the four "first time through" flags and then clear the five
 * state bytes.  The routine returns nothing of its own: 0x09D42 zeroes r0 but
 * never touches r1, so the caller's high half survives into the result and the
 * word is (arg & 0xFF00). */
uint16_t reset_flags_and_state(uint8_t *ds, uint16_t arg)
{
    flags_set_four(ds);
    state_reset_0D116(ds);
    return (uint16_t)(arg & 0xFF00u);
}

/* 0x2F62E - draw the rounded box that belongs to the active submode.  Only
 * submodes 1, 2 and 3 have one; anything else leaves the screen alone.
 *
 * The geometry is four bytes of ROM at 0x01AB4 + 4 * submode: the first two
 * are the xy word, the last two are the spec word.  They are read through the
 * data space like any other pointer, so this stays faithful to the table the
 * firmware ships with instead of copying the numbers into the C file. */
void draw_submode_box(uint8_t *ds)
{
    uint8_t submode = ds[0xD112];

    if (submode != 1 && submode != 2 && submode != 3)
        return;

    uint16_t off = (uint16_t)((uint16_t)submode << 2);
    uint16_t xy = (uint16_t)(ds[(uint16_t)(0x01AB4u + off)] |
                             ((uint16_t)ds[(uint16_t)(0x01AB5u + off)] << 8));
    uint16_t spec = (uint16_t)(ds[(uint16_t)(0x01AB6u + off)] |
                               ((uint16_t)ds[(uint16_t)(0x01AB7u + off)] << 8));

    box_round_rect(ds, xy, spec);
}
