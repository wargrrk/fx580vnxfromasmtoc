/*
 * mem.c - block memory routines from the fx-580VN X firmware.
 *
 * The count is a 16-bit value passed on the stack.  The ROM's loops are
 * count-first (`while (n > 0) { ...; n--; }`) and use `cmp 0, n ; bc lt`,
 * which for every non-zero 16-bit count takes the branch, i.e. the count is
 * effectively unsigned.
 */
#include "runtime.h"

/* The data space is a flat 64 KiB window but only 0xD000-0xEFFF latches a
 * store: below 0xD000 sits the ROM (nx8_write drops those) and above 0xEC00 the
 * stack guard.  A copy whose count runs off the end of the segment wraps into
 * ROM addresses, so the wrapped half of the copy has to disappear the same way.
 * Reads still see the ROM bytes, which is why only the store side is guarded. */
#define RAM_START 0xD000u

static void ds_store(uint8_t *ds, uint16_t addr, uint8_t value)
{
    /* the full map, not just the RAM base: a count that runs past 0xF0FF walks
     * into open bus and past 0xFFFF wraps into ROM, and the chip drops both */
    ds_put(ds, addr, value);
}

static void ds_store16(uint8_t *ds, uint16_t addr, uint16_t value)
{
    ds_store(ds, addr, (uint8_t)value);
    ds_store(ds, (uint16_t)(addr + 1), (uint8_t)(value >> 8));
}

static uint16_t ds_load16(const uint8_t *ds, uint16_t addr)
{
    return (uint16_t)(ds[addr] | ((uint16_t)ds[(uint16_t)(addr + 1)] << 8));
}

/* 0x26FFA */
uint16_t memcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n)
{
    uint16_t d = dst;
    uint16_t s = src;
    while (n > 0) {
        ds_store(ds, d++, ds[s++]);
        n--;
    }
    return dst;
}

/* 0x0A8AA - overlap-safe copy; the ROM picks the direction from the signed
 * 16-bit ordering of the pointers and of src + n. */
uint16_t memmove_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n)
{
    uint16_t d = dst;
    uint16_t s = src;

    if ((int16_t)src >= (int16_t)dst || (int16_t)dst >= (int16_t)(uint16_t)(src + n)) {
        /* forward */
        while (n > 0) {
            ds_store(ds, d++, ds[s++]);
            n--;
        }
    } else {
        /* backward */
        d += n;
        s += n;
        while (n > 0) {
            d--;
            s--;
            ds_store(ds, d, ds[s]);
            n--;
        }
    }
    return dst;
}

/* 0x1EB94 */
uint16_t memset_n(uint8_t *ds, uint16_t dst, uint8_t value, uint16_t n)
{
    uint16_t d = dst;
    while (n > 0) {
        ds_store(ds, d++, value);
        n--;
    }
    return dst;
}

/* 0x09D34 - memset(dst, 0, n) */
uint16_t memzero(uint8_t *ds, uint16_t dst, uint16_t n)
{
    return memset_n(ds, dst, 0, n);
}

/* 0x2F832 - shift offsets in later table entries whose keys are in range. */
void shift_range_offsets(uint8_t *ds, uint16_t delta, uint8_t lower,
                         uint8_t start)
{
    uint8_t count = ds[0xD169];
    uint8_t upper = ds[(uint16_t)(0xDBE7u + (uint16_t)start * 8)];

    for (uint8_t index = (uint8_t)(start + 1); index < count; index++) {
        uint16_t entry = (uint16_t)(0xDBE4u + (uint16_t)index * 8);
        uint8_t key = ds[(uint16_t)(entry + 2)];

        if (key >= lower && key < upper) {
            uint16_t offset = (uint16_t)(ds[entry] |
                                         ((uint16_t)ds[(uint16_t)(entry + 1)] << 8));
            offset = (uint16_t)(offset + delta);
            ds[entry] = (uint8_t)offset;
            ds[(uint16_t)(entry + 1)] = (uint8_t)(offset >> 8);
        }
    }
}

