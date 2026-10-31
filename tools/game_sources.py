#!/usr/bin/env python3
"""Print the HGSS game sources as the original build lists them.

Usage:
    python tools/game_sources.py --root build/upstream --game-sources
    python tools/game_sources.py --root build/upstream --c-sources
    python tools/game_sources.py --root build/upstream --cpp-sources
    python tools/game_sources.py --root build/upstream --asm-sources
    python tools/game_sources.py --root build/upstream --lib-sources
    python tools/game_sources.py --root build/upstream --port-lib-sources
    python tools/game_sources.py --root build/upstream --overlay-map
    python tools/game_sources.py --root build/upstream --json

HGSS builds with make, not meson: common.mk derives the source lists from
the checkout with wildcards (src/**/*.c, src/**/*.cpp, asm/*.s, and one
*.c/*.cpp wildcard per lib source directory), and main.lsf assigns every
linked object to Static main, an Autoload section, or one of the 129
overlays. This tool reads those same files in --root (default
build/upstream) so the port compiles exactly what the ROM links:

* --c-sources / --cpp-sources / --game-sources: every game .c / .cpp file
  under src/ (common.mk C_SRCS / CXX_SRCS).
* --asm-sources: the .s data/assembly files (common.mk ASM_SRCS plus
  LIB_ASM_SRCS): asm/*.s, lib/asm/*.s, lib/NitroSDK/asm/*.s,
  lib/MSL_C/asm/*.s, lib/NitroDWC/asm/*.s.
* --lib-sources: the lib/ C/C++ sources the ROM links (common.mk
  LIB_SRC_SUBDIR plus lib/dsprot/src and lib/syscall/src, which build
  separately: dsprot through its overlay, syscall as -lsyscall).
* --port-lib-sources: --lib-sources minus what the port does not compile
  from HGSS sources: the SDK files tools/hgss_nitro.py lists as
  port-replaced, the stubbed Wi-Fi/DWC/VCT/CPS/dsprot libraries, and the
  MSL_C files newlib replaces.
* --overlay-map: for every object in main.lsf (Static main, Autoload ITCM,
  Autoload DTCM, each Overlay), its source file in the checkout (null for
  build-generated objects such as lib/dsprot's *_encrypted.o) and the
  section/overlay it belongs to.
* --json: the same information as JSON, for the Makefile.

With no output flag, prints --game-sources (one path per line, sorted).
Each --*-sources flag prints its bare list; --overlay-map prints the
mapping as text. --json overrides the text formats.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_overlays as go  # noqa: E402

try:
    import hgss_nitro as nitro  # noqa: E402
except ImportError:  # keep the pure listing usable without the shim
    nitro = None  # type: ignore[assignment]

# Libraries the port stubs instead of compiling (game_overlays' dsprot note
# and Phase 2 step 7): no source under these roots is in --port-lib-sources.
STUBBED_LIB_ROOTS = ("lib/NitroDWC", "lib/NitroWiFi", "lib/libCPS", "lib/libVCT",
                     "lib/dsprot")


def _parse_make_vars(path: Path) -> dict[str, str]:
    """Makefile variable assignments in one file (handles backslash
    continuations)."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"\\\n", " ", text)
    variables: dict[str, str] = {}
    for line in text.splitlines():
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*[:+]?=\s*(.*)$", line.strip())
        if m:
            variables[m.group(1)] = m.group(2).strip()
    return variables


def check_root(root: Path) -> dict[str, str]:
    """Read the build files; fail clearly outside an HGSS checkout.

    Returns common.mk's variables. filesystem.mk holds the NitroFS data
    file list (NITROFS_FILES): data, not compiled sources, so it only
    validates that the checkout is complete.
    """
    for name in ("Makefile", "common.mk", "filesystem.mk", "main.lsf"):
        if not (root / name).is_file():
            raise SystemExit(f"{root}: {name} not found (not an HGSS checkout?)")
    if "common.mk" not in (root / "Makefile").read_text(encoding="utf-8"):
        raise SystemExit(f"{root}/Makefile: does not include common.mk")
    if "NITROFS_FILES" not in (root / "filesystem.mk").read_text(encoding="utf-8"):
        raise SystemExit(f"{root}/filesystem.mk: no NITROFS_FILES")
    variables = _parse_make_vars(root / "common.mk")
    for var in ("SRC_SUBDIR", "ASM_SUBDIR", "LIB_SUBDIRS"):
        if var not in variables:
            raise SystemExit(f"{root}/common.mk: no {var} (layout changed?)")
    return variables


