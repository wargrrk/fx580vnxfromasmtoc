/* batch.c - the manifest-driven differential runner. */
#include "batch.h"

#include "harness.h"

#include <stdio.h>
#include <string.h>

#define MAX_STK 4
/* Every case is replayed with a fixed set of *boundary* arguments first and
 * then with random ones.  The firmware's decisions turn on specific values -
 * 10 for a digit count, 240/0xF0 for a lead byte, 181 for a row limit - and a
 * pure random draw hits those with probability ~1/256, so they are supplied
 * explicitly before the random tail. */
#define BOUNDARY_ROUNDS 8
#define RANDOM_ROUNDS 12

static const uint16_t boundary_b[] = {0x00, 0x01, 0x09, 0x0A,
                                      0x0C, 0x7F, 0x80, 0xFF};
static const uint16_t boundary_w[] = {0x0000, 0x0001, 0x00FF, 0x0100,
                                      0x7FFF, 0x8000, 0xFF00, 0xFFFF};
static const uint16_t boundary_p[] = {0xD000, 0xD002, 0xD1FF, 0xD201,
                                      0xEBF0, 0xEBFE, 0xD800, 0xE3D4};
static const uint16_t boundary_s[] = {0, 1, 2, 0x1F, 0x20};
static const uint16_t boundary_m[] = {0, 1, 2, 0x95, 0x96};
static const uint16_t boundary_z[] = {0};
#define TABLE_LEN(t) ((int)(sizeof(t) / sizeof((t)[0])))

/* The per-kind boundary table, so the cross-product sweep below can walk it. */
static const uint16_t *boundary_table(char kind, int *len)
{
    switch (kind) {
    case 'b': *len = TABLE_LEN(boundary_b); return boundary_b;
    case 'w': *len = TABLE_LEN(boundary_w); return boundary_w;
    case 'p': *len = TABLE_LEN(boundary_p); return boundary_p;
    case 's': *len = TABLE_LEN(boundary_s); return boundary_s;
    case 'm': *len = TABLE_LEN(boundary_m); return boundary_m;
    default:  *len = 1;  return boundary_z;
    }
}

/* `index` counts the register/stack slot, `round` the repetition, so each
 * argument gets its own boundary value instead of all of them sharing one. */
static uint16_t arg_for(char kind, int round, int index, uint32_t *extra)
{
    int pick = (round + index * 3) % BOUNDARY_ROUNDS;

    switch (kind) {
    case 'b':
        if (round < BOUNDARY_ROUNDS)
            return boundary_b[pick % (int)(sizeof(boundary_b) / sizeof(boundary_b[0]))];
        return (uint16_t)(rnd() & 0xFF);
    case 'w':
        if (round < BOUNDARY_ROUNDS)
            return boundary_w[pick % (int)(sizeof(boundary_w) / sizeof(boundary_w[0]))];
        return (uint16_t)(rnd() & 0xFFFF);
    case 'p':
        /* Keep the pointer inside RAM and clear of the 0xEC00 stack window.
         * Odd addresses are included on purpose: the core forces word
         * alignment, so the odd/even split is a behaviour, not a detail. */
        if (round < BOUNDARY_ROUNDS)
            return boundary_p[pick % (int)(sizeof(boundary_p) / sizeof(boundary_p[0]))];
        return (uint16_t)(0xD000u + (rnd() % 0x1BF0u));
    case 's':
        if (round < BOUNDARY_ROUNDS)
            return boundary_s[pick % TABLE_LEN(boundary_s)];
        return (uint16_t)(rnd() % 0x21u);
    case 'm':
        if (round < BOUNDARY_ROUNDS)
            return boundary_m[pick % TABLE_LEN(boundary_m)];
        return (uint16_t)(rnd() % 0x97u);
    case 'z':
        return 0;
    default:
        *extra = 1;
        return 0;
    }
}

static int count_args(const char *shape, int *nreg, int *nstk)
{
    *nreg = 0;
    *nstk = 0;
    int stack = 0;
    for (const char *p = shape; *p; p++) {
        if (*p == '!')
            continue;
        if (*p == '|') {
            stack = 1;
            continue;
        }
        if (stack)
            (*nstk)++;
        else
            (*nreg)++;
    }
    return *nstk <= MAX_STK;
}

/* One replay with explicit register and stack values.  `why` only shows up in
 * a failure message so a boundary sweep can be told from a random round. */
