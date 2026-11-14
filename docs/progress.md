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
| routines | 120 / 768 (15.6%) |
| instructions | 3338 / 32764 (10.2%) |

(Measured with `python3 tools/coverage.py`; the instruction total counts the
ret/rt-delimited bodies the tooling treats as one routine each, not every word
in the ROM.)

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

The call graph is the schedule. `tools/frontier.py` currently reports ~412
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

30% of the instruction total is roughly 8200 routines-worth of code; at the
current 8.6% the project is ~2400 instructions in. Reaching 30% therefore means
translating on the order of 300 more routines, and the ones that are left are
bigger and more entangled than the ones done so far (the easy leaf
functions and hardware pokes are largely gone). Work in batches with
`tools/frontier.py` between them and the number stays honest.
