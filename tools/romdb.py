"""romdb.py - shared view of the ROM's routine layout.

Both `coverage.py` and `frontier.py` need the same three things: the routine
entries, the body of each routine (length and callees) and the set of routines
that have a C body.  They used to keep private copies of that logic; this
module is the single source so the two tools cannot drift apart.

A routine counts as implemented only when an address comment sits *directly
above a declaration*, i.e.

    /* 0x2B334 - expand a glyph-mask prefix ... */
    uint16_t expand_glyph_mask(uint8_t *ds, uint16_t src, uint16_t out,
                               uint16_t length)

Multi-line signatures are fine: the comment just has to be the comment block
immediately preceding the declaration.  A bare mention anywhere else - a prose
note, a TODO, or a data argument like `subtract_words(rom, 0x20000, ...)` - is
not evidence that the routine has a C body.
"""
import glob
import json
import os
import re

ADDR_RE = re.compile(r"0x([0-9A-Fa-f]{5})")

# A declaration starts at column 0 with a type and reaches the first `(`.
DECL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*?\**\s*[A-Za-z_][A-Za-z0-9_]*\s*\(")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Frame plumbing that never gets its own C body: ENTER (0x0AC30) saves the
# callee-saved registers and points er14 at the frame, LEAVE (0x0AC38)
# restores them.  Every C translation models the frame with plain C locals, so
# these two are covered by the calling convention rather than by a function.
# They are listed explicitly so a caller is not blocked by a callee that can
# never appear in src/.  They are *not* counted as translated routines.
STRUCTURAL_ENTRIES = frozenset({0x0AC30, 0x0AC38})


def load_db(root=ROOT):
    with open(os.path.join(root, "build", "disas.json")) as f:
        return json.load(f)


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


def routine_bodies(db):
    """entry -> (instruction count, set of callees).

    A body runs from its entry until the first `rt`/`rti`/unconditional `b`
    or the next routine entry, whichever comes first.
    """
    insns = {int(i["addr"], 16): i for i in db["insns"]}
    ordered = sorted(routine_entries(db))
    limit = {}
    for index, entry in enumerate(ordered):
        limit[entry] = ordered[index + 1] if index + 1 < len(ordered) else 1 << 20

    out = {}
    for entry in ordered:
        count = 0
        cur = entry
        callees = set()
        while cur in insns and cur < limit[entry] and count < 4000:
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
    return out


def _comment_blocks(path):
    """Yield (addresses, following_code_line) for each comment block."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()

        if stripped.startswith("/*"):
            block = []
            while index < len(lines):
                block.append(lines[index])
                if "*/" in lines[index]:
                    index += 1
                    break
                index += 1
            yield block, _next_code_line(lines, index)
            continue

        if stripped.startswith("//"):
            block = []
            while index < len(lines) and lines[index].strip().startswith("//"):
                block.append(lines[index])
                index += 1
            yield block, _next_code_line(lines, index)
            continue

        index += 1


def _next_code_line(lines, index):
    """First line after `index` that is not blank and not a comment."""
    while index < len(lines):
        stripped = lines[index].strip()
        if stripped == "" or stripped.startswith("/*") or stripped.startswith("*"):
            # A further comment block cancels adjacency.
            if stripped.startswith("/*") or stripped.startswith("*"):
                return ""
            index += 1
            continue
        return lines[index]
    return ""


def implemented_addrs(db, root=ROOT):
    """Addresses with a C declaration directly under an address comment."""
    entries = routine_entries(db)
    seen = set()
    paths = sorted(glob.glob(os.path.join(root, "src", "*.c")) +
                   glob.glob(os.path.join(root, "src", "*.h")))
    for path in paths:
        for block, code in _comment_blocks(path):
            if not code or not DECL_RE.match(code):
                continue
            for line in block:
                for token in ADDR_RE.findall(line):
                    addr = int(token, 16)
                    if addr in entries:
                        seen.add(addr)
    return seen


def available_addrs(db, root=ROOT):
    """Everything a caller may rely on: translated bodies plus frame plumbing."""
    return implemented_addrs(db, root) | STRUCTURAL_ENTRIES
