"""Bottom-up decompilation frontier.

Usage: python3 tools/frontier.py [-n N] [-a]

Lists ROM routines whose *callees* are all already implemented in C.  Those are
the routines that can be translated next: their C version can be diff-tested
against the ROM right away because everything they call already has a C body
with the same behaviour.
"""
import argparse
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR_RE = re.compile(r"0x([0-9A-Fa-f]{5})")


def load_db():
    with open(os.path.join(ROOT, "build", "disas.json")) as f:
        return json.load(f)


def implemented(db, entries):
    seen = set()
    for name in sorted(os.listdir(os.path.join(ROOT, "src"))):
        if not name.endswith((".c", ".h")):
            continue
        with open(os.path.join(ROOT, "src", name), encoding="utf-8") as f:
            for line in f:
                for token in ADDR_RE.findall(line):
                    addr = int(token, 16)
                    if addr in entries:
                        seen.add(addr)
    return seen


def bodies(db):
    """entry -> (instruction count, callees, has_unresolved_calls)"""
    insns = {int(i["addr"], 16): i for i in db["insns"]}
    entries = set()
    for insn in db["insns"]:
        if insn["mnemonic"] == "bl":
            for param in insn["params"]:
                if isinstance(param, list) and len(param) == 2:
                    entries.add(param[0])
    for addr, label in db["labels"].items():
        if not label.startswith("."):
            entries.add(int(addr, 16))
    ordered = sorted(entries)
    limits = {}
    for index, entry in enumerate(ordered):
        limits[entry] = ordered[index + 1] if index + 1 < len(ordered) else 1 << 20

    out = {}
    for entry in ordered:
        count = 0
        cur = entry
        callees = set()
        while cur in insns and cur < limits[entry] and count < 4000:
            insn = insns[cur]
            count += 1
            if insn["mnemonic"] == "bl":
                for param in insn["params"]:
                    if isinstance(param, list) and len(param) == 2:
                        callees.add(param[0])
            if insn["mnemonic"] in ("rt", "rti", "b", "brk"):
                break
            cur += insn["length"]
        out[entry] = (count, callees)
    return out, entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", type=int, default=30)
    parser.add_argument("-a", "--all", action="store_true",
                        help="include one-instruction stubs")
    args = parser.parse_args()

    db = load_db()
    bodies_map, entries = bodies(db)
    done = implemented(db, entries)
    labels = {int(a, 16): n for a, n in db["labels"].items()}

    ready = []
    for entry, (count, callees) in bodies_map.items():
        if entry in done:
            continue
        if not callees:
            if count < 2 and not args.all:
                continue
            ready.append((count, entry, 0, labels.get(entry, "")))
            continue
        pending = [c for c in callees if c not in done and c in bodies_map]
        if not pending:
            ready.append((count, entry, len(callees), labels.get(entry, "")))

    ready.sort(key=lambda r: (-r[2], -r[0]))
    print("%d routines are ready to translate" % len(ready))
    for count, entry, ncallee, name in ready[:args.n]:
        print("  %05X  %4d insns  %2d callees  %s" % (entry, count, ncallee, name))


if __name__ == "__main__":
    main()
