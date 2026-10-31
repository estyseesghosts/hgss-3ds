#!/usr/bin/env python3
"""Compare the linked PC program's symbols with the DS reference program's.

Usage:
    python3 tools/symbol_crosscheck.py
    python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc \\
        --reference /work/pokeheartgold/build/heartgold.us/main.elf

A linked program contains one symbol for every function and object the link
kept. If a source file was left out of the PC build (not added to the source
list, or blocked because it does not compile yet), its symbols are simply not
there. This tool takes every global game/SDK symbol the DS reference ELF
defines and requires it in the PC ELF, unless it is listed in
tools/symbol_crosscheck_allowlist.txt with the reason it was replaced or
stubbed. Anything else is a missing source, and the run exits 1 with the set.

The reference ELF, in order of preference:

    build/reference/main.elf
    /work/pokeheartgold/build/heartgold.us/main.elf
    /work/pokeheartgold/build/soulsilver.us/main.elf

If none exists and --no-build-reference was not given, the tool runs
tools/matching-build.sh /work/pokeheartgold HEARTGOLD (see the build
reference; it needs the private MWCC toolchain).

pyelftools is imported only when an ELF is read, so --help works without it.

The comparison set: symbols that are STB_GLOBAL and defined, with a C-like
name (``[A-Za-z_][A-Za-z0-9_]*``). Linker labels with dots (``SDK_OVERLAY.*``),
MWCC's C++ names (``?exc$...``, ``$``) and section/file symbols are not game
or SDK code symbols; ``_Z...`` names are kept, because both toolchains use
the Itanium ABI for ordinary C++ symbols. The allowlist is checked by
fnmatch, and the first matching row wins.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Mapping

REPO = Path(__file__).resolve().parent.parent
DEFAULT_PC = REPO / "build" / "pokeheartgold-pc"
DEFAULT_ALLOWLIST = REPO / "tools" / "symbol_crosscheck_allowlist.txt"
MATCHING_BUILD = REPO / "tools" / "matching-build.sh"
REFERENCE_CANDIDATES = (
    REPO / "build" / "reference" / "main.elf",
    Path("/work/pokeheartgold/build/heartgold.us/main.elf"),
    Path("/work/pokeheartgold/build/soulsilver.us/main.elf"),
)
REFERENCE_DECOMP = Path("/work/pokeheartgold")
REFERENCE_VERSION = "HEARTGOLD"

# Global symbols the comparison covers. C++ names from GCC also match; MWCC's
# ``?exc$...`` and linker labels with dots do not on purpose (see the module
# docstring).
C_SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SYMBOL_TYPES = ("STT_NOTYPE", "STT_OBJECT", "STT_FUNC", "STT_COMMON", "STT_TLS")


@dataclass(frozen=True)
class Symbol:
    """A symbol the comparison cares about."""

    name: str
    kind: str
    section: str


@dataclass(frozen=True)
class AllowEntry:
    """One allowlist row: a symbol or fnmatch pattern, and why it is absent."""

    pattern: str
    reason: str


@dataclass
class CompareResult:
    """The three outcomes of a comparison, sorted by name."""

    matched: list[str] = field(default_factory=list)
    allowlisted: list[tuple[str, AllowEntry]] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """True when nothing that should be in the PC ELF is missing."""
        return not self.missing


# ------------------------------------------------------------------ ELF side

def symbols_from_elf(elf) -> dict[str, Symbol]:
    """The global, defined, C-named symbols of an ELFFile-like object.

    `elf` may be a pyelftools ELFFile or anything that iterates sections with
    a ``header`` mapping and ``iter_symbols()`` (the tests use a small fake).
    Duplicate names (a weak and a strong definition) collapse to the first.
    """
    result: dict[str, Symbol] = {}
    for section in elf.iter_sections():
        if section.header.get("sh_type") != "SHT_SYMTAB":
            continue
        for sym in section.iter_symbols():
            name = sym.name
            if not name or not C_SYMBOL_RE.match(name):
                continue
            info = sym.entry["st_info"]
            if info["bind"] != "STB_GLOBAL":
                continue
            if sym.entry["st_shndx"] == "SHN_UNDEF":
                continue
            kind = info["type"]
            if kind not in SYMBOL_TYPES:
                continue
            result.setdefault(name, Symbol(name, kind, str(sym.entry["st_shndx"])))
    return result


def load_elf_symbols(path: Path) -> dict[str, Symbol]:
    """Read one ELF file's comparison symbols (imports pyelftools here)."""
    from elftools.elf.elffile import ELFFile

    with open(path, "rb") as handle:
        return symbols_from_elf(ELFFile(handle))


# --------------------------------------------------------------- allowlist