/* 0x1CA78 - compare two packed-BCD records after normalizing the second. */
uint8_t compare_bcd_records(uint8_t *ds)
{
    uint8_t first_status = ds[0xD009];
    uint8_t second_status = ds[0xD019];

    if (first_status >= 0xF0 || second_status >= 0xF0)
        return 0;

    uint8_t first_digit = ds[0xD001];
    uint8_t second_digit = ds[0xD011];
    uint8_t first_high = first_digit >= 5;
    uint8_t second_high = second_digit >= 5;
    if (first_high != second_high) {
        if (first_high) {
            second_digit = (uint8_t)((second_digit + 5) % 10);
        } else {
            do {
            second_digit = (uint8_t)((second_digit + 5) % 10);
            } while (second_digit >= 5);
        }
        ds[0xD011] = second_digit;
        ds[0xD00A] = 6;
    } else {
        ds[0xD00A] = 0;
    }

    uint16_t first_word = (uint16_t)(ds[0xD000] | ((uint16_t)ds[0xD001] << 8));
    uint16_t second_word = (uint16_t)(ds[0xD010] | ((uint16_t)ds[0xD011] << 8));
    if (first_word < second_word)
        return 1;
    if (first_word > second_word)
        return 0;

    for (int16_t i = 9; i >= 2; i--) {
        uint8_t first = ds[(uint16_t)(0xD000 + i)];
        uint8_t second = ds[(uint16_t)(0xD010 + i)];
        if (first < second)
            return 1;
        if (first > second)
            return 0;
    }
    return 0;
}

static uint8_t bcd_subtract(uint8_t left, uint8_t right)
{
    int low = (left & 0x0F) - (right & 0x0F);
    int borrow = low < 0;
    if (borrow)
        low -= 6;
    int high = (left >> 4) - (right >> 4) - borrow;
    if (high < 0)
        high -= 6;
    return (uint8_t)(((high << 4) & 0xF0) | (low & 0x0F));
}

/* 0x19FD8 - validate the packed value's digit count and decimal range. */
uint8_t decimal_record_predicate(uint8_t *ds)
{
    uint8_t status = ds[0xD009];
    if (status == 0)
        return 0;
    if (status >= 10)
        return 1;

    uint8_t zero_digits = 0;
    uint16_t address = 0xD002;
    for (uint8_t remaining = 8; remaining != 0; remaining--) {
        uint8_t packed = ds[address];
        if (packed & 0x0F)
            break;
        zero_digits++;
        if (packed != 0)
            break;
        zero_digits++;
        address++;
    }
    if (zero_digits >= 10)
        zero_digits = (uint8_t)(zero_digits + 6);

    uint8_t high = ds[0xD001];
    if (high >= 5)
        high = bcd_subtract(high, 5);
    if (high == 0)
        return 1;

    uint8_t low = ds[0xD000];
    if (low > 9)
        return 1;

    uint8_t remaining = bcd_subtract(20, low);
    return zero_digits < remaining;
}

#define MODE_ADDR 0xD111   /* mode byte: <58 normal, 68/69 comp?, 196 ... */

/* the little state bytes the cluster keeps beside the mode byte */
#define SUBMODE_ADDR  0xD112  /* submode byte, one past the mode byte */
#define CELL_SEL_ADDR 0xD115  /* cell offset byte inside a descriptor row */
#define SKEW_ADDR     0xD14F  /* non-zero: apply the +4 row skew (0x09E24) */
#define KEEP_ADDR     0xD10E  /* 16-bit word the runtime parks between calls */
#define MODE193_ADDR  0xD123  /* the value mode 193 reports through 0x09E6E */

/* 0x09DE6 - assign_var.  Store the ten-byte variable value at `src` into
 * the variable slot area: slot `slot` (the r0 argument) lives at
 * 0xD31A + 10*slot.  In the matrix/vector modes (mode byte 69 or 196 at
 * 0xD111) a second ten-byte half is stored beside the first, from src+10
 * into 0xD612 + 10*slot - the double-width slots those modes keep.
 *
 * The return value is the ROM's ABI accident, kept as-is: the byte counts
 * the caller passes to _memcpy_nn ride on the stack, and the lib burns them
 * down to zero in place, so the trailing `pop er0` in the widened path
 * yields 0.  In the basic path the last value loaded into r0 is the byte
 * at 0xD111, and er0 becomes just that: no copy happened, the "result" is
 * the mode byte itself. */
