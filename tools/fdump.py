"""Dump one or more routine bodies in the annotated listing format.

Usage: python3 tools/fdump.py ADDR [ADDR ...]

Each routine is printed from its entry until the first `rt`/`rti` (or a
maximum of 200 instructions), with branch targets resolved and labels shown.
Mostly a batch version of tools/show.py, used while translating a cluster of
routines.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASM = os.path.join(ROOT, "resource", "disas.asm")
JSON = os.path.join(ROOT, "build", "disas.json")

_ADDR_RE = re.compile(r";\s*([0-9A-F]{4,5})\s*\|")


def main():
    db = json.load(open(JSON))
    by_addr = {int(i["addr"], 16): i for i in db["insns"]}
    labels = db["labels"]

    text = {}
    with open(ASM) as f:
        for line in f:
            match = _ADDR_RE.search(line)
            if match:
                text[int(match.group(1), 16)] = line.split(";")[0].strip()

    for arg in sys.argv[1:]:
        addr = int(arg, 0)
        print("==== %05X  %s" % (addr, labels.get("%05X" % addr, "-")))
        cur = addr
        for _ in range(200):
            if cur not in by_addr:
                break
            ins = by_addr[cur]
            line = "%05X  %-30s" % (cur, text.get(cur, ins["mnemonic"]))
            for param in ins["params"]:
                if isinstance(param, list) and len(param) == 2 and param[1] == "lab":
                    line += " @%05X" % param[0]
            name = labels.get("%05X" % cur)
            if name:
                line += "   ; " + name
            print(line)
            if ins["mnemonic"] in ("rt", "rti", "brk"):
                break
            cur += ins["length"]
        print()


if __name__ == "__main__":
    main()