def parse_allowlist(text: str) -> list[AllowEntry]:
    """Parse ``PATTERN  # reason`` rows; blank lines and # comments are skipped.

    A row without a reason is an error: every replacement or stub must say
    why it is absent, so the table cannot quietly grow into a blanket.
    """
    entries: list[AllowEntry] = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        pattern, sep, reason = line.partition("#")
        pattern = pattern.strip()
        reason = reason.strip()
        if not sep or not pattern:
            raise ValueError(f"allowlist line {lineno}: expected 'PATTERN  # reason'")
        if not reason:
            raise ValueError(f"allowlist line {lineno}: {pattern!r} has no reason")
        entries.append(AllowEntry(pattern, reason))
    return entries


def load_allowlist(path: Path) -> list[AllowEntry]:
    """Read and parse the allowlist file."""
    return parse_allowlist(path.read_text(encoding="utf-8"))


def find_allow_entry(name: str, entries: Iterable[AllowEntry]) -> AllowEntry | None:
    """The first allowlist row matching `name` (fnmatch), or None."""
    for entry in entries:
        if fnmatch.fnmatchcase(name, entry.pattern):
            return entry
    return None


# ---------------------------------------------------------------- comparing

def compare_symbols(reference: Mapping[str, Symbol], pc: Mapping[str, Symbol],
                    allowlist: Iterable[AllowEntry]) -> CompareResult:
    """Classify every reference symbol as matched, allowlisted or missing."""
    entries = list(allowlist)
    result = CompareResult()
    for name in sorted(reference):
        if name in pc:
            result.matched.append(name)
            continue
        entry = find_allow_entry(name, entries)
        if entry is not None:
            result.allowlisted.append((name, entry))
        else:
            result.missing.append(name)
    return result


# ---------------------------------------------------------------- reference

def discover_reference() -> Path | None:
    """The first reference ELF that exists, or None."""
    for candidate in REFERENCE_CANDIDATES:
        if candidate.is_file():
            return candidate
    return None


def build_reference() -> Path | None:
    """Run tools/matching-build.sh and return the ELF it produced."""
    if not MATCHING_BUILD.is_file():
        return None
    cmd = ["bash", str(MATCHING_BUILD), str(REFERENCE_DECOMP), REFERENCE_VERSION]
    print("+ " + " ".join(cmd), file=sys.stderr)
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        return None
    return discover_reference()


# --------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Check that every reference game/SDK symbol is in the PC ELF.")
    ap.add_argument("--pc", type=Path, default=DEFAULT_PC,
                    help="linked PC program (default: build/pokeheartgold-pc)")
    ap.add_argument("--reference", type=Path,
                    help="DS reference ELF (default: build/reference, then /work)")
    ap.add_argument("--allowlist", type=Path, default=DEFAULT_ALLOWLIST,
                    help="replaced/stubbed symbol table "
                         "(default: tools/symbol_crosscheck_allowlist.txt)")
    ap.add_argument("--no-build-reference", action="store_true",
                    help="do not run tools/matching-build.sh when the "
                         "reference ELF is missing")
    ap.add_argument("--show-allowlisted", action="store_true",
                    help="print every allowlisted symbol with its reason")
    args = ap.parse_args(argv)

    if not args.allowlist.is_file():
        print(f"symbol cross-check: allowlist not found: {args.allowlist}", file=sys.stderr)
        return 2
    allowlist = load_allowlist(args.allowlist)
    print(f"allowlist: {args.allowlist} ({len(allowlist)} entries)")

    if not args.pc.is_file():
        print(f"symbol cross-check: PC ELF not found: {args.pc}", file=sys.stderr)
        print("  (the link has not succeeded yet; this tool starts working when it does)",
              file=sys.stderr)
        return 2

    reference = args.reference or discover_reference()
    if reference is None and not args.no_build_reference:
        reference = build_reference()
    if reference is None or not reference.is_file():
        print("symbol cross-check: reference ELF not found "
              "(build/reference/main.elf or /work/pokeheartgold/build/*/main.elf); "
              "run tools/matching-build.sh",
              file=sys.stderr)
        return 2

    try:
        reference_symbols = load_elf_symbols(reference)
        pc_symbols = load_elf_symbols(args.pc)
    except ImportError:
        print("symbol cross-check: pyelftools is required (pip install pyelftools)",
              file=sys.stderr)
        return 2
    except OSError as error:
        print(f"symbol cross-check: {error}", file=sys.stderr)
        return 2

    result = compare_symbols(reference_symbols, pc_symbols, allowlist)
    print(f"reference:  {reference} ({len(reference_symbols)} symbols)")
    print(f"pc:         {args.pc} ({len(pc_symbols)} symbols)")
    print(f"matched:    {len(result.matched)}")
    print(f"allowlisted: {len(result.allowlisted)}")
    print(f"missing:    {len(result.missing)}")

    if args.show_allowlisted:
        for name, entry in result.allowlisted:
            print(f"  allow {name}: {entry.reason}")

    if result.missing:
        print("missing symbols:", file=sys.stderr)
        for name in result.missing:
            print(f"  {name}", file=sys.stderr)
        return 1
    print("ok: every reference symbol is present or allowlisted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
