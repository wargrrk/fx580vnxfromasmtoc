/* batch.h - manifest-driven differential testing.
 *
 * Most of the ROM is made of small leaf routines whose whole behaviour is
 * "read these bytes, write those bytes, return this word".  Writing a custom
 * test loop for each one is pure boilerplate, so the batch runner takes a
 * table instead:
 *
 *     {"name", 0xROMADDR, "pp|w", adapter}
 *
 * The shape describes the incoming arguments: one character per register
 * argument (`er0`, `er2`, ...) and, after a `|`, one per stack argument.
 *
 *     b   byte                0x00..0xFF
 *     w   word                0x0000..0xFFFF
 *     p   RAM pointer         0xD000..0xEBFE (kept clear of the stack)
 *     s   small count         0x00..0x20
 *     z   zero
 *
 * A `!` anywhere in the shape means "do not compare the return value" - used
 * for routines whose result word is whatever the last callee happened to
 * leave in er0.
 *
 * Every case is replayed on several random memory images; the C adapter gets
 * the same arguments and must produce the same `er0` and the same data space.
 * Adapters exist because the C functions have real signatures - see the
 * W0/W1/W2/... macros below.
 */
#ifndef NX_TEST_BATCH_H
#define NX_TEST_BATCH_H

#include <stdint.h>

typedef uint16_t (*BatchFn)(uint8_t *ds, const uint16_t *er,
                            const uint16_t *stk);

typedef struct {
    const char *name;
    uint32_t addr;
    const char *shape;
    BatchFn fn;
    int rounds;          /* 0 = default */
} BatchCase;

void run_batch(const BatchCase *cases, int count);

/* Adapters.  `ds` is the flat 64 KiB data space the C routines operate on. */
#define W0(f)                                                                  \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        (void)e; (void)s;                                                      \
        return f(ds);                                                          \
    }
#define W1(f)                                                                  \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        (void)s;                                                               \
        return f(ds, e[0]);                                                    \
    }
#define W2(f)                                                                  \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        (void)s;                                                               \
        return f(ds, e[0], e[1]);                                              \
    }
#define W3(f)                                                                  \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        (void)s;                                                               \
        return f(ds, e[0], e[1], e[2]);                                        \
    }
#define W4(f)                                                                  \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        (void)s;                                                               \
        return f(ds, e[0], e[1], e[2], e[3]);                                  \
    }
#define W1S1(f)                                                                \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        return f(ds, e[0], s[0]);                                              \
    }
#define W2S1(f)                                                                \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        return f(ds, e[0], e[1], s[0]);                                        \
    }
#define W1S2(f)                                                                \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        return f(ds, e[0], s[0], s[1]);                                        \
    }
#define W2S2(f)                                                                \
    static uint16_t a_##f(uint8_t *ds, const uint16_t *e, const uint16_t *s)   \
    {                                                                          \
        return f(ds, e[0], e[1], s[0], s[1]);                                  \
    }

#define BATCH(...) run_batch((const BatchCase[]){__VA_ARGS__},                 \
                             (int)(sizeof((const BatchCase[]){__VA_ARGS__}) /   \
                                   sizeof(BatchCase)))

void test_batch(void);

#endif
