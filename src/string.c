/*
 * string.c - string routines from the fx-580VN X firmware.
 *
 * Each function is annotated with its ROM entry point and follows the
 * instruction stream one-for-one, so the C can be checked against the listing
 * line by line.  Address arithmetic is 16-bit, like the machine's.
 */
#include "runtime.h"

/* 0x29A2C */
uint16_t strlen_n(uint8_t *ds, uint16_t s)
{
    uint16_t p = s;
    while (ds[p])
        p++;
    return (uint16_t)(p - s);
}

/* 0x2AB26 - strlen returning a byte (the ROM uses it for short strings) */
uint8_t byte_strlen_n(uint8_t *ds, uint16_t s)
{
    uint8_t n = 0;
    while (ds[s]) {
        s++;
        n++;
    }
    return n;
}

/* 0x0E5C8 - copy including the terminator, returns dst */
uint16_t strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t ret = dst;
    uint8_t c;
    do {
        c = ds[src++];
        ds[dst++] = c;
    } while (c);
    return ret;
}

/* 0x2EDA0 - append src (including its terminator) to dst, returns dst */
uint16_t strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t p = dst;
    while (ds[p])
        p++;
    uint8_t c;
    do {
        c = ds[src++];
        ds[p++] = c;
    } while (c);
    return dst;
}

/* 0x203B8 - NULL-safe strlen: len(NULL) == 0 */
uint16_t smart_strlen_n(uint8_t *ds, uint16_t s)
{
    if (s == 0)
        return 0;
    return strlen_n(ds, s);
}

/* 0x203C2 - copy to NULL is a no-op; copy from NULL writes "" */
uint16_t smart_strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    if (dst == 0)
        return dst;
    if (src == 0) {
        ds[dst] = 0;
        return dst;
    }
    return strcpy_nn(ds, dst, src);
}

/* 0x203D6 - same NULL rules as smart_strcpy_nn */
uint16_t smart_strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    if (dst == 0)
        return dst;
    if (src == 0) {
        ds[dst] = 0;
        return dst;
    }
    return strcat_nn(ds, dst, src);
}

/*
 * 0x24BD6 - pointer to string constant #index.
 *
 * 0x0D12B selects the string bank (language): the table at 0x07AC2 holds one
 * base per bank, and each base is the address of a table of string pointers.
 * The pointer tables are 1-based, so entry n is at base + 2*n - 2.
 */
uint16_t get_string_constant(uint8_t *ds, uint16_t index)
{
    uint16_t base = (uint16_t)(0x07AC2u + 2 * ds[0x0D12B]);
    uint16_t table = (uint16_t)(ds[base] | ((uint16_t)ds[(uint16_t)(base + 1)] << 8));
    uint16_t entry = (uint16_t)(table + (uint16_t)(2 * index) - 2);

    return (uint16_t)(ds[entry] | ((uint16_t)ds[(uint16_t)(entry + 1)] << 8));
}

/*
 * 0x2AC9E - expand string constant #index into dst and return its length.
 * The constant itself is stored in the same nested format, so one call only
 * unwraps a single level; str_decompress_print drives it.
 */
uint16_t copy_string_constant(uint8_t *ds, uint16_t index, uint16_t dst)
{
    smart_strcpy_nn(ds, dst, get_string_constant(ds, index));
    return smart_strlen_n(ds, dst);
}

/* 0x24010 - convert a byte to two uppercase hexadecimal characters.
 * The high nibble is the low byte of the return value. */
uint16_t hex_byte(uint8_t value)
{
    uint8_t high = (uint8_t)(value >> 4);
    uint8_t low = (uint8_t)(value & 0x0F);

    high = (uint8_t)(high < 10 ? high + '0' : high + 'A' - 10);
    low = (uint8_t)(low < 10 ? low + '0' : low + 'A' - 10);
    return (uint16_t)(high | ((uint16_t)low << 8));
}

