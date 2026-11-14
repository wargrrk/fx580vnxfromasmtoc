"""Bottom-up decompilation frontier.

Usage: python3 tools/frontier.py [-n N] [-a] [-b]

Lists ROM functions whose *dependencies* are all already implemented in C.  Those
are the routines that can be translated next: their C version can be diff-tested
against the ROM right away because everything they reach already has a C body
with the same behaviour.

"Dependency" is not the same thing as "callee".  A routine also depends on what a
plain `b` jumps to (a tail call into another function) and on the `.l_NNN` label
it may enter through `bl f_XXXX.l_NNN` - a shared tail or an inner loop of a body
nobody has written yet.  Both are resolved to the owning function by
romdb.dependencies(); a frontier built only from `bl` targets that happen to be
function entries advertises 71 routines that cannot be translated at all.

"Implemented" uses the same rule as coverage.py (tools/romdb.py): an address
comment directly above a declaration.  A routine that is only mentioned in
passing therefore does not unlock its callers.  ENTER/LEAVE are excepted because
the frame they build is modelled by the C calling convention.

`-b/--blocked` inverts the report: it lists the routines that are *not* ready
together with the routines that stand in their way, which is what tells you
whether to write a three-instruction leaf or a 300-instruction monster next.
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
    parser.add_argument("-b", "--blocked", action="store_true",
                        help="list the routines that are blocked, and by what")
    args = parser.parse_args()

    db = romdb.load_db()
    bodies = romdb.function_bodies(db)
    deps = romdb.dependencies(db, bodies)
    done = romdb.available_addrs(db) | romdb.implemented_functions(db)
    labels = {int(a, 16): n for a, n in db["labels"].items()}

    def pending_of(entry):
        return sorted(d for d in deps[entry] if d not in done and d in bodies)

    ready = []
    blocked = []
    for entry, (count, _callees) in bodies.items():
        if entry in done:
            continue
        pending = pending_of(entry)
        if pending:
            blocked.append((count, entry, pending))
        elif count >= 2 or args.all:
            ready.append((count, entry, len(deps[entry]), labels.get(entry, "")))

    if args.blocked:
        # cheapest first: the routine that costs least and still unlocks work
        blocked.sort(key=lambda r: (r[0], r[1]))
        unlocks = {}
        for _count, entry, pending in blocked:
            if len(pending) == 1:
                unlocks[pending[0]] = unlocks.get(pending[0], 0) + 1
        print("%d routines are blocked" % len(blocked))
        for count, entry, pending in blocked[:args.n]:
            print("  %05X  %4d insns  blocked by %s" % (
                entry, count,
                ",".join("%05X" % p for p in pending)))
        print("\nthe same list seen from the other side - owner -> unlocks:")
        for owner, hits in sorted(unlocks.items(), key=lambda kv: -kv[1])[:args.n]:
            count = bodies[owner][0] if owner in bodies else 0
            print("  %05X  %4d insns  unblocks %d  %s" % (
                owner, count, hits, labels.get(owner, "")))
        return

    ready.sort(key=lambda r: (-r[2], -r[0]))
    print("%d routines are ready to translate" % len(ready))
    for count, entry, ndep, name in ready[:args.n]:
        print("  %05X  %4d insns  %2d deps  %s" % (entry, count, ndep, name))


if __name__ == "__main__":
    main()
