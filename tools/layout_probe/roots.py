#!/usr/bin/env python3
"""List the save-chunk root types of the pinned decomp.

Parses src/save_arrays.c (gSaveChunkHeaders and the DECL_CHUNK_EX entries) for
the size functions, then finds each function's `return sizeof(TYPE) [* N];`
body. These are the types whose layout defines the save format
(src/save.c: GetSaveChunkSizePlusCRC calls hdr[idx].sizeFunc()).

Usage: roots.py DECOMP_DIR      prints "sizeFunc<TAB>type<TAB>source file"
"""
import glob
import re
import sys
from pathlib import Path


def size_functions(decomp):
    text = (Path(decomp) / "src/save_arrays.c").read_text(errors="replace")
    text = re.sub(r"#define(?:[^\n]*\\\n)*[^\n]*", "", text)  # drop macro definitions (their parameters look like names)
    names = re.findall(r"\(SAVESIZEFN\)\s*(\w+)", text)
    names += re.findall(r"DECL_CHUNK_EX\(\s*(\w+)\s*,", text)
    seen, out = set(), []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def find_roots(decomp):
    sources = {f: Path(f).read_text(errors="replace")
               for f in glob.glob(str(Path(decomp) / "src/**/*.c"), recursive=True)}
    roots = []
    for name in size_functions(decomp):
        for path, text in sources.items():
            m = re.search(r"\b(?:u32|int|size_t)\s+" + re.escape(name) + r"\s*\(\s*(?:void)?\s*\)\s*\{(.*?)\n\}", text, re.S)
            if not m:
                continue
            body = re.sub(r"\s+", " ", m.group(1)).strip()
            t = re.fullmatch(r"return sizeof\(\s*((?:struct\s+)?\w+)\s*\)(?:\s*\*\s*\w+)?;", body)
            if not t:
                sys.exit(f"{name}: unexpected body {body!r} in {path}; teach roots.py about it")
            roots.append((name, t.group(1), str(Path(path).relative_to(decomp))))
            break
        else:
            sys.exit(f"size function {name} not found under src/")
    return roots


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for r in find_roots(sys.argv[1]):
        print(*r, sep="\t")
