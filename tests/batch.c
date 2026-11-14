/* batch.c - the manifest-driven differential runner. */
#include "batch.h"

#include "harness.h"

#include <stdio.h>
#include <string.h>

#define MAX_STK 4
#define DEFAULT_ROUNDS 6

static uint16_t arg_for(char kind, uint32_t *extra)
{
    switch (kind) {
    case 'b':
        return (uint16_t)(rnd() & 0xFF);
    case 'w':
        return (uint16_t)(rnd() & 0xFFFF);
    case 'p':
        /* Keep the pointer inside RAM and clear of the 0xEC00 stack window. */
        return (uint16_t)(0xD000u + (rnd() % 0x1BF0u));
    case 's':
        return (uint16_t)(rnd() % 0x21u);
    case 'm':
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

        for (const char *p = cs->shape; *p; p++) {
            if (*p == '!')
                continue;
            if (*p == '|') {
                stack = 1;
                continue;
            }
            uint16_t value = arg_for(*p, &extra);
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

        Args a = {.nstk = nstk};
        memcpy(a.er, er, sizeof(er));
        memcpy(a.stk, stk, sizeof(stk));

        uint16_t rom_ret = run_rom(cs->addr, &a, cs->name);
        begin_c();
        uint16_t c_ret = cs->fn(after_c, argv, stk);

        if (!strchr(cs->shape, '!'))
            check_ret(cs->name, rom_ret, c_ret);
        check_mem(cs->name, RAM_BASE, CMP_END);
    }
}

void run_batch(const BatchCase *cases, int count)
{
    for (int i = 0; i < count; i++)
        run_case(&cases[i], cases[i].rounds > 0 ? cases[i].rounds
                                                : DEFAULT_ROUNDS);
}