static void one_round(const BatchCase *cs, const uint16_t *er,
                      const uint16_t *stk, int nstk, const char *why)
{
    uint16_t argv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint16_t no_stack[MAX_STK] = {0, 0, 0, 0};
    Args a = {.nstk = nstk};

    if (!stk)
        stk = no_stack;
    memcpy(argv, er, sizeof(argv));
    memcpy(a.er, er, sizeof(a.er));
    memcpy(a.stk, stk, sizeof(a.stk));

    int returned = 0;
    uint16_t rom_ret = run_rom_ex(cs->addr, &a, cs->name, &returned);
    if (!returned)
        return;                 /* out of domain: counted in rom_no_return */
    begin_c();
    uint16_t c_ret = cs->fn(after_c, argv, stk);

    if (!strchr(cs->shape, '!'))
        check_ret(cs->name, rom_ret, c_ret);
    (void)why;
    check_mem_all(cs->name);
}

static void run_case(const BatchCase *cs, int rounds)
{
    int nreg = 0;
    int nstk = 0;

    if (!count_args(cs->shape, &nreg, &nstk)) {
        printf("FAIL %-24s shape has too many stack arguments\n", cs->name);
        failures++;
        return;
    }

    for (int round = 0; round < rounds; round++) {
        uint16_t er[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint16_t argv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint16_t stk[MAX_STK] = {0, 0, 0, 0};
        uint32_t extra = 0;
        int index = 0;
        int stack = 0;

        random_image((uint32_t)(cs->addr * 2654435761u + (uint32_t)round));

        int slot = 0;
        for (const char *p = cs->shape; *p; p++) {
            if (*p == '!')
                continue;
            if (*p == '|') {
                stack = 1;
                slot = 0;
                continue;
            }
            uint16_t value = arg_for(*p, round, slot++, &extra);
            if (extra) {
                printf("FAIL %-24s bad shape character '%c'\n", cs->name, *p);
                failures++;
                return;
            }
            if (stack) {
                stk[index++] = value;
            } else {
                argv[index] = value;
                er[index++] = value;
            }
        }

        (void)argv;
        one_round(cs, er, stk, nstk, "generated");
    }
}

/* Two-argument routines get the *whole* cross product of their boundary
 * tables.  A single shared draw on each slot only ever sees eight pairs of the
 * sixty-four, which is exactly how the box and line routines kept passing
 * while being wrong for every line wider than 127 columns. */
static void sweep_two(const BatchCase *cs)
{
    char k[3] = {0, 0, 0};
    int n = 0;

    for (const char *p = cs->shape; *p && n < 3; p++)
        if (*p != '!' && *p != '|')
            k[n++] = *p;
    if (n != 2 || strchr(cs->shape, '|'))
        return;

    int la, lb;
    const uint16_t *ta = boundary_table(k[0], &la);
    const uint16_t *tb = boundary_table(k[1], &lb);

    for (int i = 0; i < la; i++)
        for (int j = 0; j < lb; j++) {
            uint16_t er[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            er[0] = ta[i];
            er[1] = tb[j];
            random_image((uint32_t)(cs->addr * 2654435761u + (uint32_t)(i * 31 + j)));
            one_round(cs, er, NULL, 0, "sweep");
        }
}

/* A single byte argument has only 256 possible values, so there is no reason
 * to sample it: walk the lot. */
static void sweep_byte(const BatchCase *cs)
{
    const char *k = cs->shape;
    int n = 0;

    for (const char *p = k; *p; p++)
        if (*p != '!' && *p != '|')
            n++;
    if (n != 1 || strchr(cs->shape, '|') || *k != 'b')
        return;

    for (int v = 0; v < 256; v++) {
        uint16_t er[8] = {(uint16_t)v, 0, 0, 0, 0, 0, 0, 0};
        random_image((uint32_t)(cs->addr * 2654435761u + (uint32_t)v));
        one_round(cs, er, NULL, 0, "byte-sweep");
    }
}

void run_batch(const BatchCase *cases, int count)
{
    for (int i = 0; i < count; i++) {
        int rounds = cases[i].rounds > 0 ? cases[i].rounds
                                         : BOUNDARY_ROUNDS + RANDOM_ROUNDS;
        run_case(&cases[i], rounds);
        sweep_two(&cases[i]);
        sweep_byte(&cases[i]);
    }
}