/* 0x20462 - format the signed word at source+16 as the firmware's digit token. */
uint16_t format_number_token(uint8_t *ds, uint16_t source, uint16_t output)
{
    uint16_t value = (uint16_t)(ds[(uint16_t)(source + 16)] |
                                ((uint16_t)ds[(uint16_t)(source + 17)] << 8));
    uint16_t cursor = output;
    uint8_t alternate = ds[0xD14E] != 0;
    uint8_t extended = ds[0xD13F] == 1;
    uint8_t negative = (value & 0x8000) != 0;

    if (alternate) {
        ds[cursor++] = 45;
        if (negative)
            ds[cursor++] = 192;
    } else {
        if (extended) {
            ds[cursor++] = 251;
            ds[cursor++] = 58;
        } else {
            ds[cursor++] = 144;
        }
        if (extended)
            ds[cursor++] = 251;
        ds[cursor++] = (uint8_t)(extended ? (negative ? 60 : 59)
                                           : (negative ? 146 : 145));
    }

    if (negative)
        value = (uint16_t)(~value + 1);
    uint8_t tens = (uint8_t)(value / 10);
    uint8_t ones = (uint8_t)(value % 10);

    if (tens != 0) {
        if (alternate) {
            ds[cursor++] = (uint8_t)(tens + '0');
        } else if (extended) {
            ds[cursor++] = 251;
            ds[cursor++] = (uint8_t)(tens + (ds[0xD137] == 14 ? 68 : 48));
        } else {
            if (ds[0xD137] == 14)
                ds[cursor++] = 4;
            ds[cursor++] = (uint8_t)(tens + 160);
        }
    }

    if (extended && !alternate)
        ds[cursor++] = 251;
    ds[cursor++] = (uint8_t)(ones + ((alternate || extended) ? 48 : 160));
    ds[cursor] = 0;
    return output;
}

/* 0x203E6 - expand a packed decimal record into its 15 digit nibbles. */
uint8_t unpack_decimal_record(uint8_t *ds, uint16_t source, uint16_t output)
{
    uint8_t tag = ds[source];

    if (tag & 0xF0)
        return 0;

    for (uint16_t i = 0; i < 24; i++)
        ds[(uint16_t)(output + i)] = 0;
    if (tag == 0)
        return 1;

    uint8_t exponent = ds[(uint16_t)(source + 9)];
    uint8_t packed_exp = ds[(uint16_t)(source + 8)];
    uint16_t adjusted_exp = (uint16_t)(((packed_exp >> 4) * 10) + (packed_exp & 0x0F));
    if (exponent == 0 || exponent == 5)
        adjusted_exp = (uint16_t)(adjusted_exp + 0xFF9C);
    ds[(uint16_t)(output + 16)] = (uint8_t)adjusted_exp;
    ds[(uint16_t)(output + 17)] = (uint8_t)(adjusted_exp >> 8);
    ds[(uint16_t)(output + 15)] = exponent >= 5 ? 1 : 0;

    for (uint16_t i = 0; i < 15; i++) {
        uint8_t packed = ds[(uint16_t)(source + (i + 1) / 2)];
        ds[(uint16_t)(output + i)] = (i & 1) ? (packed >> 4) : (packed & 0x0F);
    }
    return 1;
}

/* 0x208A6 - replace a trailing token, preserving the ROM's sentinel forms. */
uint16_t replace_trailing_token(uint8_t *ds, uint16_t string, uint8_t value)
{
    uint16_t end = string;

    if (ds[string] == 49 && ds[(uint16_t)(string + 1)] == 0) {
        ds[string] = value;
        return string;
    }
    if ((ds[string] == 96 || ds[string] == 192) &&
        ds[(uint16_t)(string + 1)] == 49 && ds[(uint16_t)(string + 2)] == 0) {
        ds[(uint16_t)(string + 1)] = value;
        return string;
    }

    while (ds[end] != 0)
        end++;
    ds[end] = value;
    ds[(uint16_t)(end + 1)] = 0;
    return (uint16_t)(end + 1);
}

static uint16_t read_word(const uint8_t *ds, uint16_t address)
{
    return (uint16_t)(ds[address] | ((uint16_t)ds[(uint16_t)(address + 1)] << 8));
}