def _existing_dirs(root: Path, names: list[str]) -> list[str]:
    return [n for n in names if (root / n).is_dir()]


def lib_src_dirs(root: Path, variables: dict[str, str]) -> list[str]:
    """lib/ source roots, derived the way common.mk builds LIB_SRC_SUBDIR,
    plus the separately-built dsprot and syscall libraries the ROM links."""
    subs = variables.get("LIB_SUBDIRS", "").split()
    dirs = ["lib/src"] + [f"lib/{s}/src" for s in subs]
    dirs += [f"lib/NitroSDK/src/{s}" for s in variables.get("NITROSDK_SRC_SUBDIRS", "").split()]
    dirs += ["lib/dsprot/src", "lib/syscall/src"]
    seen = set()
    ordered = [d for d in dirs if not (d in seen or seen.add(d))]
    return _existing_dirs(root, ordered)


def lib_asm_dirs(root: Path, variables: dict[str, str]) -> list[str]:
    """lib/ assembly roots, as common.mk builds LIB_ASM_SUBDIR."""
    subs = variables.get("LIB_SUBDIRS", "").split()
    return _existing_dirs(root, ["lib/asm"] + [f"lib/{s}/asm" for s in subs])


def _glob(root: Path, directory: str, patterns: tuple[str, ...], recursive: bool) -> list[str]:
    found = set()
    base = root / directory
    for pattern in patterns:
        globbed = base.rglob(pattern) if recursive else base.glob(pattern)
        for p in globbed:
            if p.is_file():
                found.add(p.relative_to(root).as_posix())
    return sorted(found)


def game_c_sources(root: Path) -> list[str]:
    """Every game .c file under src/ (common.mk C_SRCS)."""
    return _glob(root, "src", ("*.c",), recursive=True)


def game_cpp_sources(root: Path) -> list[str]:
    """Every game .cpp file under src/ (common.mk CXX_SRCS)."""
    return _glob(root, "src", ("*.cpp",), recursive=True)


def game_sources(root: Path) -> list[str]:
    """Every game .c and .cpp file under src/."""
    return sorted(game_c_sources(root) + game_cpp_sources(root))


def asm_sources(root: Path, variables: dict[str, str] | None = None) -> list[str]:
    """The .s data/assembly files (common.mk ASM_SRCS + LIB_ASM_SRCS)."""
    if variables is None:
        variables = _parse_make_vars(root / "common.mk")
    found = _glob(root, variables.get("ASM_SUBDIR", "asm"), ("*.s",), recursive=False)
    for directory in lib_asm_dirs(root, variables):
        found += _glob(root, directory, ("*.s",), recursive=False)
    return sorted(set(found))


def lib_sources(root: Path, variables: dict[str, str] | None = None) -> list[str]:
    """The lib/ C/C++ sources the ROM links."""
    if variables is None:
        variables = _parse_make_vars(root / "common.mk")
    found: list[str] = []
    for directory in lib_src_dirs(root, variables):
        found += _glob(root, directory, ("*.c", "*.cpp"), recursive=True)
    return sorted(set(found))


def _nitro_excluded(rel: str) -> bool:
    """A lib/NitroSDK/src-relative path the port replaces (hgss_nitro.py)."""
    if nitro is None:
        return False
    if rel in nitro.EXCLUDE_FILES:
        return True
    return rel.startswith(nitro.EXCLUDE_PREFIXES)


def port_lib_sources(root: Path, variables: dict[str, str] | None = None) -> list[str]:
    """--lib-sources minus port-replaced SDK files, stubbed libraries
    (Wi-Fi/DWC/VCT/CPS/dsprot), newlib-replaced MSL_C and the DS secure-area
    images."""
    kept = []
    for source in lib_sources(root, variables):
        if source.startswith("lib/MSL_C/"):
            continue
        # lib/syscall/Makefile compiles exactly one _secure_$(GAME_CODE).c per
        # game: an embedded MWCC asm image of the DS secure area, with SVC
        # aliases the matching build adds afterwards. Nothing in the game or
        # SDK references it, GCC cannot parse it, and emitting the firmware
        # bytes into a PC/3DS binary would be both unused and wrong.
        if source.startswith("lib/syscall/src/_secure_"):
            continue
        if any(source == r or source.startswith(r + "/") for r in STUBBED_LIB_ROOTS):
            continue
        if source.startswith("lib/NitroSDK/src/"):
            if _nitro_excluded(source[len("lib/NitroSDK/src/"):]):
                continue
        kept.append(source)
    return kept