uint16_t assign_var(uint8_t *ds, uint8_t slot, uint16_t src)
{
    uint16_t off = (uint16_t)(10u * slot);

    memcpy_nn(ds, (uint16_t)(0xD31A + off), src, 10);

    uint8_t mode = ds[MODE_ADDR];
    if (mode == 69 || mode == 196) {
        memcpy_nn(ds, (uint16_t)(0xD612 + off), (uint16_t)(src + 10), 10);
        return 0;
    }
    return mode;
}

/* 0x09D62 - park a 16-bit word at 0xD10E.  Two-instruction leaf: the store
 * uses er0's register pair, so ds[0xD10E] takes the low byte and ds[0xD10F]
 * the high one, and er0 leaves untouched. */
void store_0D10E(uint8_t *ds, uint16_t v)
{
    ds[KEEP_ADDR] = (uint8_t)v;
    ds[KEEP_ADDR + 1] = (uint8_t)(v >> 8);
}

/* 0x09E6E - one-byte attribute of the active mode.  Only mode 193 carries a
 * payload (the byte at 0xD123); every other mode reports 0.  The ROM leaves
 * er0's high byte (the incoming r1) untouched, so the C model keeps the
 * argument pair and mirrors that. */
uint16_t mode193_value(uint8_t *ds, uint16_t arg)
{
    uint16_t hi = (uint16_t)(arg & 0xFF00u);

    if (ds[MODE_ADDR] != 193)
        return hi;                          /* mov r0, 0 */
    return (uint16_t)(hi | ds[MODE193_ADDR]);
}

/* 0x09E24 - one byte of the per-submode descriptor table at 0x38E6 in ROM.
 * The submode byte at 0xD112 picks the row (5 bytes each, rows counted from
 * one); in submodes of 4 and up with 0xD14F non-zero the row count shifts
 * by 4 (the skew the alternate layers use), and the cell inside the row is
 * the byte at 0xD115.  The routine ignores its incoming registers, and it
 * zeroes r1 itself before the load, so the result is a clean byte. */
uint8_t var_mode_attr(uint8_t *ds)
{
    uint8_t row = ds[SUBMODE_ADDR];

    if (row >= 4 && ds[SKEW_ADDR] != 0)
        row = (uint8_t)(row + 4);
    row = (uint8_t)(row - 1);

    uint16_t cell = (uint16_t)(5u * row);
    cell = (uint16_t)(cell + 0x38E6u);
    cell = (uint16_t)(cell + ds[CELL_SEL_ADDR]);
    return ds[cell];
}

/* 0x1B124 - initialize the numeric mode's ten-byte state block. */
void init_numeric_state(uint8_t *ds)
{
    static const uint8_t low_mode[] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 9};
    static const uint8_t vector_mode[] = {0, 1, 144, 148, 103, 50, 150, 7, 87, 1};
    static const uint8_t other_mode[] = {2, 1, 0, 0, 0, 0, 0, 0, 0, 1};
    const uint8_t *state = ds[0xD02F] < 5 ? low_mode
                         : ds[0xD02F] == 5 ? vector_mode : other_mode;

    for (size_t i = 0; i < 10; i++)
        ds[(uint16_t)(0xD010u + i)] = state[i];
}

/* 0x0E5A0 - fill a 10-byte record with the "6 / 0x55 / 0x36" template.  The
 * `4, 1` pair goes to bytes 8 and 9 and is also what the routine returns. */
