# Translation progress

How much of the fx-580VN X firmware has a C body that is verified against the
ROM, and what is left.

## Measuring

```sh
python3 tools/coverage.py        # totals
python3 tools/coverage.py -v     # plus the list of translated routines
python3 tools/frontier.py        # routines that can be translated next
```

`tools/coverage.py` counts a routine as translated when its entry address
appears in an address annotation *directly above a declaration* in `src/*.c` /
`src/*.h` (`/* 0xNNNNN - ... */`, the convention every C function in this
project uses).  The adjacency matters: merely mentioning an address elsewhere -
a TODO note, a prose range such as `0x0AC30-0x0C000`, or a data argument like
`subtract_words(rom, 0x20000, ...)` - does not count, so the percentage cannot
be inflated without writing a function.  Both tools share this rule through
`tools/romdb.py`.

A routine here is a *function*, not a chunk: `f_XXXXX` entries own the
`.l_NNN` blocks that follow them, and only the instructions control can reach
inside that span are counted.  Counting chunks instead (the older rule, which
stopped at the first `b`) hid callees and under-counted bodies - 0x0C362 looked
like a 17-instruction leaf until the walk followed its `bc ne` past the
`b LEAVE` into the `.l_028` block that calls 0x24F26.

Bodies and sizes come from `build/disas.json`, so the two numbers are measured
on the same partition:

| metric | current |
| --- | --- |
| routines | 159 / 763 (20.8%) |
| instructions | 3907 / 32751 (11.9%) |

(Measured with `python3 tools/coverage.py`; the instruction total counts the
ret/rt-delimited bodies the tooling treats as one routine each, not every word
in the ROM.)

## Instruction-set traps

Properties of the nX-U8/100 that cost a full debugging cycle each while
translating `0x1D950`/`0x1DCCE`/`0x2E2BC`/`0x08E62`.  They are checked against
the core manual (`resource/nX-U8100 Core Instruction Manual.txt`) or against
`emu/nx8.c`, which is the ground truth the diff-test compares to, not guessed:

| trap | rule | where the manual says so |
| --- | --- | --- |
| `BC GE` / `BC LT` | **unsigned** (`C=0` / `C=1`); the signed forms are `BC GES` / `BC LTS` | §3.3, `Bcond` condition table |
| multi-byte `l`/`st` | the effective address is forced even: address bit 0 is ignored, so an odd pointer rounds *down* | §1.3.5.3 "Word Boundaries" |
| register groups | `er` = 2 bytes, `xr` = 4 (`r4..r7`), `qr` = 8 (`r8..r15`) | `tools/nx8.py`, instruction formats |
| immediates | printed in **decimal** in `resource/disas.asm`: `add r2, 22` is +0x16 | `tools/disasm.py` |
| `[ea+]` on a multi-byte access | the bump happens first and the result is rounded down to even *again*, so an odd `ea` stays usable | `load_store()`, `bump_ea()` in `emu/nx8.c` |
| multi-byte `st` | the bytes go out in **descending** address order (`ix = length-1 .. 0`) | `load_store()` in `emu/nx8.c` |
| the `ENTER` frame | `push lr` pushes **lcsr and lr** (four bytes), so an `ENTER` frame is 16 bytes and the stacked arguments of a routine are at `16/18/20[er14]`, not `14/16/18` | `H_PUSHL` in `emu/nx8.c` (`mask & 8` pushes both) |
| `BC AL` | condition nibble 14 really is unconditional; a backward `bc al` is a plain loop jump and the exit test sits at the loop head | §3.3 condition table (1000..1110 = NE, EQ, NV, OV, PS, NS, AL) |

Consequence worth stating once: a block copy must not read and write one byte at
a time.  The ROM loads ten bytes into `er6`+`qr8`, stores them, and only then
loads the next ten, so an overlapping source/destination sees the first store -
the interleaved form is wrong for every overlapping pair.  Store *order* within
one group, on the other hand, is unobservable for a byte copy, because every
byte is written exactly once.

Two more that only show up in return values:

* `mov r0, x` writes the low byte only, so `er0` keeps the caller's `r1` in its
  high half.  That is why `0x2805E` returns 0x0707/0x0505 and `0x2AB3C` returns
  `arg & 0xFF00` rather than 0.
* `DIV ERn, Rm` puts the quotient in `ERn` and the remainder in `Rm`, and its Z
  flag reports the *dividend*.  `MUL ERn, Rm` multiplies the low bytes only.

The arithmetic core at 0x27000-0x31000 contains no `daa`/`das` at all - its
digit work is byte-decrement loops and `addc`/`cmpc` carry chains - so plain C
loops model it; the decimal-adjust instructions live in other regions.

## How a routine gets translated

1. `python3 tools/frontier.py` lists routines whose callees are already done -
   only those can be diff-tested immediately.  `ENTER`/`LEAVE` count as done
   for this purpose even though they have no C body: the frame they build is
   modelled by the calling convention itself, and `tools/romdb.py` lists them
   in `STRUCTURAL_ENTRIES`.
2. `python3 tools/fdump.py 0xNNNNN` prints the annotated body; every branch
   target, call and data label is resolved.
3. The C body goes into the module for its subsystem, with the ROM address in
   the comment and a note for every place where the original is surprising
   (8-bit wrap-around, flags left in `er0`, bytes burned down on the stack...).
4. A manifest line in `tests/t_*.c` (`{"name", 0xNNNNN, "shape", adapter, 0}`)
   makes `tests/batch.c` run the ROM routine and the C routine on several random
   data spaces and compare the result word plus all of RAM.
5. `bash tools/test.sh` must stay at `0 failures`.

## Where the remaining work is

The call graph is the schedule. `tools/frontier.py` currently reports 232
routines that are ready (all callees already translated), and the count grows
as routines land, so the work does not run out.

Rough shape of what is left, by ROM region:

| region | contents |
| --- | --- |
| `0x08C00-0x09500` | screen buffers, brushes, brush/blend plot primitives |
| `0x09700-0x0A600` | character/cell grid, editor cursor, key handling glue |
| `0x0AC00-0x13000` | editor command dispatch and text buffer maintenance |
| `0x13000-0x1D000` | mode-specific input (`INPUT`/`EQN`/`MATRIX`/`VECTOR`) |
| `0x1D000-0x22000` | number formatting, converter, table of constants |
| `0x22000-0x27000` | diagnostics, serial/ID, contrast, device test screens |
| `0x27000-0x31000` | the arithmetic core: packed BCD float add/sub/mul/div and
  the transcendental kernels |

The arithmetic core is the largest single block and the most intricate: it is
~160 routines of packed-BCD digit loops that read and write the ten-byte record
at `0xD000`/`0xD010` (see `compare_bcd_records` and `decimal_record_predicate`
in `src/mem.c` for the flavour). Translating it is what will move the
instruction percentage fastest, because those routines are big.

## A note on the 30% target

30% of the instruction total is roughly 9800 instructions; at the
current 11.3% the project is ~3900 instructions in. Reaching 30% therefore means
translating on the order of 300 more routines, and the ones that are left are
bigger and more entangled than the ones done so far (the easy leaf
functions and hardware pokes are largely gone). Work in batches with
`tools/frontier.py` between them and the number stays honest.
