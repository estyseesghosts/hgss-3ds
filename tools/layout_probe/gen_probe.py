#!/usr/bin/env python3
"""Generate the struct-layout probe C file for the save-block types.

Two steps, because the member lists come from the compiler, not from a C parser:

  1. `gen_probe.py types DECOMP OUT.c`
       writes a translation unit that includes the headers of every save-chunk
       source file and declares one extern pointer per root type
       (roots.py). Compile it with `arm-none-eabi-gcc -g -c`.
  2. `gen_probe.py probe TYPES.o OUT.c OUT.names`
       walks the DWARF of TYPES.o from the roots, recursively through members
       (arrays of structs and nested structs/unions, not pointers), and writes
         const unsigned int probe[] = { sizeof(T), offsetof(T, m), ... };
       plus OUT.names (one "T" / "T.m" label per array element).
       Bit-fields are skipped (offsetof is illegal); members of anonymous
       aggregates are skipped, but the aggregate's size still shows up in its
       parent's size and in the offsets of the named members after it.

The same probe.c is compiled by MWCC (Wine) and GCC; compare.py diffs the
arrays. Needs pyelftools. Uses only the decomp's headers: no game data.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import roots as roots_mod  # noqa: E402


def collect_includes(decomp, root_rows):
    incs = []
    for _, _, rel in root_rows:
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', (Path(decomp) / rel).read_text(errors="replace"), re.M):
            if m.group(1) not in incs:
                incs.append(m.group(1))
    return incs


def cmd_types(decomp, out):
    rows = roots_mod.find_roots(decomp)
    lines = ['#include "global.h"']
    lines += [f'#include "{i}"' for i in collect_includes(decomp, rows) if i != "global.h"]
    for n, (_, typ, _) in enumerate(rows):
        lines.append(f"extern {typ} *probe_root_{n};")
    # keep roots visible in DWARF even though nothing defines them
    Path(out).write_text("\n".join(lines) + "\n")
    Path(out).with_suffix(".roots").write_text("\n".join(t for _, t, _ in rows) + "\n")


def strip(die, dies):
    """Follow typedef/const/volatile to the underlying type DIE, remembering the first typedef name."""
    name = None
    while die is not None and die.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
        if die.tag == "DW_TAG_typedef" and name is None:
            name = die.attributes["DW_AT_name"].value.decode()
        die = type_of(die, dies)
    return die, name


def type_of(die, dies):
    a = die.attributes.get("DW_AT_type")
    return dies[a.value + die.cu.cu_offset] if a else None


def elem_type(die, dies):
    """For arrays, the innermost element type (arrays of structs embed the struct)."""
    while die is not None and die.tag == "DW_TAG_array_type":
        die = type_of(die, dies)
        die, _ = strip(die, dies) if die is not None else (None, None)
    return die


def cmd_probe(obj, out, names_out):
    from elftools.elf.elffile import ELFFile

    f = ELFFile(open(obj, "rb"))
    di = f.get_dwarf_info()
    dies = {}
    top = []
    for cu in di.iter_CUs():
        for die in cu.iter_DIEs():
            dies[die.offset] = die
        top += list(cu.get_top_DIE().iter_children())

    by_typedef, by_tag = {}, {}
    for d in top:
        n = d.attributes.get("DW_AT_name")
        if not n:
            continue
        n = n.value.decode()
        if d.tag == "DW_TAG_typedef":
            by_typedef[n] = d
        elif d.tag in ("DW_TAG_structure_type", "DW_TAG_union_type") and "DW_AT_declaration" not in d.attributes:
            by_tag[("struct " if d.tag == "DW_TAG_structure_type" else "union ") + n] = d

    spelled = {}
    entries = []  # (label, c_expr)

    def visit(die, spelling):
        if die.offset in spelled:
            return
        spelled[die.offset] = spelling
        entries.append((spelling, f"sizeof({spelling})"))
        for m in die.iter_children():
            if m.tag != "DW_TAG_member":
                continue
            name = m.attributes.get("DW_AT_name")
            mt, td = strip(type_of(m, dies), dies)
            if name and "DW_AT_bit_size" not in m.attributes and "DW_AT_data_bit_offset" not in m.attributes:
                n = name.value.decode()
                entries.append((f"{spelling}.{n}", f"offsetof({spelling}, {n})"))
            et = elem_type(mt, dies) if mt is not None else None
            if et is None:
                continue
            et, td2 = strip(et, dies)
            if et is None or et.tag not in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                continue
            tag = et.attributes.get("DW_AT_name")
            if td or td2:
                sp = td or td2
            elif tag:
                sp = ("struct " if et.tag == "DW_TAG_structure_type" else "union ") + tag.value.decode()
            else:
                continue  # anonymous aggregate: cannot be spelled
            if "DW_AT_declaration" in et.attributes:
                continue
            visit(et, sp)

    for root in Path(obj).with_suffix(".roots").read_text().split("\n"):
        if not root:
            continue
        die = by_typedef.get(root)
        if die is not None:
            tgt, _ = strip(die, dies)
        else:
            tgt = by_tag.get(root)
        if tgt is None or tgt.tag not in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            sys.exit(f"root type {root!r} is not a struct/union in {obj}")
        visit(tgt, root)

    hdr = Path(obj).with_suffix(".c")
    src = hdr.read_text() if hdr.exists() else ""
    includes = [l for l in src.splitlines() if l.startswith("#include")]
    body = ",\n    ".join(e for _, e in entries)
    Path(out).write_text("#include <stddef.h>\n" + "\n".join(includes) + "\n\n"
                         "const unsigned int probe[] = {\n    " + body + "\n};\n")
    Path(names_out).write_text("\n".join(l for l, _ in entries) + "\n")
    print(f"{len(entries)} probe values")


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "types":
        cmd_types(a[1], a[2])
    elif len(a) == 4 and a[0] == "probe":
        cmd_probe(a[1], a[2], a[3])
    else:
        sys.exit(__doc__)
