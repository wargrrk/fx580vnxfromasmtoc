# fx580vnxfromasmtoc
Compile .asm source code of Casio FX580 VN X (Vietnam Market) to .c source code

## Generate C ROM source

The address-annotated listing contains the complete ROM image. Generate a C
source file that embeds those bytes and exposes `fx580vnx_load_rom()` for the
nX-U8/100 emulator:

```sh
python3 tools/asm2c.py
gcc -std=c11 -Wall -Wextra -Iemu -c build/rom.c -o build/rom.o
```

The generator verifies that the listing has no conflicting or missing words.
This preserves the original machine code in a buildable C representation; it
does not translate the firmware into high-level C logic.

## Translate the firmware to C

Hand-written C reimplementations live in `src/`, one module per subsystem. Every
routine keeps the ROM address it replaces in a comment, uses the Casio calling
convention (`uint16_t f(uint8_t *ds, ...)`, arguments in `er0`/`er2`/`er4`,
result in `er0`) and works on the flat 64 KiB data space (`DSR` is always 0 on
this firmware).

Because the CPU core in `emu/nx8.c` is accurate enough to run the real ROM,
every C routine is checked against the original: the tests load a data space,
run the ROM routine, run the C routine on an identical copy, and compare the
returned `er0` plus **every byte a store can latch** - RAM, the SFRs at
`0xF000` and the LCD framebuffer at `0xF800` - skipping only the stack window at
`0xEC00-0xEFFF` that the harness itself fills with arguments.  Leaving the SFRs
and the framebuffer out used to let a translation drop a hardware poke, or
render nothing at all, and still pass.

Three things keep the comparison honest:

* The data space is filled from a table of boundary bytes (`0x00`, `0x09`,
  `0x0A`, `0x7F`, `0x80`, `0xF0`, `0xFF`, ...) rather than uniformly: the values
  the firmware branches on are then common instead of one-in-256.
* Arguments are drawn from per-kind boundary tables first, and two-argument
  routines are swept over the **whole cross product** of theirs (sixty-four
  pairs, not eight) - a single shared draw is how the box and line routines
  stayed green while being wrong for every line wider than 127 columns.  A lone
  byte argument is simply tried for all 256 values.
* If the ROM itself never returns for some argument the case is counted and
  reported, never silently skipped, and is not compared against a shredded
  image.

`tools/mutation.sh` plants eight deliberate bugs - the ones this project has
actually made - and fails unless the suite catches each of them, so "0 failures"
cannot quietly come from weak checks.

```sh
bash tools/test.sh          # differential tests + cross-check
bash tools/test.sh --quick  # differential tests only
./tools/mutation.sh         # prove the suite still catches planted bugs
```

### Adding a routine

```sh
python3 tools/frontier.py -n 40      # routines whose callees are already done
python3 tools/fdump.py 0x9CC4        # annotated body, labels resolved
```

Write the C body, then add one manifest line to the matching `tests/t_*.c`:

```c
BATCH({"name", 0x09CC4, "ww", a_name, 0})
```

The shape string describes the arguments: `b` byte, `w` word, `p` RAM pointer,
`s` small count, `z` zero, `|` starts the stack arguments, `!` anywhere means
"do not compare the return value". The runner takes care of the rest; the suite
must stay at `0 failures`.

### Progress

`python3 tools/coverage.py` reports how much of the firmware has a verified C
body (currently 20.8% of routines, 11.9% of instructions). See
[`docs/progress.md`](docs/progress.md) for the details and the plan for the
remaining work.
