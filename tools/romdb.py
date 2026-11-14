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
STRUCTURAL_ENTRIES = frozenset({0x0AC30, 0x0AC38, 0x2110A})

# Entries that are not functions at all and can never have a C body of their
# own: the frame plumbing above (which every translation already models), the
# reset vector, and the interrupt return stub.  Keeping them in the denominator
# would cap coverage below 100% for ever.
NOT_A_FUNCTION = frozenset({0x00004, 0x0A1BC, 0x0AC30, 0x0AC38, 0x2110A})


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

    A chunk runs from its entry until the first `rt`/`rti`/unconditional `b`
    or the next routine entry, whichever comes first.  Chunks are not functions:
    the compiler emits `.l_NNN` labels inside a function body for its internal
    branches, and each of those is an entry too.  Use function_bodies() to get
    the whole function.
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


def _entry_names(db):
    return {int(addr, 16): name for addr, name in db["labels"].items()}


def entry_owners(db):
    """entry -> the function entry it belongs to.

    A label that does not start with `.` starts a function; the `.l_NNN`
    labels that follow are internal chunks of it.  This is the rule that makes
    a function's callees complete: 0x0C362 looks self-contained until you see
    that its `.l_028` chunk calls 0x24F26.
    """
    names = _entry_names(db)
    owners = {}
    current = None
    for entry in sorted(routine_entries(db)):
        if not names.get(entry, "").startswith("."):
            current = entry
        if current is None:
            current = entry
        owners[entry] = current
    return owners


def _successors(insn, addr, next_addr):
    """Addresses control can reach from `addr`: a call returns, so it falls
    through; `b` does not; a conditional branch does both."""
    mnemonic = insn["mnemonic"]
    if mnemonic in ("rt", "rti", "brk"):
        return []
    target = None
    for param in insn["params"]:
        if isinstance(param, list) and len(param) == 2 and param[1] == "lab":
            target = param[0]
    if mnemonic == "b":
        return [target] if target is not None else []
    if mnemonic == "bl" or target is None:
        return [next_addr]
    return [target, next_addr]


def function_bodies(db):
    """function entry -> (instruction count, callees) over its whole body.

    A function owns everything from its entry up to the next function entry.
    Inside that span only the instructions control can actually reach count,
    which keeps the data regions (which disassemble as instructions too) out
    of the total, and a small control-flow walk follows branches that jump
    *over* an unconditional `b` into a later block - exactly the shape that hid
    0x24F26 behind 0x0C362's `b LEAVE`.
    """
    insns = {int(i["addr"], 16): i for i in db["insns"]}
    everything = sorted(set(entry_owners(db).values()))
    owners = [a for a in everything if a not in NOT_A_FUNCTION]

    out = {}
    for index, owner in enumerate(owners):
        # Boundaries come from the full list, so a stub in between belongs to
        # nobody instead of being charged to the function before it.
        end = everything[everything.index(owner) + 1] \
            if everything.index(owner) + 1 < len(everything) else 1 << 20
        seen = set()
        stack = [owner]
        callees = set()
        while stack:
            addr = stack.pop()
            if addr in seen or addr < owner or addr >= end or addr not in insns:
                continue
            seen.add(addr)
            insn = insns[addr]
            if insn["mnemonic"] == "bl":
                for param in insn["params"]:
                    if isinstance(param, list) and len(param) == 2:
                        callees.add(param[0])
            stack.extend(_successors(insn, addr, addr + insn["length"]))
        callees.discard(owner)
        out[owner] = (len(seen), callees)
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


def implemented_functions(db, root=ROOT):
    """Function entries that have a C body (an annotation on any of its chunks)."""
    owners = entry_owners(db)
    return {owners[addr] for addr in implemented_addrs(db, root) if addr in owners}