uint16_t record_template_06(uint8_t *ds, uint16_t dst)
{
    memzero(ds, dst, 10);
    ds[dst] = 6;
    ds[(uint16_t)(dst + 1)] = 85;
    ds[(uint16_t)(dst + 2)] = 54;
    /* `st er0, 8[er8]` is a 16-bit access, and the nX-U8 ignores A0 for those,
     * so an odd record address puts the pair at dst+7/dst+8. */
    uint16_t pair = (uint16_t)((dst + 8u) & ~1u);
    ds[pair] = 4;
    ds[(uint16_t)(pair + 1)] = 1;
    return 0x0104u;
}

/* 0x2F0B4 - the same template with 3 / 39 / 103 in the first three bytes. */
uint16_t record_template_03(uint8_t *ds, uint16_t dst)
{
    memzero(ds, dst, 10);
    ds[dst] = 3;
    ds[(uint16_t)(dst + 1)] = 39;
    ds[(uint16_t)(dst + 2)] = 103;
    uint16_t pair = (uint16_t)((dst + 8u) & ~1u);
    ds[pair] = 4;
    ds[(uint16_t)(pair + 1)] = 1;
    return 0x0104u;
}

/* 0x2502A - store one entry of the 0xDBE4 table: the offset (key minus the
 * 0xD144 base) at 0xDBE6 + index*8 and the 16-bit value at 0xDBE4 + index*8.
 * The subtraction is 8-bit, and r1 - the high half of the first argument - is
 * never touched, so it ends up in the result word. */
uint16_t table_slot_store(uint8_t *ds, uint16_t offset, uint16_t value,
                          uint8_t index)
{
    uint16_t entry = (uint16_t)(0xDBE4u + (uint16_t)index * 8u);

    ds[(uint16_t)(entry + 2)] = (uint8_t)((uint8_t)offset - ds[0xD144]);
    ds[entry] = (uint8_t)value;
    ds[(uint16_t)(entry + 1)] = (uint8_t)(value >> 8);
    return (uint16_t)((offset & 0xFF00u) |
                      (uint8_t)((uint8_t)offset - ds[0xD144]));
}

/* 0x0CD86 - fold `delta` into the 8-byte slots of the 0xDBE4 table.
 *
 * The slot count comes from 0xD169 (falling back to 0xD013 when that byte is
 * zero).  Starting at `start`, every slot whose second byte lies in
 * [low, high) - both taken from the slot at `start` - gets `delta` added to
 * its 16-bit word, and when `apply_global` is set the same delta is folded
 * into the global counters at 0xD00E (word) and 0xD010 (byte, low half of
 * `extra`).  The table lives in ROM on this firmware, so the slot stores are
 * issued but dropped, which is why the constants reload unchanged.
 */
uint16_t table_slot_fold(uint8_t *ds, uint16_t delta, uint16_t extra,
                         uint16_t apply_global, uint16_t start)
{
    uint8_t limit = ds[0xD169];
    uint8_t i = (uint8_t)start;

    if (ds[0xD169] == 0)
        limit = ds[0xD013];

    if (delta != 0) {
        uint16_t base = (uint16_t)(0x0DBE4u + (uint16_t)i * 8u);
        uint8_t low = ds[(uint16_t)(base + 2)];
        uint8_t high = ds[(uint16_t)(base + 3)];

        while (i < limit) {
            uint16_t entry = (uint16_t)(0x0DBE4u + (uint16_t)i * 8u);
            uint8_t key = ds[(uint16_t)(entry + 2)];

            if (key >= low && key < high) {
                uint16_t acc = (uint16_t)(ds[entry] |
                                          ((uint16_t)ds[(uint16_t)(entry + 1)] << 8));
                acc = (uint16_t)(acc + delta);
                if (entry >= 0xD000u) {
                    ds[entry] = (uint8_t)acc;
                    ds[(uint16_t)(entry + 1)] = (uint8_t)(acc >> 8);
                }
            }
            i++;
        }
    }

    if (apply_global != 0) {
        uint16_t counter = (uint16_t)(ds[0xD00E] |
                                      ((uint16_t)ds[0xD00F] << 8));
        counter = (uint16_t)(counter + delta);
        ds[0xD00E] = (uint8_t)counter;
        ds[0xD00F] = (uint8_t)(counter >> 8);
        ds[0xD010] = (uint8_t)(ds[0xD010] + (uint8_t)extra);
    }

    return 0;
}