uint8_t expand_string_token(uint8_t *ds, uint16_t code, uint16_t output)
{
    uint8_t index = (uint8_t)code;
    uint8_t marker = (uint8_t)(code >> 8);
    uint16_t pointer_table, length_table;
    uint16_t output_length = 0;
    uint8_t result = index;

    if (index == 0) {
        ds[output] = 0;
        return result;
    }

    switch (marker) {
    case 0:   pointer_table = 0x2432; length_table = 0x2612; break;
    case 250: pointer_table = 0x2360; length_table = 0x23E2; break;
    case 251: pointer_table = 0x1E82; length_table = 0x1F22; break;
    case 253: pointer_table = 0x1F72; length_table = 0x2032; break;
    case 254: pointer_table = 0x2092; length_table = 0x2270; break;
    default:
        ds[output] = 0;
        return result;
    }

    uint16_t source = read_word(ds, (uint16_t)(pointer_table + 2u * index));
    uint8_t descriptor = ds[(uint16_t)(length_table + index)];
    uint8_t remaining = descriptor & 0x0F;
    uint8_t offset = descriptor >> 4;
    result = remaining;
    if (remaining == 0) {
        ds[output] = 0;
        return result;
    }
    if (offset != 15)
        source = (uint16_t)(source + offset);

    while (output_length < 33) {
        uint8_t value = ds[source++];
        ds[(uint16_t)(output + output_length++)] = value;
        if (value == 4 || value >= 240)
            continue;
        remaining--;
        if (remaining != 0)
            continue;
        if (offset == 15) {
            ds[(uint16_t)(output + output_length++)] = 40;
            result++;
        }
        break;
    }
    ds[(uint16_t)(output + output_length)] = 0;
    return result;
}

/* 0x2B334 - expand a glyph-mask prefix and append a run of raw text.
 *
 * `src` holds one mask byte per source cell.  `out` is the destination
 * buffer and `length` is a byte string giving, per cell, how many raw text
 * bytes follow it.  For every cell the original reads mask byte `b`: when `b`
 * is non-zero it emits a marker (96 for 1, 208 for anything else) and then,
 * for 3 and 4, a second code (96 for 3, 208 for 4).  After the markers the raw
 * byte at `length[i]` is appended at `out[i + out_len]`.  Cells whose mask
 * byte is 0 contribute only that raw byte.  Every store lands at
 * `out + i + out_len`, so the running marker count shifts later cells along.
 * The loop runs while `i <= byte_strlen_n(length)` and returns 1.
 *
 * The original keeps the two indices in `r13` (source index) and `r12`
 * (destination index); both wrap at 8 bits, which the casts preserve. */
uint16_t expand_glyph_mask(uint8_t *ds, uint16_t src, uint16_t out,
                           uint16_t length)
{
	uint8_t n = byte_strlen_n(ds, length);
	uint8_t i;
	uint8_t out_len = 0;
	unsigned guard = 0;

	/* `i` is 8-bit like `r13`; stop instead of wrapping forever when n is 255. */
	for (i = 0; i <= n && guard++ < 256; i++) {
		uint8_t b = ds[(uint16_t)(src + i)];
		uint8_t code = 0;

		if (b == 0)
			goto copy_raw;

		ds[(uint16_t)(out + i + out_len)] = (b == 1) ? 96 : 208;
		out_len++;

		if (b == 4)
			code = 208;
		else if (b == 3)
			code = 96;

		if (code != 0) {
			ds[(uint16_t)(out + i + out_len)] = code;
			out_len++;
		}

copy_raw:
		ds[(uint16_t)(out + i + out_len)] = ds[(uint16_t)(length + i)];
	}

	return 1;
}

/* 0x0E1E8 - copy a NUL-terminated string including its terminator: the ROM
 * takes `byte_strlen_n(src) + 1` and memcpy's that many bytes. */
uint16_t string_copy_with_nul(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t n = (uint16_t)(byte_strlen_n(ds, src) + 1u);

    memcpy_nn(ds, dst, src, n);
    return dst;
}

/* 0x0ED88 - true for 0xF0..0xFF only, i.e. the lead-byte range of the text
 * encoding: the scanner at 0x0A568 rejects a byte that fails this test and
 * then compares the survivors against 0xF1 and 0xF2 explicitly.  The compare
 * in the ROM is `cmp r2, 240` on the low byte alone - the high half of er0 is
 * overwritten at entry - and it behaves unsigned, so 0xF0 itself passes. */
uint16_t byte_is_lead(uint8_t *ds, uint16_t value)
{
    (void)ds;
    return ((uint8_t)value >= 0xF0u) ? 1 : 0;
}
