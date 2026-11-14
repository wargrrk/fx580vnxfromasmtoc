/* edit.h - pieces of the text-entry box that shows the expression.
 *
 * The editor region sits around 0x0AC30-0x0C000 in the ROM.  Its text buffer
 * is an array of two-byte units at 16-bit offsets in the data space.
 */
#ifndef NX_EDIT_H
#define NX_EDIT_H

#include <stdint.h>

/* 0x0AC60 - step one display unit backwards.  `lo` bounds the region,
 * `probe` is the position the caller walks back from.  The result is:
 *   - 0 when `lo` is at or past `probe` (nothing left in the region),
 *   - `probe - 1` when `probe` sits directly after `lo`,
 *   - otherwise the byte before `probe` decides the unit width: a byte of
 *     0xF0..0xFF (the marker/tag range) means the unit is two bytes and the
 *     answer is `probe - 2`; any other byte means a one-byte unit and the
 *     answer is `probe - 1`.
 * The region compare is the ROM's unsigned `bc ge` test.
 */
uint16_t prev_unit_off(uint8_t *ds, uint16_t lo, uint16_t probe);

/* 0x17B16 - prev_unit_off with `lo` fixed at 0; the answer lands in er14 and the
 * caller's er0 comes straight back (`pop xr0` restores r0..r3). */
uint16_t prev_unit_off_open(uint8_t *ds, uint16_t arg, uint16_t probe,
                            uint16_t *er14_out);

/* 0x20AA8 - encode an editor mode/value token at `output`. */
uint16_t encode_editor_token(uint8_t *ds, uint8_t value, uint16_t output);

/* 0x0DC1E - adjust editor cursor/submode state for an editor command. */
uint8_t adjust_editor_state(uint8_t *ds, uint16_t command);

/* 0x2ABE4 - map editor control codes to their diagnostic ASCII letters. */
uint16_t editor_special_to_ascii(uint16_t code);

/* 0x2035C - map an editor byte to its compact character class. */
uint8_t editor_classify_code(uint8_t code);

/* 0x24E24 - classify the byte behind a pointer, or 5 when it has no glyph. */
uint16_t byte_class_or_5(uint8_t *ds, uint16_t ptr);

/* 0x0E7A8 - encode an editor character index into its internal token. */
uint16_t encode_editor_character(uint8_t *ds, uint16_t output, uint8_t index);

/* 0x15FE4 - classify an editor byte according to its marker type. */
uint8_t classify_editor_token(uint8_t value, uint8_t marker);

/* 0x0D742 - clamp an editor offset using the stack-passed extent limit. */
uint16_t clamp_editor_offset(uint16_t start, uint16_t probe, uint16_t limit);

#endif /* NX_EDIT_H */
