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

/* ------------------------------------------------------------------ */
/* Ten-byte record moves between the state blocks at 0xD000..0xD05F.
 *
 * 0x19E54 onwards is a run of sixteen routines with one shape: `l qr0, [ea+]`
 * grabs eight bytes, `l er8, [ea+]` the last two, and the same pair is stored
 * back through a second `lea`.  That is a ten-byte record move - the same
 * record width 0x1D950 (record_init_kind) and 0x1DCCE (record_pair_copy) use.
 * Only the two fixed addresses differ, so one helper serves all of them.
 *
 * Both accesses are multi-byte, so the core forces the effective address even
 * (address bit 0 is ignored).  The loads all happen before the first store, so
 * buffering the record is exact and the source has to be read before the copy
 * even when the blocks overlapped - none of the fixed pairs do, but 0x17CA6
 * takes its addresses from the caller and does not get that guarantee.
 *
 * The return value is what the loads left in er0: the first word of the
 * *source* record, not the destination. */
static uint16_t record10_move(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint8_t record[10];
    uint16_t first;

    dst = (uint16_t)(dst & 0xFFFEu);
    src = (uint16_t)(src & 0xFFFEu);
    for (int i = 0; i < 10; i++)
        record[i] = ds[(uint16_t)(src + i)];
    first = (uint16_t)(record[0] | ((uint16_t)record[1] << 8));
    for (int i = 0; i < 10; i++)
        ds_store(ds, (uint16_t)(dst + i), record[i]);
    return first;
}

/* 0x1CAFC - clear the accumulator record and mark it as "zero result".  Four
 * word stores zero 0xD000..0xD007, a byte store clears 0xD008 and then 0xF3
 * goes to 0xD009, so the ten-byte record at 0xD000 comes back empty with the
 * flag digit set.  er0 still holds the 0 the stores wrote, so it answers 0. */
uint16_t record_reset_flag38(uint8_t *ds)
{
    for (int i = 0; i < 9; i++)
        ds_store(ds, (uint16_t)(0xD000u + i), 0);
    ds_store(ds, 0xD009u, 0xF3);
    return 0;
}

/* The two record handovers below are not straight copies: they convert between
 * the numeric record at 0xD000 and the display buffer, and the conversion is a
 * byte-order change.  Reading the record as five big-endian words, words 1..4
 * are reversed *and* byte-swapped while word 0 is left alone, so the ten bytes
 * come out in the order below.  `record_swap_out` is what 0x1C674 writes (its
 * inverse, `record_swap_in`, is what 0x1C63E reads) - the two are each other's
 * inverse, which the differential tests confirm byte for byte. */
static const uint8_t record_swap_out[10] = {9, 8, 7, 6, 5, 4, 3, 2, 0, 1};
static const uint8_t record_swap_in[10] = {8, 9, 7, 6, 5, 4, 3, 2, 1, 0};

/* Load all ten source bytes, then store them through `map`.  The load is done
 * first so an overlapping source is still read before anything is written, and
 * the source address is rounded down to even because both loads are
 * multi-byte.  Which end the *word* store sits at decides where the alignment
 * rule bites, so it is a parameter: 0x1C63E starts with the pair, 0x1C674 ends
 * with it and the pair's address is forced even there, which on an odd
 * destination overwrites the eighth byte again. */
static uint16_t record10_permute(uint8_t *ds, uint16_t dst, uint16_t src,
                                 const uint8_t *map, int word_first)
{
    uint8_t record[10];

    src = (uint16_t)(src & 0xFFFEu);
    for (int i = 0; i < 10; i++)
        record[i] = ds[(uint16_t)(src + i)];

    if (word_first) {
        ds_store(ds, dst, record[map[0]]);
        ds_store(ds, (uint16_t)(dst + 1), record[map[1]]);
        for (int i = 2; i < 10; i++)
            ds_store(ds, (uint16_t)(dst + i), record[map[i]]);
    } else {
        uint16_t tail = (uint16_t)((uint16_t)(dst + 8) & 0xFFFEu);

        for (int i = 0; i < 8; i++)
            ds_store(ds, (uint16_t)(dst + i), record[map[i]]);
        ds_store(ds, tail, record[map[8]]);
        ds_store(ds, (uint16_t)(tail + 1), record[map[9]]);
    }
    return (uint16_t)(record[0] | ((uint16_t)record[1] << 8));
}

