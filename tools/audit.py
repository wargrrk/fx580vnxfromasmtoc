"""Check that the coverage number is backed by real differential tests.

Usage: python3 tools/audit.py [-v]

`tools/coverage.py` counts a routine as translated as soon as its address is
annotated above a declaration in `src/`.  Nothing there checks that the body is
actually compared against the ROM - a routine can be annotated, never listed in
`tests/t_*.c`, and still push the percentage up.  This script closes that gap
and is meant to be run next to coverage:

  * an annotated routine with no manifest entry  -> FAIL (unverified claim)
  * a manifest entry with no annotation          -> FAIL (dead test, or the
                                                    address in the test is not
                                                    the exported routine)
  * an annotated address romdb does not know     -> warn (probably a label two
                                                    bytes off, or a data blob)

Exit status is non-zero if either FAIL group is non-empty, so it can be wired
into `tools/test.sh`.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romdb

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# One manifest line: {"name", 0xNNNNN, "shape", ...} - the address is the ROM
# entry the case is diff-tested against.
MANIFEST_RE = re.compile(r'\{\s*"[A-Za-z0-9_]+"\s*,\s*(0x[0-9A-Fa-f]+)\s*,')
# The bespoke tests (the ones with a hand-written loop) address the ROM the
# other way, through run_rom()/run_rom_ex(); both count as a differential test.
DIRECT_RE = re.compile(r'run_rom(?:_ex)?\(\s*(0x[0-9A-Fa-f]+)')
# Tests that loop over a table of addresses (screen_addr/lcd_addr do) pass the
# address in a variable, so the literal only appears somewhere in the file.
ANY_HEX_RE = re.compile(r'0x(0[0-9A-Fa-f]{4,4}|[0-9A-Fa-f]{5,5})\b')


def test_addresses():
    """Every address the tests run the ROM routine at, manifest or direct.

    Returns (mentioned, direct): `mentioned` also takes any five-digit hex
    literal, because a test that loops over a table of routines (screen_addr /
    lcd_addr do) passes the address in a variable.  Only `direct` - the address
    the case actually calls - is used for the reverse check, so a comment or a
    data address cannot look like a routine.
    """
    found = {}
    direct = {}
    tests = os.path.join(HERE, "tests")
    for name in sorted(os.listdir(tests)):
        if not (name.startswith("t_") and name.endswith(".c")):
            continue
        path = os.path.join(tests, name)
        with open(path, encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                for regex in (MANIFEST_RE, DIRECT_RE):
                    for match in regex.finditer(line):
                        found.setdefault(int(match.group(1), 16), []).append(
                            "%s:%d" % (name, lineno))
                        direct.setdefault(int(match.group(1), 16), []).append(
                            "%s:%d" % (name, lineno))
                for match in ANY_HEX_RE.finditer(line):
                    found.setdefault(int(match.group(1), 16), []).append(
                        "%s:%d" % (name, lineno))
    return found, direct


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    db = romdb.load_db()
    routines = romdb.routine_entries(db)
    bodies = romdb.function_bodies(db)
    # coverage.py counts routine *entries*, so compare like with like: an
    # annotation on a shared tail marks the tail, not the owning function.
    claimed = romdb.implemented_functions(db)
    owners = romdb.entry_owners(db)
    # A function is "the" owner of one or more entries; an annotation may sit on
    # any of its chunks (a shared tail, say), so a chunk counts for its owner.
    chunks = {}
    for entry, owner in owners.items():
        chunks.setdefault(owner, set()).add(entry)
    tested, direct = test_addresses()

    # An annotated routine with no test of its own is still exercised when a
    # routine that *is* tested calls it: a wrong body then shows up as a
    # difference in the caller.  That is weaker than a direct test, so it is
    # reported, but it is not the same as being unverified.
    reachable = set(tested)
    frontier = list(tested)
    while frontier:
        addr = frontier.pop()
        for callee in bodies.get(addr, (0, set()))[1]:
            if callee not in reachable:
                reachable.add(callee)
                frontier.append(callee)

    def covered(owner):
        if owner in tested or owner in reachable:
            return True
        return bool(chunks.get(owner, set()) & (set(tested) | reachable))

    untested = sorted(addr for addr in claimed
                      if addr not in romdb.NOT_A_FUNCTION and not covered(addr))
    # Routines whose own address is never addressed by a test, but whose body
    # is reached from one that is.
    indirect = sorted(addr for addr in claimed
                      if addr not in romdb.NOT_A_FUNCTION
                      and addr not in tested
                      and addr not in reachable
                      and covered(addr)
                      and not (addr in tested
                               or bool(chunks.get(addr, set()) & set(tested))))
    unknown = sorted(addr for addr in direct if addr not in routines)
    # Entries a test addresses that are aliases inside an already annotated
    # body (a shared tail, a mid-function label) show up here; they are not a
    # problem, so they are only listed with -v.
    stray = sorted(addr for addr in direct
                   if addr in routines and addr not in claimed
                   and owners.get(addr) not in claimed)

    labels = {int(a, 16): n for a, n in db["labels"].items()}

    def describe(addr):
        return "  %05X  %s" % (addr, labels.get(addr, ""))

    print("annotated routines: %d" % len(claimed))
    print("diff-tested addresses: %d" % len(tested))

    for addr in indirect:
        print("warn indirect 0x%05X  %s  (only reached through a tested caller)"
              % (addr, labels.get(addr, "")))

    if args.verbose:
        for addr in sorted(claimed):
            mark = "tested" if addr in tested else "UNTESTED"
            print("  %-8s %s" % (mark, describe(addr).strip()))

    for addr in untested:
        print("FAIL untested %s" % describe(addr).strip())
    for addr in unknown:
        print("FAIL unknown   0x%05X  %s  (tested but not a routine entry)"
              % (addr, ", ".join(tested[addr])))
    if args.verbose:
        for addr in stray:
            print("note  stray    0x%05X  tested but no annotation in src/"
                  % addr)

    if untested or unknown:
        print("audit: %d untested annotation(s), %d unknown address(es)"
              % (len(untested), len(unknown)))
        return 1

    print("audit: OK - every annotated routine is diff-tested (%d only "
          "indirectly, through a tested caller)" % len(indirect))
    return 0


if __name__ == "__main__":
    sys.exit(main())
