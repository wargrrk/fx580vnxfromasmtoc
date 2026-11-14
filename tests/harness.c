/* harness.c - machine setup and comparison helpers. */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t image[IMG_SIZE];
uint8_t after_rom[IMG_SIZE];
uint8_t after_c[IMG_SIZE];
uint8_t after_rom_regs[16];
int checks;
int failures;
int rom_no_return;

static Nx8Machine m;
static uint8_t rom[0x40000];
static uint32_t rng_state;

uint32_t rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

/* Uniform random bytes almost never land on the values the firmware actually
 * branches on - 0, 9, 10, 0x7F, 0x80, 0xF0, 181 - so a purely random image
 * leaves every unsigned/signed boundary and every off-by-one untested.  About
 * a third of the bytes are therefore drawn from the boundary table, and a
 * short run of repeats is emitted now and then so that the loops which compare
 * whole blocks see equal neighbours. */
static const uint8_t boundary_bytes[] = {
    0x00, 0x01, 0x02, 0x03, 0x09, 0x0A, 0x0B, 0x0D, 0x0F, 0x10,
    0x1F, 0x20, 0x28, 0x30, 0x31, 0x36, 0x49, 0x4C, 0x55, 0x64,
    0x7E, 0x7F, 0x80, 0x81, 0x9F, 0xA0, 0xB5, 0xC3, 0xEF, 0xF0,
    0xF1, 0xF2, 0xFC, 0xFD, 0xFE, 0xFF
};

void random_image(uint32_t seed)
{
    rng_state = seed * 2654435761u + 1;
    memcpy(image, rom, IMG_SIZE);

    uint32_t a = RAM_BASE;
    while (a < IMG_SIZE) {
        uint32_t roll = rnd() % 100u;
        if (roll < 33u) {
            uint32_t idx = rnd() % (sizeof(boundary_bytes) / sizeof(boundary_bytes[0]));
            image[a++] = boundary_bytes[idx];
        } else if (roll < 45u) {
            uint8_t fill = (uint8_t)((rnd() & 0xFF) | 1u);
            if ((rnd() & 3u) == 0)
                fill = (uint8_t)(rnd() & 1u ? 0x00 : 0xFF);
            uint32_t run = 2u + (rnd() % 6u);
            while (run-- && a < IMG_SIZE)
                image[a++] = fill;
        } else {
            image[a++] = (uint8_t)(rnd() & 0xFF);
        }
    }
}

void begin_c(void)
{
    memcpy(after_c, image, IMG_SIZE);
}

static void machine_reset(void)
{
    nx8_init(&m);
    nx8_load_rom(&m, rom, 0x40000);
    memset(m.mem + RAM_BASE, 0, IMG_SIZE - RAM_BASE);
    m.sp = STACK_TOP;
    m.psw = 0;
    m.dsr = 0;
    m.csr = 0;
    m.faulted = 0;
    m.ignored_writes = 0;
    m.steps = 0;
}

uint16_t run_rom_ex(uint32_t addr, const Args *a, const char *what,
                    int *returned)
{
    machine_reset();
    memcpy(m.mem + RAM_BASE, image + RAM_BASE, IMG_SIZE - RAM_BASE);
    if (a && a->sp)
        m.sp = a->sp;
    for (int i = 0; i < 8; i++)
        nx8_set_er(&m, (unsigned)(i * 2), a ? a->er[i] : 0);
    if (a)
        for (int i = 0; i < a->nstk; i++) {
            m.mem[m.sp + 2 * i] = (uint8_t)(a->stk[i] & 0xFF);
            m.mem[m.sp + 2 * i + 1] = (uint8_t)(a->stk[i] >> 8);
        }
    size_t steps = nx8_call(&m, addr, 8000000);
    if (steps == (size_t)-1) {
        if (returned) {
            *returned = 0;
            rom_no_return++;
        } else {
            printf("FAIL %-24s ROM call did not return (fault=%d, %s, pc=%04X)\n",
                   what, m.faulted, m.fault_msg ? m.fault_msg : "-", m.pc);
            failures++;
        }
        memset(after_rom, 0, sizeof(after_rom));
        return 0;
    }
    if (returned)
        *returned = 1;
    memcpy(after_rom, image, IMG_SIZE);
    memcpy(after_rom + RAM_BASE, m.mem + RAM_BASE, IMG_SIZE - RAM_BASE);
    memcpy(after_rom_regs, m.r, sizeof(after_rom_regs));
    return nx8_er(&m, 0);
}

uint16_t run_rom(uint32_t addr, const Args *a, const char *what)
{
    return run_rom_ex(addr, a, what, NULL);
}

void check_ret(const char *what, uint16_t rom_ret, uint16_t c_ret)
{
    checks++;
    if (rom_ret != c_ret) {
        failures++;
        printf("FAIL %-24s ret: rom=%04X c=%04X\n", what, rom_ret, c_ret);
    }
}

void check_regs(const char *what, const uint8_t *mine, uint16_t mask)
{
    const int show = 8;
    int shown = 0;
    int total = 0;

    checks++;
    for (int reg = 0; reg < 16; reg++) {
        if (!((mask >> reg) & 1u))
            continue;
        if (after_rom_regs[reg] == mine[reg])
            continue;
        if (shown < show) {
            printf("FAIL %-24s reg r%-2d rom=%02X c=%02X\n", what, reg,
                   after_rom_regs[reg], mine[reg]);
            shown++;
        }
        total++;
    }
    if (total == 0)
        return;
    failures++;
    if (total > shown)
        printf("FAIL %-24s regs: %d differing bytes (first %d listed)\n", what,
               total, shown);
    else
        printf("FAIL %-24s regs: %d differing bytes\n", what, total);
}

void check_mem(const char *what, uint32_t from, uint32_t to)
{
    const int show = 8;
    int shown = 0;
    int total = 0;

    checks++;
    for (uint32_t a = from; a < to; a++) {
        if (after_rom[a] == after_c[a])
            continue;
        if (shown < show) {
            printf("FAIL %-24s mem: %04X rom=%02X c=%02X\n", what, a,
                   after_rom[a], after_c[a]);
            shown++;
        }
        total++;
    }
    if (total == 0)
        return;

    failures++;
    /* The count is what tells "one stray pixel" from "the whole routine is
     * wrong"; the addresses above only show where it starts. */
    if (total > shown)
        printf("FAIL %-24s mem: %d differing bytes (first %d listed)\n", what,
               total, shown);
    else
        printf("FAIL %-24s mem: %d differing bytes\n", what, total);
}

void check_mem_all_except(const char *what, uint32_t gap_lo, uint32_t gap_hi)
{
    check_mem(what, RAM_BASE, gap_lo);
    check_mem(what, gap_hi, IMG_SIZE);
}

void check_mem_all(const char *what)
{
    check_mem_all_except(what, CMP_GAP_LO, CMP_GAP_HI);
}

void harness_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
        fprintf(stderr, "ROM must be %zu bytes\n", sizeof(rom));
        exit(1);
    }
    fclose(f);
}

Nx8Machine *harness_machine(void)
{
    return &m;
}

uint8_t *harness_rom(void)
{
    return rom;
}