/* 0x1C63E - load both ten-byte records at once: the bytes at `src_b` become
 * 0xD010..0xD019 and the bytes at `src_a` become 0xD000..0xD009, both through
 * the byte-order map.  The upper record is filled first and each half is read
 * completely before it is written, so an overlapping source sees the first
 * half's writes.  er0 is never written, so the caller's argument comes back. */
uint16_t records_from_pointers(uint8_t *ds, uint16_t src_a, uint16_t src_b)
{
    (void)record10_permute(ds, 0xD010u, src_b, record_swap_in, 1);
    (void)record10_permute(ds, 0xD000u, src_a, record_swap_in, 1);
    return src_a;
}

/* 0x1C674 - hand the record at 0xD000 to the buffer whose address sits at
 * 0xD05C, through the byte-order map.  The pair of stores at the end are the
 * only word-wide ones, so an *odd* 0xD05C leaves the eighth byte written twice
 * and never reaches 0xD05C+9 (see record10_permute).  The answer is the first
 * word of the source, which the loads left in er0. */
uint16_t record_to_pointer(uint8_t *ds)
{
    uint16_t dst = (uint16_t)(ds[0xD05Cu] | ((uint16_t)ds[0xD05Du] << 8));

    return record10_permute(ds, dst, 0xD000u, record_swap_out, 0);
}

/* 0x14BDC - the stride-5 index of 0x14BCC with the caller's registers saved
 * around it.  It copies er0/er2 into er4/er12 for no reason the result can see
 * (the callee clobbers r4..r7 and r8..r15, which is what the pushes protect),
 * then calls 0x14BCC with the same er0/er2 it came in with, so the whole body
 * is that call. */
uint16_t record10_index_5x_saved(uint8_t *ds, uint16_t base, uint16_t bias)
{
    return record10_index_5x(ds, base, bias, NULL);
}

/* 0x14BF0 - `_memcpy_nn` with a count of 200 pushed on the stack: `mov r4, 200
 * / mov r5, 0 / push er4` is the fourth argument.  The two `pop er4` restore
 * the caller's pair one at a time. */
uint16_t memcpy_200(uint8_t *ds, uint16_t dst, uint16_t src)
{
    return memcpy_nn(ds, dst, src, 200);
}

/* 0x14C04 - the same with a count of 50. */
uint16_t memcpy_50(uint8_t *ds, uint16_t dst, uint16_t src)
{
    return memcpy_nn(ds, dst, src, 50);
}

/* 0x1E5B4 - the mode test of the table/converter screen: mode 75 (0x4B) with
 * sub-state 1.  Both stores are byte loads, so the high half of the answer is
 * the caller's r1 and has to be carried through. */
uint16_t mode_is_75_1(uint8_t *ds, uint16_t arg)
{
    uint8_t flag = (uint8_t)(ds[0xD111] == 75 && ds[0xD114] == 1);

    return (uint16_t)((arg & 0xFF00u) | flag);
}

/* 0x24B28 - the same test for mode 69 (0x45). */
uint16_t mode_is_69_1(uint8_t *ds, uint16_t arg)
{
    uint8_t flag = (uint8_t)(ds[0xD111] == 69 && ds[0xD114] == 1);

    return (uint16_t)((arg & 0xFF00u) | flag);
}

/* 0x153C4 - the same ten-byte copy, with the destination computed as er10 plus
 * forty and the source left in er2.  It branches straight into the shared copy
 * tail at 0x13ADA, so er0 comes back holding the destination. */
uint16_t record10_from_er10_plus40(uint8_t *ds, uint16_t src, uint16_t base10)
{
    uint16_t dst = (uint16_t)(base10 + 40);

    (void)record10_move(ds, dst, src);
    return dst;
}

/* 0x13ACC - copy ten bytes from er4 to er8, both rounded down to even.  The
 * pair of `mov`s feeds the shared copy tail at 0x1DCE4, and er0 is never
 * written, so the destination address is what comes back. */