def overlay_map(root: Path) -> list[dict]:
    """Every main.lsf block's objects with their checkout source and section.

    Blocks in file order: Static main, Autoload ITCM/DTCM, then the 129
    overlays (index = overlay ID). source is null for build-generated
    objects (lib/dsprot's encrypted/encoded/decoder set).
    """
    spec = go.parse_lsf((root / "main.lsf").read_text(encoding="utf-8"))
    blocks = []
    if spec["static"] is not None:
        blocks.append(spec["static"])
    blocks += spec["autoloads"] + spec["overlays"]
    result = []
    for block in blocks:
        result.append({
            "kind": block["kind"],
            "name": block["name"],
            "after": block["after"],
            "address": block["address"],
            "objects": [{"object": e["path"],
                         "source": go.object_source(root, e["path"]),
                         "section": e["section"]} for e in block["objects"]],
        })
    return result


def all_data(root: Path) -> dict:
    variables = check_root(root)
    c = game_c_sources(root)
    cpp = game_cpp_sources(root)
    asm = asm_sources(root, variables)
    lib = lib_sources(root, variables)
    port_lib = port_lib_sources(root, variables)
    overlay = overlay_map(root)
    return {
        "root": str(root),
        "c_sources": c,
        "cpp_sources": cpp,
        "game_sources": sorted(c + cpp),
        "asm_sources": asm,
        "lib_sources": lib,
        "port_lib_sources": port_lib,
        "overlay_map": overlay,
        "counts": {
            "c": len(c),
            "cpp": len(cpp),
            "game": len(c) + len(cpp),
            "asm": len(asm),
            "lib": len(lib),
            "port_lib": len(port_lib),
            "overlays": sum(1 for b in overlay if b["kind"] == "Overlay"),
        },
    }


def render_overlay_map_text(blocks: list[dict]) -> str:
    lines = []
    for block in blocks:
        header = block["kind"] + " " + block["name"]
        if block["after"] is not None:
            header += f" (After {block['after']})"
        if block["address"] is not None:
            header += f" (Address {block['address']:#x})"
        lines.append(header)
        for entry in block["objects"]:
            source = entry["source"] if entry["source"] is not None else "(generated)"
            section = f" [{entry['section']}]" if entry["section"] else ""
            lines.append(f"  {entry['object']}{section} <- {source}")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path("build/upstream"))
    parser.add_argument("--game-sources", action="store_true")
    parser.add_argument("--c-sources", action="store_true")
    parser.add_argument("--cpp-sources", action="store_true")
    parser.add_argument("--asm-sources", action="store_true")
    parser.add_argument("--lib-sources", action="store_true")
    parser.add_argument("--port-lib-sources", action="store_true")
    parser.add_argument("--overlay-map", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    root = args.root
    if args.json:
        data = all_data(root)
        want = {k for k, flag in (
            ("game_sources", args.game_sources), ("c_sources", args.c_sources),
            ("cpp_sources", args.cpp_sources), ("asm_sources", args.asm_sources),
            ("lib_sources", args.lib_sources), ("port_lib_sources", args.port_lib_sources),
            ("overlay_map", args.overlay_map)) if flag}
        if want:
            data = {k: v for k, v in data.items() if k in want | {"root", "counts"}}
        print(json.dumps(data, indent=2))
        return 0

    variables = check_root(root)
    any_list = (args.game_sources or args.c_sources or args.cpp_sources
                or args.asm_sources or args.lib_sources or args.port_lib_sources)
    lists: list[list[str]] = []
    if args.c_sources:
        lists.append(game_c_sources(root))
    if args.cpp_sources:
        lists.append(game_cpp_sources(root))
    if args.game_sources or not (any_list or args.overlay_map):
        lists.append(game_sources(root))
    if args.asm_sources:
        lists.append(asm_sources(root, variables))
    if args.lib_sources:
        lists.append(lib_sources(root, variables))
    if args.port_lib_sources:
        lists.append(port_lib_sources(root, variables))
    for lst in lists:
        for source in lst:
            print(source)
    if args.overlay_map:
        print(render_overlay_map_text(overlay_map(root)), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
