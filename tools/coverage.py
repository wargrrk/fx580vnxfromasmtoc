"""Measure how much of the ROM has been reimplemented in C.

Usage: python3 tools/coverage.py [-v]

Two numbers are reported:

  instructions  implemented instruction bytes / all instruction bytes
  functions     implemented routines / routines reachable through `bl`

An implemented routine is one whose entry address appears in the address
annotations of `src/*.c` / `src/*.h` (`/* 0xNNNNN - ... */` style comments,
which the project uses to mark the ROM origin of every C function).  The
routine boundaries come from build/disas.json and are the same heuristic the
rest of the tooling uses: a routine runs from its entry until the first
`rt`/`rti`/unconditional `b` or until the next routine entry, whichever comes
first.
"""
import argparse
import glob
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR_RE = re.compile(r"0x([0-9A-Fa-f]{5})")


def load_db():
    with open(os.path.join(ROOT, "build", "disas.json")) as f:
        return json.load(f)


def implemented(db):
    """Addresses annotated in the C sources, restricted to routine entries."""
    entries = routine_entries(db)
    seen = set()
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "*.c")) +
                       glob.glob(os.path.join(ROOT, "src", "*.h"))):
        with open(path, encoding="utf-8") as f:
            for line in f:
                for token in ADDR_RE.findall(line):
                    addr = int(token, 16)
                    if addr in entries:
                        seen.add(addr)
    return seen


def routine_entries(db):
    """Every address that is a `bl` target or a non-local label."""
    entries = set()
    for insn in db["insns"]:
        if insn["mnemonic"] == "bl":
            for param in insn["params"]:
                if isinstance(param, list) and len(param) == 2:
                    entries.add(param[0])
    for addr, name in db["labels"].items():
        if not name.startswith("."):
            entries.add(int(addr, 16))
    return entries


def body_lengths(db):
    """Map entry -> number of instructions in the routine body."""
    insns = {int(i["addr"], 16): i for i in db["insns"]}
    entries = sorted(routine_entries(db))
    next_entry = {}
    for index, entry in enumerate(entries):
        next_entry[entry] = entries[index + 1] if index + 1 < len(entries) else 1 << 20

    lengths = {}
    for entry in entries:
        count = 0
        cur = entry
        while cur in insns and cur < next_entry[entry] and count < 4000:
            insn = insns[cur]
            count += 1
            mnemonic = insn["mnemonic"]
            if mnemonic in ("rt", "rti", "b", "brk"):
                break
            cur += insn["length"]
        lengths[entry] = count
    return lengths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="list the implemented routines")
    args = parser.parse_args()

    db = load_db()
    lengths = body_lengths(db)
    done = implemented(db)

    total_insns = sum(lengths.values())
    done_insns = sum(lengths[a] for a in done)
    total_funcs = len(lengths)
    done_funcs = len(done)

    print("routines: %d/%d (%.1f%%)" %
          (done_funcs, total_funcs, 100.0 * done_funcs / total_funcs))
    print("instructions: %d/%d (%.1f%%)" %
          (done_insns, total_insns, 100.0 * done_insns / total_insns))

    if args.verbose:
        labels = {int(a, 16): n for a, n in db["labels"].items()}
        for addr in sorted(done):
            print("  %05X  %4d  %s" % (addr, lengths[addr],
                                       labels.get(addr, "")))


if __name__ == "__main__":
    main()
