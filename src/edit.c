/* edit.c - reimplementation of the fx-580VN X edit-box support routines.
 *
 * Complements the print/keyboard clusters; every function here is checked
 * against the original ROM by tests/t_edit.c.
 */
#include "edit.h"

#include <stddef.h>

/* 0x0AC60 - prev_unit_off.  Machine translation of the ROM routine:
 *
 *   push er12
 *   mov er12, er2                  ; probe
 *   cmp er0, er2  / bc ge          ; lo >= probe (UNSIGNED `ge`) → answer 0
 *   add er12, -1                   ; probe - 1
 *   cmp er0, er12 / bc eq          ; probe == lo + 1 → answer probe - 1
 *   add er12, -1                   ; probe - 2
 *   l r0, [er12]                   ; BYTE load - the destination register
 *                                  ; size picks the access width
 *   cmp r0, 240 / bc ge            ; `ge` again: byte >= 240 unsigned, i.e.
 *                                  ; the marker/tag range 0xF0-0xFF
 *   add er12, 1                    ; plain char: settle on probe - 1
 *
 * So a marker byte (0xF0-0xFF) before the cursor hides a two-byte unit whose
 * start is one further back; everything else is a single-byte unit.  In this
 * annotation scheme plain `ge`/`eq` tests are the unsigned forms; the signed
 * variants are written `ges`/`lts` and do not appear here.
 */
uint16_t prev_unit_off(uint8_t *ds, uint16_t lo, uint16_t probe)
{
    uint16_t r12 = probe;

    if (lo >= probe) {
        r12 = 0;
    } else {
        r12 = (uint16_t)(probe - 1);
        if (lo != r12) {
            r12 = (uint16_t)(probe - 2);
            if (ds[r12] < 0xF0)                 /* not a marker byte */
                r12 = (uint16_t)(r12 + 1);
        }
    }
    return r12;
}

/* 0x17B16 - the same step with the lower bound cleared: `mov er0, 0` then
 * `bl f_0AC60` with the caller's er14 as the probe.  The answer is left in
 * **er14** (`mov er14, er0`); er0 is then thrown away by `pop xr0`, which
 * restores r0..r3, so the routine comes back with the caller's er0 intact. */
uint16_t prev_unit_off_open(uint8_t *ds, uint16_t arg, uint16_t probe,
                            uint16_t *er14_out)
{
    if (er14_out)
        *er14_out = prev_unit_off(ds, 0, probe);
    return arg;
}

uint16_t encode_editor_token(uint8_t *ds, uint8_t value, uint16_t output)
{
    uint8_t mode = ds[0xD117];
    uint8_t extended = ds[0xD13F];
    uint8_t token[4];
    size_t length = 0;

    if (mode & 0x10) {
        switch (mode) {
        case 17:
            token[length++] = value ? (extended ? 73 : 17)
                                    : (extended ? 72 : 16);
            break;
        case 18:
            if (value == 0) {
                token[length++] = extended ? 253 : 114;
                if (extended)
                    token[length++] = 19;
            } else {
                token[length++] = extended ? 251 : 196;
                if (extended)
                    token[length++] = 66;
            }
            break;
        case 19:
            if (value == 0) {
                uint16_t index = (uint16_t)(ds[0xD5EA] + 192);
                token[length++] = ds[(uint16_t)(0x8ACE + index)];
            } else {
                token[length++] = 76;
                token[length++] = 45;
                token[length++] = 82;
            }
            break;
        case 20:
            if (value != 0) {
                token[length++] = extended ? 251 : 82;
                if (extended)
                    token[length++] = 65;
            }
            break;
        default:
            if (value == 0)
                token[length++] = extended ? 71 : 70;
            break;
        }
    }

    for (size_t i = 0; i < length; i++)
        ds[(uint16_t)(output + i)] = token[i];
    if (length != 0)
        ds[(uint16_t)(output + length++)] = extended ? 165 : 61;
    ds[(uint16_t)(output + length)] = 0;
    return output;
}

