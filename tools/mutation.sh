#!/bin/sh
# mutation.sh - prove the differential harness actually catches bugs.
#
# A green suite means nothing on its own: it could be green because the checks
# are too weak.  This script plants one deliberate bug at a time - the exact
# bugs this project has already made once - rebuilds, and insists that the run
# fails.  Any mutation that survives is a hole in the harness, not a win.
#
#   ./tools/mutation.sh          - run every mutation
#   ./tools/mutation.sh -v       - also show the failing lines
set -u
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -O2"
SRC="src/string.c src/mem.c src/display.c src/font.c src/print.c src/keyboard.c
     src/edit.c src/diagnostic.c src/hw.c src/draw.c src/arith.c"
TESTS="tests/main.c tests/harness.c tests/batch.c tests/t_string.c tests/t_mem.c
       tests/t_display.c tests/t_font.c tests/t_print.c tests/t_keyboard.c
       tests/t_edit.c tests/t_diagnostic.c tests/t_hw.c tests/t_draw.c
       tests/t_record.c tests/t_arith.c tests/t_emu.c"

BKDIR=$(mktemp -d)
trap 'rm -rf "$BKDIR"; rm -f build/mutant' EXIT
mkdir -p build

# mutate <name> <file> <perl-substitution>
mutate() {
    name=$1
    file=$2
    expr=$3

    cp "$file" "$BKDIR/backup"
    perl -0pi -e "$expr" "$file"
    if cmp -s "$file" "$BKDIR/backup"; then
        echo "BROKEN  $name - the mutation did not apply (pattern gone?)"
        cp "$BKDIR/backup" "$file"
        return 1
    fi

    if ! $CC $CFLAGS -o build/mutant $TESTS $SRC emu/nx8.c 2>"$BKDIR/cc.log"; then
        echo "BUILDFAIL $name"
        sed -n 1,5p "$BKDIR/cc.log"
        cp "$BKDIR/backup" "$file"
        return 1
    fi

    if ./build/mutant resource/rom.bin >"$BKDIR/run.log" 2>&1; then
        echo "SURVIVED $name - harness missed it"
        result=1
    else
        n=$(grep -c '^FAIL' "$BKDIR/run.log")
        echo "caught   $name ($n failures)"
        if [ "${1:-}" = "-v" ]; then
            grep '^FAIL' "$BKDIR/run.log" | head -3
        fi
        result=0
    fi

    cp "$BKDIR/backup" "$file"
    return $result
}

fails=0
run() {
    mutate "$@" || fails=$((fails + 1))
}

# 1. A store outside RAM/SFR/LCD must be dropped by the chip, not written.
mutate_hint="drops the writable-map guard"
run "$mutate_hint" src/print.c \
    's/if \(!ds_writable\(address\)\)\n        return;/if (0)\n        return;/'

# 2. memset counts: the loop must run exactly n times.
run "memset_n off-by-one" src/mem.c \
    's/(uint16_t memset_n\(uint8_t \*ds, uint16_t dst, uint8_t value, uint16_t n\)\n\{\n    uint16_t d = dst;\n    while )\(n > 0\)/$1(n-- > 1)/'

# 3. The interrupt-enable mask: 253 clears bit 1, not bit 2.
run "lcd_timer_poke wrong bit" src/hw.c \
    's/ds\[0xF014\] &= \(uint8_t\)253u;/ds[0xF014] \&= (uint8_t)~0x04;/'

# 4. A widened delta: |dx| runs to 255, so it must not be a signed byte.
run "draw_line signed-byte delta" src/draw.c \
    's/int16_t dx = \(int16_t\)\(\(uint16_t\)\(\(uint16_t\)\(uint8_t\)end - \(uint16_t\)x0\)\);/int16_t dx = (int16_t)(int8_t)((uint8_t)end - x0);/'

# 5. Overlapping copies need the four phases kept apart: interleaving the read
#    and the write of each byte breaks every pair of records that overlaps.
run "record_pair_copy interleaved copy" src/mem.c \
    's/    for \(int i = 0; i < 10; i\+\+\)\n        half\[i\] = ds\[\(uint16_t\)\(s \+ 10 \+ i\)\];[\s\S]{0,220}?for \(int i = 9; i >= 2; i--\)\n        ds_put\(ds, \(uint16_t\)\(d \+ 10 \+ i\), half\[i\]\);/    for (int i = 0; i < 10; i++)\n        ds_put(ds, (uint16_t)(d + 10 + i), ds[(uint16_t)(s + 10 + i)]);/'

# 6. A return value is part of the behaviour: er0 is compared every round.
run "memset_n wrong return" src/mem.c \
    's/(uint16_t memset_n\(uint8_t \*ds, uint16_t dst, uint8_t value, uint16_t n\)[\s\S]{0,200}?return )dst;/$1(uint16_t)(dst + 1);/'

# 7. One SFR write dropped entirely.
run "hw.c missing SFR poke" src/hw.c \
    's/    ds\[0xF008\] = 160;/    (void)0;/'

# 8. An SFR poke with the wrong value.
run "keyboard.c wrong SFR value" src/keyboard.c \
    's/    ds\[0xF045\] = 255;/    ds[0xF045] = 254;/'

echo
if [ "$fails" -eq 0 ]; then
    echo "mutation self-test: OK - every planted bug was caught"
    exit 0
fi
echo "mutation self-test: FAILED - $fails mutation(s) survived or broke"
exit 1