/* 0x09D26 - clear the 20-byte block at 0xD16C.  The owner of the block is not
 * identified yet, so the name keeps the address rather than inventing a
 * meaning; the callers are the state-reset branch at 0x1FC72 and three more
 * in the 0x25Exx/0x26xxx region. */
uint16_t clear_block_d16c(uint8_t *ds)
{
    return memzero(ds, 0xD16Cu, 20);
}

/* 0x2ABDA - the larger of the two arguments in unsigned order: `bc le` tests
 * zero-or-borrow, so an argument below the other is the one replaced. */
uint16_t max_u16(uint8_t *ds, uint16_t a, uint16_t b)
{
    (void)ds;
    return (a <= b) ? b : a;
}

/* 0x143CA - clear 160 bytes at dst. */
uint16_t clear_160_bytes(uint8_t *ds, uint16_t dst)
{
    return memzero(ds, dst, 160);
}

/* 0x14C18 - clear 200 bytes at dst. */
uint16_t clear_200_bytes(uint8_t *ds, uint16_t dst)
{
    return memzero(ds, dst, 200);
}

/* the state bytes the indexed table scan reports through */
#define SCAN_COUNT_ADDR  0xD169  /* number of eight-byte entries */
#define SCAN_BASE_ADDR   0xD144  /* subtracted from the key before matching */
#define SCAN_TABLE       0xDBE6  /* ROM table, one key byte every eight */
#define SCAN_RESULT_ADDR 0xD014  /* slot the found index lands in */

/* 0x2E2BC - find a key in the eight-byte-stride table at 0xDBE6.  The key is
 * matched against `key - ds[0xD144]` (8-bit wrap) and the index is written to
 * 0xD014, whether or not the scan found anything.
 *
 * Two details of the original are kept because the differential test can see
 * them: `bc lt` is *unsigned* (`BC LT` tests C, the signed form is `BC LTS`),
 * and r1 is not merely a scratch register - it holds the base byte until the
 * `sllc r1, 3` that builds the byte offset replaces it with the top three bits
 * of the index.  The returned word is that r1 with the found index in the low
 * half, so a scan that never ran reports the base byte, not the key's. */
uint16_t table_index_scan(uint8_t *ds, uint16_t key)
{
    uint8_t count = ds[SCAN_COUNT_ADDR];
    uint8_t base = ds[SCAN_BASE_ADDR];
    uint8_t high = base;
    uint8_t index = 0;
    uint8_t target = (uint8_t)((uint8_t)key - base);

    for (uint8_t i = 0; i < count; i++) {
        high = (uint8_t)(i >> 5);
        if (ds[(uint16_t)(SCAN_TABLE + 8u * i)] == target) {
            index = i;
            break;
        }
    }
    ds_store(ds, SCAN_RESULT_ADDR, index);
    return (uint16_t)(((uint16_t)high << 8) | index);
}

/* 0x1D950 - initialise a ten-byte record with a kind byte.  The record is
 * `kind, 0, 0, 0, 0, 0, 0, 0, 0, kind != 0`, and a kind of 10 or more writes
 * ten zero bytes instead.
 *
 * `cmp r2, 10 ; bc ge` is an unsigned test (`BC GE` checks C, `BC GES` is the
 * signed one), so a kind byte of 0x80..0xFF still counts as 10 or more and
 * takes the zero-filled path.  The `mov r0, ...` before the return
 * only touches the low half, so the result keeps the low byte the path
 * computed and the destination pointer's high byte: dst & 0xFF00 for a normal
 * record, (dst & 0xFF00) | 0xF0 for the zero-filled one. */
