"""Bottom-up decompilation frontier.

Usage: python3 tools/frontier.py [-n N] [-a]

Lists ROM routines whose *callees* are all already implemented in C.  Those are
the routines that can be translated next: their C version can be diff-tested
against the ROM right away because everything they call already has a C body
with the same behaviour.

"Callee is implemented" uses the same rule as coverage.py (tools/romdb.py):
an address comment directly above a declaration.  A routine that is only
mentioned in passing therefore does not unlock its callers.  ENTER/LEAVE are
excepted because the frame they build is modelled by the C calling convention.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romdb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", type=int, default=30)
    parser.add_argument("-a", "--all", action="store_true",
                        help="include one-instruction stubs")
    args = parser.parse_args()

    db = romdb.load_db()
    bodies = romdb.routine_bodies(db)
    done = romdb.available_addrs(db)
    labels = {int(a, 16): n for a, n in db["labels"].items()}

    ready = []
    for entry, (count, callees) in bodies.items():
        if entry in done:
            continue
        if not callees:
            if count < 2 and not args.all:
                continue
            ready.append((count, entry, 0, labels.get(entry, "")))
            continue
        pending = [c for c in callees if c not in done and c in bodies]
        if not pending:
            ready.append((count, entry, len(callees), labels.get(entry, "")))

    ready.sort(key=lambda r: (-r[2], -r[0]))
    print("%d routines are ready to translate" % len(ready))
    for count, entry, ncallee, name in ready[:args.n]:
        print("  %05X  %4d insns  %2d callees  %s" % (entry, count, ncallee, name))


if __name__ == "__main__":
    main()