uint16_t record10_er8_from_er4(uint8_t *ds, uint16_t src, uint16_t dst)
{
    (void)record10_move(ds, dst, src);
    return dst;
}

/* 0x17FDE - zero a thirty-byte structure at er14 and set two flags in it.
 * 0x180D0 is called first, which leaves er0/er2/er4/er6 zero and r8/r9 at 0/1.
 * Then five stores write r0..r7 twice, r0..r3 once, r0..r7 again (now with r0
 * set to 1) and finally the r8/r9 pair, so byte 20 becomes 1 and byte 29
 * becomes 1 while the other twenty-eight stay zero.  The first store is word
 * wide, so an odd er14 rounds down and the block starts one byte earlier. */
uint16_t init_struct_30(uint8_t *ds, uint16_t frame)
{
    uint16_t base = (uint16_t)(frame & 0xFFFEu);

    for (int i = 0; i < 30; i++)
        ds_store(ds, (uint16_t)(base + i), 0);
    ds_store(ds, (uint16_t)(base + 20), 1);
    ds_store(ds, (uint16_t)(base + 29), 1);
    return 1;
}

/* 0x1B682 - a flag taken from the record's kind byte: the top nibble of
 * 0xD009, after masking, must be neither 0 nor 0x20.  `l r0` and `and r0,240`
 * are byte operations, so the high half of the answer is the caller's r1. */
uint16_t record_kind_flag(uint8_t *ds, uint16_t arg)
{
    uint8_t kind = (uint8_t)(ds[0xD009] & 0xF0u);

    return (uint16_t)((arg & 0xFF00u) | (kind != 0 && kind != 0x20 ? 1u : 0u));
}

/* 0x29490 - the digit at the pointer if it is below 15, otherwise the raw byte
 * just after it.  The compare is against the *decoded* digit that 0x1DAE2
 * returns, and the second path only reloads the low half, so the high half of
 * the answer is the pointer's high byte either way. */
uint16_t digit_or_next_byte(uint8_t *ds, uint16_t ptr)
{
    uint16_t digit = digit_at(ds, ptr);

    if ((uint8_t)digit < 15)
        return digit;
    return (uint16_t)((digit & 0xFF00u) | ds[(uint16_t)(ptr + 1)]);
}

/* 0x09D16 - copy a ten-byte record only when both addresses are non-zero, and
 * answer with the destination either way.
 *
 * The tail is `bl f_1DCCE.l_016`, the label inside record_pair_copy where the
 * second `push er6 / push qr8` sits: entering there skips the `lea 10[...]` of
 * the upper half, so only the ten bytes at the source are moved.  Zero is the
 * "no record" pointer, and the two guards are `add erN, 0` / `bc eq` - the
 * value itself is untouched, so the early exit returns the caller's er0. */
uint16_t record10_copy_if_present(uint8_t *ds, uint16_t dst, uint16_t src)
{
    if (dst == 0 || src == 0)
        return dst;

    (void)record10_move(ds, dst, src);
    return dst;
}

/* 0x19E54 - copy the 0xD046 record over 0xD010. */
uint16_t record10_d046_to_d010(uint8_t *ds)
{
    return record10_move(ds, 0xD010u, 0xD046u);
}

/* 0x19E66 - the reverse direction. */
uint16_t record10_d010_to_d046(uint8_t *ds)
{
    return record10_move(ds, 0xD046u, 0xD010u);
}

/* 0x19E78 */
uint16_t record10_d000_to_d046(uint8_t *ds)
{
    return record10_move(ds, 0xD046u, 0xD000u);
}

/* 0x19E8A */
uint16_t record10_d03c_to_d000(uint8_t *ds)
{
    return record10_move(ds, 0xD000u, 0xD03Cu);
}

/* 0x19E9C */
uint16_t record10_d03c_to_d010(uint8_t *ds)
{
    return record10_move(ds, 0xD010u, 0xD03Cu);
}

/* 0x19EAE */
uint16_t record10_d000_to_d03c(uint8_t *ds)
{
    return record10_move(ds, 0xD03Cu, 0xD000u);
}