uint8_t adjust_editor_state(uint8_t *ds, uint16_t command)
{
    uint8_t submode = ds[0xD112];
    uint16_t table_offset = (uint16_t)((uint8_t)(submode - 1) * 2);
    uint16_t submode_table = (uint16_t)(0x1BCE + table_offset);
    uint16_t limit_table = (uint16_t)(0x804E + table_offset);
    uint8_t reset_submode = ds[(uint16_t)(submode_table + 1)];
    uint8_t limit = ds[limit_table];
    uint8_t changed = 0;
    uint8_t position = ds[0xD135];
    uint8_t layer = ds[0xD136];

    if (command == 0xFC1C) {
        if (position > 1) {
            ds[0xD135] = (uint8_t)(position - 1);
            changed = 1;
        }
    } else if (command == 0xFC1D) {
        if (position < limit &&
            ((submode == 5 && layer == 1) ||
             (submode == 6 && (layer == 1 || layer == 2)))) {
            ds[0xD135] = (uint8_t)(position + 1);
            changed = 1;
        }
    } else if (command == 0xFC1E || command == 0xFC2D) {
        if (layer == 3) {
            if (position < limit) {
                ds[0xD135] = (uint8_t)(position + 1);
                ds[0xD136] = 1;
                changed = 1;
            }
        } else if (submode == 4 || position != limit ||
                   (submode == 6 && layer == 1)) {
            ds[0xD136] = (uint8_t)(layer + 1);
            changed = 1;
        }
    } else if (command == 0xFC1F) {
        if (layer == 1) {
            if (position != 1) {
                ds[0xD135] = (uint8_t)(position - 1);
                ds[0xD136] = reset_submode;
                changed = 1;
            }
        } else {
            ds[0xD136] = (uint8_t)(layer - 1);
            changed = 1;
        }
    }

    if (changed) {
        ds[0xD119] = 0;
        ds[0xD118] = 0;
        ds[0xD148] = 0;
    }
    return changed ? 0 : 1;
}

uint16_t editor_special_to_ascii(uint16_t code)
{
    switch (code) {
    case 0x0060: return 'H';
    case 0x0077: return 'E';
    case 0x0078: return 'F';
    case 0x0079: return 'G';
    case 0x00C0: return 'B';
    case 0x00D0: return 'I';
    case 0xFC2E: return '@';
    case 0xFC36: return 'C';
    case 0xFC3A: return 'K';
    case 0xFC54: return 'D';
    default: return code;
    }
}

uint8_t editor_classify_code(uint8_t code)
{
    if (code == 24)
        return 0;
    if (code == 47)
        return 1;
    if (code >= 80 && code <= 83)
        return (uint8_t)(2 + code - 80);
    if (code == 104)
        return 6;
    if (code >= 114 && code <= 116)
        return (uint8_t)(7 + code - 114);
    if (code == 125)
        return 10;
    if (code >= 200 && code <= 202)
        return (uint8_t)(11 + code - 200);
    return 15;
}

uint16_t encode_editor_character(uint8_t *ds, uint16_t output, uint8_t index)
{
    uint8_t extended = ds[0xD13F] != 0 || ds[0xD14E] != 0;
    uint8_t length = 1;

    if (index < 5) {
        ds[output] = (uint8_t)(extended ? 231 - index : 208 + index);
    } else if (index == 5) {
        if (!extended) {
            ds[output] = 32;
        } else if (ds[0xD14E] == 0) {
            ds[output] = 251;
            ds[(uint16_t)(output + 1)] = 67;
            length = 2;
        } else {
            length = 0;
        }
    } else {
        ds[output] = (uint8_t)(extended ? 232 - index : 207 + index);
    }

    ds[(uint16_t)(output + length)] = 0;
    return output;
}

uint8_t classify_editor_token(uint8_t value, uint8_t marker)
{
    if (marker == 251) {
        if (value < 16)
            return 10;
        if (value < 32)
            return 8;
        if (value < 48)
            return 7;
        return 15;
    }
    if (marker == 253)
        return 6;
    if (marker == 254 || marker == 250)
        return 3;

    if (value == 0)
        return 10;
    if (value < 32)
        return 15;
    if (value < 35)
        return 6;
    if (value == 35)
        return 10;
    if (value == 36)
        return 5;
    if (value == 37)
        return 15;
    if (value == 44)
        return 9;
    if (value < 64)
        return 4;
    if (value < 80)
        return 5;
    if (value < 96)
        return 0;
    if (value < 160)
        return 1;
    if (value < 184)
        return 2;
    if (value < 192)
        return 3;
    if (value < 200)
        return 1;
    if (value < 208)
        return 2;
    return 3;
}

uint16_t clamp_editor_offset(uint16_t start, uint16_t probe, uint16_t limit)
{
    if ((int16_t)start < 0) {
        if (probe >= limit)
            return start;
        uint16_t distance = (uint16_t)(probe - start);
        if (distance > limit)
            return (uint16_t)(start + limit - probe);
        return 0;
    }

    if (probe <= limit)
        return 0;
    uint16_t distance = (uint16_t)(probe - start);
    if (distance >= limit)
        return start;
    return (uint16_t)(probe - limit);
}
