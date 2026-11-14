"""Measure how much of the ROM has been reimplemented in C.

Usage: python3 tools/coverage.py [-v]

Two numbers are reported:

  instructions  implemented instruction bytes / all instruction bytes
  functions     implemented routines / routines reachable through `bl`

An implemented routine is one whose entry address appears in a comment block
sitting *directly above a declaration* in `src/*.c` / `src/*.h` (see
tools/romdb.py for the exact rule).  Merely mentioning an address somewhere
else - a TODO note, a prose range, a data argument - does not count, so the
percentage cannot be inflated without writing a function.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romdb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="list the implemented routines")
    args = parser.parse_args()

    db = romdb.load_db()
    bodies = romdb.routine_bodies(db)
    done = romdb.implemented_addrs(db)

    total_insns = sum(count for count, _ in bodies.values())
    done_insns = sum(bodies[addr][0] for addr in done)
    total_funcs = len(bodies)
    done_funcs = len(done)

    print("routines: %d/%d (%.1f%%)" %
          (done_funcs, total_funcs, 100.0 * done_funcs / total_funcs))
    print("instructions: %d/%d (%.1f%%)" %
          (done_insns, total_insns, 100.0 * done_insns / total_insns))

    if args.verbose:
        labels = {int(a, 16): n for a, n in db["labels"].items()}
        for addr in sorted(done):
            print("  %05X  %4d  %s" % (addr, bodies[addr][0],
                                       labels.get(addr, "")))


if __name__ == "__main__":
    main()