/* 0x19EC0 */
uint16_t record10_d050_to_d010(uint8_t *ds)
{
    return record10_move(ds, 0xD010u, 0xD050u);
}

/* 0x19ED2 */
uint16_t record10_d000_to_d050(uint8_t *ds)
{
    return record10_move(ds, 0xD050u, 0xD000u);
}

/* 0x19EE4 */
uint16_t record10_d030_to_d010(uint8_t *ds)
{
    return record10_move(ds, 0xD010u, 0xD030u);
}

/* 0x19EF6 */
uint16_t record10_d000_to_d030(uint8_t *ds)
{
    return record10_move(ds, 0xD030u, 0xD000u);
}

/* 0x19F08 - the same fixed pair as 0x19F1A in the opposite direction, and the
 * same pair as 0x19E54 the other way round; the ROM keeps a separate entry for
 * every direction it needs instead of testing one at run time. */
uint16_t record10_d000_to_d010(uint8_t *ds)
{
    return record10_move(ds, 0xD010u, 0xD000u);
}

/* 0x19F1A */
uint16_t record10_d010_to_d000(uint8_t *ds)
{
    return record10_move(ds, 0xD000u, 0xD010u);
}

/* 0x19F2C */
uint16_t record10_d010_to_d050(uint8_t *ds)
{
    return record10_move(ds, 0xD050u, 0xD010u);
}

/* 0x19F3E */
uint16_t record10_d050_to_d000(uint8_t *ds)
{
    return record10_move(ds, 0xD000u, 0xD050u);
}

/* 0x19F50 */
uint16_t record10_d030_to_d000(uint8_t *ds)
{
    return record10_move(ds, 0xD000u, 0xD030u);
}

/* 0x19F62 */
uint16_t record10_d010_to_d030(uint8_t *ds)
{
    return record10_move(ds, 0xD030u, 0xD010u);
}

/* 0x17CA6 - the same ten-byte move with both addresses taken from registers:
 * the source is er14 and the destination er12, which is how the ROM passes a
 * frame-resident record around without a stack argument.  er0 is not an input
 * - the first load overwrites it - so the caller only sees the source word
 * come back. */
uint16_t record10_from_frame(uint8_t *ds, uint16_t dst, uint16_t src)
{
    return record10_move(ds, dst, src);
}

/* The packed-digit step of the same family: 0x1B286 adds five to the second
 * byte of the 0xD000 record in BCD.  A byte below five is left alone (`bc lt`
 * is unsigned, so 0x80..0xFF takes the add path).
 *
 * `add r1, 5` sets carry and half-carry, and the `daa` that follows reads both:
 * 5 + 5 carries out of the low nibble, so 0x05 comes back as digit 0 and not
 * 0x0A.  The half-carry is the one taken out of the *low nibble*, not out of
 * the full sum - reading bit 4 of the sum sets it for 0x11 as well and turns
 * the answer into 0.  `and r1, 15` then keeps the digit alone, and that mask is
 * why the whole 0xFB..0xFF range (where the add carries out) collapses onto the
 * same answer either way. */
static uint8_t bcd_adjust(uint8_t value, int half_carry, int carry)
{
    uint8_t adjust = 0;

    if ((value & 0x0Fu) > 0x09u || half_carry)
        adjust |= 0x06u;
    if ((value & 0xF0u) > 0x90u || carry)
        adjust |= 0x60u;
    if ((value & 0xF0u) == 0x90u && (value & 0x0Fu) > 0x09u && !half_carry)
        adjust |= 0x60u;
    return (uint8_t)(value + adjust);
}

/* 0x1B286 - the record byte add itself. */
uint16_t record_high_byte_add5(uint8_t *ds)
{
    uint16_t word = ds_load16(ds, 0xD000u);
    uint8_t high = (uint8_t)(word >> 8);
    uint16_t sum;
    int half_carry;

    if (high < 5u)
        return word;

    sum = (uint16_t)((uint16_t)high + 5u);
    half_carry = (int)((((unsigned)(high & 0x0Fu) + 5u) >> 4) & 1u);
    high = bcd_adjust((uint8_t)sum, half_carry, sum > 0xFFu);
    word = (uint16_t)((word & 0x00FFu) | ((uint16_t)(high & 0x0Fu) << 8));
    ds_store16(ds, 0xD000u, word);
    return word;
}

