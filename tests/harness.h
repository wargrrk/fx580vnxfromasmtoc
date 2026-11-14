/* harness.h - shared differential-test harness.
 *
 * Each test puts the machine into a fully defined state, runs a ROM function
 * in the C nX-U8/100 core, then runs the C reimplementation on a byte image
 * with identical contents and compares the results.
 */
#ifndef NX_TEST_HARNESS_H
#define NX_TEST_HARNESS_H

#include <stdint.h>

#include "../emu/nx8.h"

#define IMG_SIZE 0x10000
#define RAM_BASE 0xD000
#define CMP_END 0xEC00
#define STACK_TOP 0xED00
/* The stack lives at [CMP_END, 0xF000): pushes grow *down* from STACK_TOP, and
 * the harness puts the stack arguments at STACK_TOP and above, which the called
 * routine reads and may scribble on.  Everything else that latches a store must
 * be compared - RAM below the stack, the SFRs at 0xF000 and the LCD
 * framebuffer at 0xF800.  Leaving the last two out let a translation drop a
 * hardware poke, or render nothing to the framebuffer, and still pass. */
#define CMP_GAP_LO CMP_END
#define CMP_GAP_HI 0xF000

extern uint8_t image[IMG_SIZE];
extern uint8_t after_rom[IMG_SIZE];
extern uint8_t after_c[IMG_SIZE];
/* The register file as it stood when the ROM returned: r0..r15, one byte each,
 * indexed the way the core numbers them (er0 is r0/r1).  A few routines put
 * their result in a register that is not er0 - 0x143D6 answers in r5 and r2
 * alone - and without this image the differential test cannot see them at all. */
extern uint8_t after_rom_regs[16];
extern int checks;
extern int failures;
/* Cases whose ROM run never returned; reported separately so that skipping
 * them cannot hide a difference. */
extern int rom_no_return;

typedef struct {
    uint16_t er[8];      /* er0, er2, ... er14 */
    uint16_t stk[8];     /* stack arguments, [sp], [sp+2], ... */
    int nstk;
    uint16_t sp;         /* 0 = default (STACK_TOP), else custom */
} Args;

uint32_t rnd(void);
void random_image(uint32_t seed);
void begin_c(void);
void harness_load_rom(const char *path);
Nx8Machine *harness_machine(void);
uint8_t *harness_rom(void);

uint16_t run_rom(uint32_t addr, const Args *a, const char *what);
/* Same, but reports whether the ROM returned at all.  Some routines loop for
 * ever on out-of-domain arguments; that is a property of the firmware, not a
 * translation bug, so the batch runner needs to tell the two apart instead of
 * reporting a memory mismatch against an all-zero image. */
uint16_t run_rom_ex(uint32_t addr, const Args *a, const char *what,
                    int *returned);
void check_ret(const char *what, uint16_t rom_ret, uint16_t c_ret);
/* Compare the register bytes selected by `mask` (bit N = rN) against the
 * register file the ROM left behind.  `mine` is a 16-byte image the caller
 * builds: start from the registers it set up, then overwrite whatever the C
 * body is supposed to have produced. */
void check_regs(const char *what, const uint8_t *mine, uint16_t mask);
void check_mem(const char *what, uint32_t from, uint32_t to);
/* Every byte of the data space a store can latch, except the stack window. */
void check_mem_all(const char *what);
/* Same, for a run whose stack lives somewhere other than STACK_TOP. */
void check_mem_all_except(const char *what, uint32_t gap_lo, uint32_t gap_hi);

void test_string(void);
void test_mem(void);
void test_display(void);
void test_font(void);
void test_print(void);
void test_keyboard(void);
void test_edit(void);
void test_diagnostic(void);
void test_hw(void);
void test_draw(void);
void test_record(void);
void test_arith(void);
void test_emu(void);

#endif