uint16_t record_init_kind(uint8_t *ds, uint16_t dst, uint8_t kind)
{
    /* every store here is at least two bytes wide, and the core forces word
     * alignment by ignoring address bit 0 - an odd pointer therefore lands one
     * byte lower */
    dst = (uint16_t)(dst & 0xFFFEu);
    if (kind >= 10) {
        for (int i = 0; i < 10; i++)
            ds_store(ds, (uint16_t)(dst + i), 0);
        return (uint16_t)((dst & 0xFF00u) | 0xF0u);
    }

    ds_store16(ds, dst, kind);
    for (int i = 2; i < 8; i++)
        ds_store(ds, (uint16_t)(dst + i), 0);
    ds_store(ds, (uint16_t)(dst + 8), 0);
    ds_store(ds, (uint16_t)(dst + 9), kind != 0);
    return (uint16_t)(dst & 0xFF00u);
}

/* 0x1DCCE - copy a twenty-byte record in two ten-byte halves.  The ROM loads
 * er6 (two bytes) plus qr8 (the eight-byte octet r8..r15) and stores the same
 * pair twice, so each half is exactly ten bytes.
 *
 * Order matters for an overlapping copy: the upper half at +10..+19 is read
 * and written first, then the lower half at +0..+9.  The two halves meet in
 * the middle, so a `dst` a few bytes below `src` really does behave differently
 * from one a few bytes above.  The return value is the *unmasked* argument. */
uint16_t record_pair_copy(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t result = dst;
    uint16_t d = (uint16_t)(dst & 0xFFFEu);
    uint16_t s = (uint16_t)(src & 0xFFFEu);
    uint8_t half[10];

    /* The four phases must stay separate: the ROM reads ten bytes into
     * er6+qr8, stores them, and only then reads the next ten, so when `d` and
     * `s` overlap the lower half sees the bytes the upper half just wrote.  A
     * loop that reads and writes one byte at a time - the obvious translation,
     * and the one that was here first - is wrong for every overlapping pair,
     * which the pointer sweep hits at once.
     *
     * The address masks are the core's alignment rule: `l`/`st` of more than
     * one byte ignores bit 0, and `[ea+]` rounds the bumped address down to
     * even again. */
    for (int i = 0; i < 10; i++)
        half[i] = ds[(uint16_t)(s + 10 + i)];
    ds_put(ds, (uint16_t)(d + 11), half[1]);
    ds_put(ds, (uint16_t)(d + 10), half[0]);
    for (int i = 9; i >= 2; i--)
        ds_put(ds, (uint16_t)(d + 10 + i), half[i]);

    for (int i = 0; i < 10; i++)
        half[i] = ds[(uint16_t)(s + i)];
    ds_put(ds, (uint16_t)(d + 1), half[1]);
    ds_put(ds, (uint16_t)(d + 0), half[0]);
    for (int i = 9; i >= 2; i--)
        ds_put(ds, (uint16_t)(d + i), half[i]);

    return result;
}

/* 0x0E4AE - re-home the parked word at 0xD10E once the mode byte at 0xD116 is
 * 1.  Only four values are tolerated as "already resident": 0xFC37..0xFC3B,
 * 0xFC31 and the sentinel 0xFC36, which is rewritten as 0x00DC.  Everything
 * else is answered with 0xFC37 and leaves memory untouched.
 *
 * The early exit returns er0 with the mode byte in the low half and the
 * caller's r1 in the high half, which is what the C signature reproduces. */
uint16_t fixup_0D10E(uint8_t *ds, uint16_t arg)
{
    uint8_t mode = ds[0xD116];

    if (mode != 1)
        return (uint16_t)((arg & 0xFF00u) | mode);

    uint16_t v = ds_load16(ds, KEEP_ADDR);

    if (v == 0xFC36) {
        store_0D10E(ds, 0x00DC);
        return 0x00DC;
    }
    if ((int16_t)v >= (int16_t)0xFC37 && (int16_t)v <= (int16_t)0xFC3B)
        goto resident;
    if ((int16_t)v != (int16_t)0xFC31)
        return 0xFC37;
resident:
    store_0D10E(ds, 0);
    return 0;
}

/* 0x15BF8 - clear the fixed 320-byte block at 0xD6B2. */
uint16_t clear_block_d6b2(uint8_t *ds)
{
    return memzero(ds, 0xD6B2u, 320);
}