/* 0x1C6AA - the packed digit byte at 0xD009: 0xF0 and up carry a digit in the
 * low nibble, anything below carries nothing.  `l r0, [d_0D009]` replaces only
 * the low half, so the caller's r1 survives into the result. */
uint16_t digit_0D009(uint8_t *ds, uint16_t arg)
{
    uint8_t value = ds[0xD009];
    uint8_t digit = (value >= 0xF0u) ? (uint8_t)(value & 0x0Fu) : 0;

    return (uint16_t)((arg & 0xFF00u) | digit);
}

/* 0x1DAE2 - the same decode with the byte fetched through the argument, which
 * keeps the byte as a plain number instead of a state byte. */
uint16_t digit_at(uint8_t *ds, uint16_t ptr)
{
    uint8_t value = ds[ptr];
    uint8_t digit = (value >= 0xF0u) ? (uint8_t)(value & 0x0Fu) : 0;

    return (uint16_t)((ptr & 0xFF00u) | digit);
}

/* 0x13B86 and 0x14BCC are the same routine with a different stride, and each
 * needs its own address annotation to count as translated.  Three instruction
 * traps stack up here.  `mul er0, r12` multiplies the *low bytes* only
 * (emu/nx8.c op_mul masks er0 to 8 bits), the `add r0, r13` that follows is an
 * 8-bit add whose carry is lost into nothing, and the second `mul er0, r2`
 * again takes the low byte alone.  So a stride that overflows the byte wraps
 * before the times-ten, exactly as it does in the ROM.  The byte offset comes
 * back in er2 as well as sitting below the base in er0, which is why the C
 * signature takes an out-parameter. */
static uint16_t record10_index(uint16_t base, uint16_t bias, uint8_t stride,
                              uint16_t *offset)
{
    uint16_t product = (uint16_t)((uint16_t)stride * (uint8_t)bias);
    uint8_t low = (uint8_t)((uint8_t)product + (uint8_t)(bias >> 8));
    uint16_t scaled = (uint16_t)((uint16_t)low * 10u);

    if (offset)
        *offset = scaled;
    return (uint16_t)(scaled + base);
}

/* 0x13B86 - stride 4. */
uint16_t record10_index_4x(uint8_t *ds, uint16_t base, uint16_t bias,
                           uint16_t *offset)
{
    (void)ds;
    return record10_index(base, bias, 4u, offset);
}

/* 0x14BCC - the same with stride 5. */
uint16_t record10_index_5x(uint8_t *ds, uint16_t base, uint16_t bias,
                           uint16_t *offset)
{
    (void)ds;
    return record10_index(base, bias, 5u, offset);
}

/* 0x14FC6 - er10 = er6 + 10 * r12.  No base address is involved and nothing is
 * loaded or stored: the whole routine is an 8-bit index times ten, added to a
 * pointer, so it exists to walk a table of ten-byte records.  `mul er0, r2`
 * masks er0 to its low byte first (op_mul) and the `add er0, er6` that follows
 * is a full sixteen-bit add. */
uint16_t base_plus_10x_index(uint8_t *ds, uint8_t index, uint16_t er6,
                             uint16_t *er10_out)
{
    uint16_t moved = (uint16_t)((uint16_t)(index * 10u) + er6);

    (void)ds;
    if (er10_out)
        *er10_out = moved;
    return moved;
}

/* 0x13B96 - load the word at `base + 2 * index`, and leave 9 in r2.  The address
 * is forced even, because a multi-byte `l` ignores bit 0.  er2 is deliberately
 * *not* the base in er0 here: the ROM adds into er2 in place, so the returned
 * er0 is the loaded word while the caller's er2 has been disturbed - a detail
 * that only matters to the caller and is not part of this signature. */
uint16_t word_at_2x_index(uint8_t *ds, uint16_t base, uint8_t index,
                          uint8_t *r2_out)
{
    uint16_t addr = (uint16_t)((base + (uint16_t)(2u * index)) & 0xFFFEu);

    if (r2_out)
        *r2_out = 9;
    return (uint16_t)(ds[addr] | ((uint16_t)ds[(uint16_t)(addr + 1)] << 8));
}

/* 0x17E52 - unpack one nibble pair out of a record at er12.
 *
 * `l r11,[er12]` loads the byte at +0 into r11 and `l r10,1[er12]` the byte at
 * +1 into r10, so as a word er10 holds +0 in its *high* half.  The shift that
 * follows, `sllc r11, 4`, is not an eight-bit shift: the manual's `SLLC Rn,obj`
 * shifts "the 16 bits in the specified register and the register below it" and
 * stores the upper eight bits back in Rn.  So the pair is shifted left four
 * places as a word and only the high byte survives, which makes r11 the low
 * nibble of the first byte spliced into the low nibble of the second.  The
 * answer in er0 is the byte at +8, read *before* the branch that guards the
 * shift, and the carry that `sllc` pulls in lands in the low half that is
 * discarded, so a set carry cannot change the result. */
uint16_t nibble_pair_shifted(uint8_t *ds, uint16_t base, uint8_t *r10_out,
                             uint8_t *r11_out)
{
    uint8_t high = ds[base];
    uint8_t low = ds[(uint16_t)(base + 1)];
    uint8_t flag = ds[(uint16_t)(base + 8)];

    if (flag != 0) {
        uint16_t word = (uint16_t)(((uint16_t)high << 8) | low);

        high = (uint8_t)(((uint16_t)(word << 4)) >> 8);
    }
    if (r11_out)
        *r11_out = high;
    if (r10_out)
        *r10_out = low;
    return flag;
}

/* 0x16070 - two's complement of the 32-bit value held in er2:er0.
 *
 * The complement is four `xor rN, 255`, the increment is `add er0, 1` followed
 * by two 8-bit `addc` on r2 and r3 - a real 32-bit carry chain, so 0x0000FFFF
 * comes back as 0xFFFF0001. */
uint16_t complement32(uint8_t *ds, uint16_t low, uint16_t high,
                      uint16_t *out_high)
{
    uint32_t value = (uint32_t)(~(((uint32_t)high << 16) | low)) + 1u;

    (void)ds;
    if (out_high)
        *out_high = (uint16_t)(value >> 16);
    return (uint16_t)value;
}

/* 0x09E84 - how much of the 0xE3E4-byte budget the stack has eaten: the
 * routine is `mov er0, sp ; sub r0, 228 ; subc r1, 227`, i.e. sp - 0xE3E4.
 * It reads the stack pointer itself, so the C body takes it as an argument
 * rather than reaching for a global the flat data space cannot express. */
uint16_t stack_bytes_used(uint8_t *ds, uint16_t sp)
{
    (void)ds;
    return (uint16_t)(sp - 0xE3E4u);
}

/* 0x23F8A - spill the whole register file into the caller's frame: er12 is
 * pointed at er14 - 16, and `st qr0` plus `st qr8` write sixteen bytes there.
 * er12 is saved and restored around the address arithmetic, so er0 and every
 * other register come back unchanged - the *return value* is the word the
 * caller already had in er0, and the effect is entirely in memory. */
uint16_t store_reg_to_stack(uint8_t *ds, uint16_t frame, const uint8_t *regs)
{
    uint16_t base = (uint16_t)((uint16_t)(frame - 16u) & 0xFFFEu);

    for (int i = 0; i < 16; i++)
        ds_store(ds, (uint16_t)(base + i), regs[i]);
    return (uint16_t)(regs[0] | ((uint16_t)regs[1] << 8));
}

/* 0x143D6 - the high nibble of r4 parked in r5, with the constant 3 left in r2.
 * Three instructions, no memory and no er0: the whole result is in registers
 * the calling convention does not carry, which is why the C body answers them
 * through out-parameters and returns er0 untouched. */
uint16_t high_nibble_into_r5(uint8_t *ds, uint16_t arg, uint8_t r4,
                             uint8_t *r5, uint8_t *r2)
{
    (void)ds;
    if (r5)
        *r5 = (uint8_t)(r4 >> 4);
    if (r2)
        *r2 = 3;
    return arg;
}
