#!/usr/bin/env python3
"""Convert pokeheartgold MWAS assembly (.s) to GAS syntax.

The 150 ARM9 data files (``asm/*.s``, ``lib/asm/*.s``,
``lib/NitroSDK/asm/*.s``, ``lib/MSL_C/asm/*.s``, ``lib/NitroDWC/asm/*.s``)
are written for the CodeWarrior (MWAS) assembler and cannot be fed to GNU
``as`` directly.  This tool translates them so the same file assembles
cleanly under both ``gcc -m32`` (for ``pokeheartgold-pc``) and
``arm-none-eabi-gcc`` (for ``pokeheartgold.3dsx``), preserving symbol names,
section names and alignment.

Usage:
    python tools/s_to_gas.py --root <checkout> <input.s> -o <output.s>
    python tools/s_to_gas.py --root <checkout> <input.s> -o <output.o> \\
        --cc arm-none-eabi-gcc --cflags -march=armv6k -mfpu=vfp

With ``-o *.s`` the tool emits GAS text; with ``-o *.o`` it assembles that
text with ``--cc`` (passing ``--cflags`` to both the preprocessor and the
assembler).  ``-o -`` (the default) writes GAS text to stdout.

Pipeline (each stage exists because MWAS and GAS differ there):

1. Recursive ``.include`` resolution using HGSS's MWASFLAGS search order
   (``asm/``, ``asm/include``, ``lib/asm/include``,
   ``lib/NitroSDK/asm/include``, ``lib/MSL_C/asm/include``,
   ``lib/NitroDWC/asm/include``, ``<root>``).  Files with ``#pragma once``
   are inlined at most once; anything else missing is a hard error.
2. Missing C ``#include`` headers (the build-generated ``msgdata/msg/*.h``
   files when the matching-build generators have not run yet) are replaced
   with a warning plus an empty line.  This is only safe when the file
   never uses the header's constants; a used-but-missing constant would
   otherwise become a silent undefined symbol reference, so every skip is
   reported on stderr.
3. The C preprocessor (``<cc> -E -x assembler-with-cpp`` with ``-DSDK_ASM
   -DPM_ASM``) expands ``FS_EXTERN_OVERLAY(name)`` to
   ``.public SDK_OVERLAY_<name>_ID`` (via the ``SDK_ASM`` branch of
   ``nitro/fs/overlay.h``) and substitutes ``#define`` constants from
   ``constants/*.h``.  C declarations, typedefs and enums from headers are
   stripped by keeping only lines the preprocessor attributes to the
   ``.s``/``.inc`` input: a ``#define`` used in assembly is expanded at
   its use site (kept), while header-side C text is attributed to the
   header (dropped).
4. MWAS ``cw.inc``'s ``exception`` macro uses ``?exc$\\what`` labels, which
   are not valid GAS symbol names (``?`` and ``$`` are unusable there), so
   the whole macro body is replaced with a GAS macro emitting a unique
   local label ``.Lexc_\\@`` per invocation.  No two files use the same
   entry name twice, and ``\\@`` keeps even a single file collision-free.
5. Per-line GAS mapping:

   * Escape-aware ``;`` comment stripping: ``;`` ends the line unless it
     is inside a string literal (e.g. the ``;`` in
     ``dwc_lobby_chat_data.s``'s big printable-ASCII ``.asciz``), with
     ``\\"`` and ``\\\\`` handled.  (``//`` comments are already gone,
     removed by the preprocessor; ``//`` inside strings survives it.)
   * ``.public`` -> ``.global``.  ``.extern`` is kept: GAS on both
     targets accepts it as a no-op for compatibility.
   * Bare ``.rodata`` -> ``.section .rodata,"a",%progbits`` + ``.balign 4``.
   * MWAS ``.section <name>,<align>,<type>,<flags>`` ->
     ``.section <name>,"<gas-flags>",%<progbits|nobits>`` +
     ``.balign <align>``.  The middle field is the MWAS section type
     (``8`` = NOBITS, seen only on ``.bss*``; ``1`` = PROGBITS) and the
     last field is the ELF ``sh_flags`` value (``3`` = WRITE|ALLOC on
     ``.data*``/``.bss*``, ``2`` = ALLOC on ``.rodata*``/``.exceptix``),
     so ``.bss*`` lands on ``"aw",%nobits`` with the alignment MWAS used.
     The alignment value is in bytes (cf. the ``; 0x...`` address comments
     in the sources, which advance by exact data widths, and the
     ``.balign 4`` convention the Platinum port uses for the same MWAS
     format); it is emitted verbatim as ``.balign <align>``.
   * ``.type sym,@function`` -> ``.type sym,%function`` (``@`` is not the
     GAS type prefix on either target; ``%`` works on both x86 and ARM
     GAS, as does ``%object``).
   * ``.word`` -> ``.4byte``.  This is the one width-sensitive directive
     the files use (4420 occurrences): ARM GAS ``.word`` is 4 bytes but
     x86 GAS ``.word`` is 2 bytes (it even warns ``value truncated``), so
     emitting ``.word`` would silently corrupt every pointer table in the
     ``pokeheartgold-pc`` build.  ``.4byte`` is exactly 4 bytes on both
     targets (``R_ARM_ABS32`` / ``R_386_32`` relocations for symbols).
     ``.byte``/``.short``/``.ascii``/``.asciz``/``.space``/``.balign``
     already mean the same on both targets and pass through untouched.
"""

import argparse
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


class S2GError(Exception):
    """Fatal, reportable conversion failure (exit 1)."""


# HGSS's MWASFLAGS search order for `.include` (relative to the checkout).
INCLUDE_SEARCH = [
    "asm",
    "asm/include",
    "lib/asm/include",
    "lib/NitroSDK/asm/include",
    "lib/MSL_C/asm/include",
    "lib/NitroDWC/asm/include",
    ".",
]

# `-I` search order for C `#include` (mirrors MWASFLAGS `-i`/`-I` flags).
CPP_SEARCH = [
    ".",
    "include",
    "asm/include",
    "files",
    "lib/asm/include",
    "lib/NitroDWC/asm/include",
    "lib/MSL_C/asm/include",
    "lib/NitroSDK/asm/include",
    "lib/syscall/asm/include",
    "asm",
    "files/msgdata",
    "lib/include",
]

INCLUDE_RE = re.compile(r'^\s*\.include\s+"([^"]+)"')
CPP_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
PRAGMA_ONCE_RE = re.compile(r"(?m)^\s*#\s*pragma\s+once\b")
EXCEPTION_MACRO_RE = re.compile(r"\.macro\s+exception\b.*?\.endm", re.S)
LINEMARK_RE = re.compile(r'^#\s+\d+\s+"([^"]+)"')
PUBLIC_RE = re.compile(r"^(\s*)\.public\b")
TYPE_RE = re.compile(r"^(\s*\.type\s+\S+\s*,\s*)@(\w+)")
WORD_RE = re.compile(r"^(\s*)\.word\b")
# `FS_EXTERN_OVERLAY(name)` expands to `.public SDK_OVERLAY_<name>_ID` in the
# DS build's link script. The port gives each of those a numeric value at
# assembly time instead (see --overlay-ids): a link-script absolute symbol
# leaves `R_ARM_ABS32` relocations with a value below the image base, which
# 3dsxtool rejects ("Relocation to invalid address") because the 3dsx loader
# would add the load address to an overlay number.
OVERLAY_DEF_RE = re.compile(r"^\s*SDK_OVERLAY_(\w+)_ID\s*=\s*(\d+)\s*;")
OVERLAY_REF_RE = re.compile(r"\bSDK_OVERLAY_(\w+)_ID\b")
OVERLAY_DIRECTIVE_RE = re.compile(
    r"^\s*\.(?:public|global)\s+SDK_OVERLAY_(\w+)_ID\s*$")
RODATA_RE = re.compile(r"^\s*\.rodata\s*$")
MWAS_SECTION_RE = re.compile(
    r"^(\s*)\.section\s+(\S+?)\s*,\s*(\d+)\s*(?:,\s*(\d+)\s*,\s*(\d+))?\s*$"
)

# GAS replacement for cw.inc's `exception` macro.  `?exc$\what` is not a
# valid GAS symbol, so each entry becomes a unique local label; `\@` is
# GAS's per-invocation counter, so even repeated names stay conflict-free.
# The `.type`/`.global`/`.size` lines of the MWAS macro are dropped: the
# entries are pure data for the `.exceptix` section and need no symbols.
EXCEPTION_GAS_MACRO = (
    ".macro exception what, size, data\n"
    ".section .exceptix,\"a\",%progbits\n"
    ".balign 4, 0\n"
    ".Lexc_\\@:\n"
    ".word \\what\n"
    ".short \\size\n"
    ".word \\data\n"
    ".previous\n"
    ".endm"
)


def split_cflags(raw):
    """Shell-split trailing `--cflags` tokens into a flag list."""
    out = []
    for item in raw or ():
        out.extend(shlex.split(item))
    return out


def extract_user_includes(cflags):
    """Pull `-I<dir>` / `-I <dir>` entries out of raw compiler flags."""
    dirs = []
    i = 0
    while i < len(cflags):
        arg = cflags[i]
        if arg == "-I" and i + 1 < len(cflags):
            dirs.append(cflags[i + 1])
            i += 2
        elif arg.startswith("-I") and len(arg) > 2:
            dirs.append(arg[2:])
            i += 1
        else:
            i += 1
    return dirs


def resolve(name, dirs):
    for d in dirs:
        cand = Path(d) / name
        if cand.is_file():
            return cand
    return None


def inline_file(path, include_dirs, stack, once_seen):
    """Recursively inline `.include` files; return a list of text lines."""
    resolved = Path(path)
    key = resolved.resolve()
    if key in stack:
        chain = " -> ".join([str(p) for p in stack] + [str(resolved)])
        raise S2GError(f".include cycle: {chain}")
    try:
        text = resolved.read_text(encoding="utf-8")
    except OSError as exc:
        raise S2GError(f"cannot read {resolved}: {exc}") from exc
    if PRAGMA_ONCE_RE.search(text):
        if key in once_seen:
            return [f"; s_to_gas: already inlined (pragma once): {resolved}"]
        once_seen.add(key)
    stack.append(key)
    try:
        out = [f"; s_to_gas: inlined: {resolved}"]
        for line in text.splitlines():
            m = INCLUDE_RE.match(line)
            if m:
                target = resolve(m.group(1), include_dirs)
                if target is None:
                    searched = ", ".join([str(d) for d in include_dirs])
                    raise S2GError(
                        f"{resolved}: .include not found: {m.group(1)!r} "
                        f"(searched: {searched})"
                    )
                out.extend(inline_file(target, include_dirs, stack, once_seen))
            else:
                out.append(line)
        return out
    finally:
        stack.pop()


def rewrite_exception_macro(text):
    """Replace cw.inc's `exception` macro with the GAS-safe version."""
    return EXCEPTION_MACRO_RE.sub(lambda _m: EXCEPTION_GAS_MACRO, text)


def strip_semicolon(line):
    """Strip an MWAS `;` comment, preserving `;` inside string literals.

    Handles `\\"` (escaped quote stays in the string) and `\\\\`
    (an escaped backslash does not escape the following quote).
    """
    res = []
    in_str = False
    esc = False
    for ch in line:
        if in_str and not esc and ch == "\\":
            esc = True
            res.append(ch)
            continue
        if ch == '"' and not esc:
            in_str = not in_str
            res.append(ch)
        elif ch == ";" and not in_str:
            break
        else:
            res.append(ch)
        esc = False
    return "".join(res)


def map_section(indent, name, align, stype, flags):
    """Map an MWAS `.section` directive to GAS section + balign lines."""
    if stype is None:
        nobits = name == ".bss" or name.startswith((".bss.", ".sbss"))
        writable = name.startswith((".data", ".bss", ".sdata", ".sbss"))
    else:
        # MWAS middle field: 8 = NOBITS (only ever on .bss*), 1 = PROGBITS.
        # Last field: ELF sh_flags (3 = WRITE|ALLOC, 2 = ALLOC).
        nobits = stype == "8"
        writable = flags == "3"
    executable = name.startswith((".text", ".init", ".fini"))
    gas_flags = "a" + ("w" if writable else "") + ("x" if executable else "")
    kind = "%nobits" if nobits else "%progbits"
    return [
        f'{indent}.section {name},"{gas_flags}",{kind}',
        f"{indent}.balign {align}",
    ]


def load_overlay_ids(path):
    """Read `SDK_OVERLAY_<name>_ID = <n>;` lines into {name: n}.

    The file is the ld script tools/game_overlays.py writes from main.lsf, so
    the numbers come from the same source of truth as the DS link order.
    """
    ids = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        m = OVERLAY_DEF_RE.match(line)
        if m:
            ids[m.group(1)] = int(m.group(2))
    return ids


def gas_translate_line(line, overlay_ids=None):
    """Translate one post-cpp line; return a list of output lines."""
    line = strip_semicolon(line)
    if overlay_ids:
        m = OVERLAY_DIRECTIVE_RE.match(line)
        if m and m.group(1) in overlay_ids:
            # Drop the declaration: the reference below becomes a number, so
            # the linker script's absolute symbol is not used at all.
            return []
        line = OVERLAY_REF_RE.sub(
            lambda hit: str(overlay_ids[hit.group(1)])
            if hit.group(1) in overlay_ids else hit.group(0), line)
    m = PUBLIC_RE.match(line)
    if m:
        line = f"{m.group(1)}.global{line[m.end():]}"
    m = TYPE_RE.match(line)
    if m:
        line = f"{m.group(1)}%{m.group(2)}"
    if RODATA_RE.match(line):
        indent = line[: len(line) - len(line.lstrip())]
        return [f'{indent}.section .rodata,"a",%progbits', f"{indent}.balign 4"]
    m = MWAS_SECTION_RE.match(line)
    if m:
        return map_section(m.group(1), m.group(2), m.group(3), m.group(4),
                           m.group(5))
    m = WORD_RE.match(line)
    if m:
        line = f"{m.group(1)}.4byte{line[m.end():]}"
    return [line.rstrip()]


def translate_cpp_output(text, overlay_ids=None):
    """Drop C-header lines via cpp linemarkers, then GAS-map the rest."""
    asm_lines = []
    keep = True
    for line in text.splitlines():
        m = LINEMARK_RE.match(line)
        if m:
            name = m.group(1)
            keep = name == "<stdin>" or name.endswith(".s") or \
                name.endswith(".inc")
            continue
        if line.startswith("#"):
            continue
        if keep:
            asm_lines.append(line)
    out = []
    for line in asm_lines:
        out.extend(gas_translate_line(line, overlay_ids))
    return "\n".join(out) + "\n"


def blank_missing_c_includes(lines, cpp_dirs):
    """Replace unresolvable `#include`s with a comment; report warnings."""
    out = []
    warnings = []
    for line in lines:
        m = CPP_INCLUDE_RE.match(line)
        if m and resolve(m.group(1), cpp_dirs) is None:
            warnings.append(
                f"warning: C header not found, skipping: {m.group(1)} "
                f"(line kept only if the file uses none of its defines)")
            out.append(f"; s_to_gas: missing header skipped: {m.group(1)}")
        else:
            out.append(line)
    return out, warnings


def convert(src, root, cc="cc", cflags=(), extra_inc_dirs=(),
            overlay_ids=None):
    """Run the full MWAS -> GAS pipeline; return (gas_text, warnings)."""
    root = Path(root)
    if not root.is_dir():
        raise S2GError(f"--root is not a directory: {root}")
    user_inc = [str(p) for p in extra_inc_dirs]
    include_dirs = [str(root / d) for d in INCLUDE_SEARCH] + user_inc
    cpp_dirs = [str(root / d) for d in CPP_SEARCH] + user_inc
    lines = inline_file(Path(src), include_dirs, [], set())
    lines, warnings = blank_missing_c_includes(lines, cpp_dirs)
    combined = rewrite_exception_macro("\n".join(lines) + "\n")
    cmd = [cc, "-E", "-x", "assembler-with-cpp", "-",
           "-DSDK_ASM", "-DPM_ASM"]
    cmd += [f"-I{d}" for d in cpp_dirs]
    cmd += list(cflags)
    try:
        proc = subprocess.run(cmd, input=combined, capture_output=True,
                              text=True)
    except OSError as exc:
        raise S2GError(f"cannot run preprocessor {cc!r}: {exc}") from exc
    if proc.returncode != 0:
        raise S2GError(
            f"preprocessing {src} failed (exit {proc.returncode}):\n"
            f"{proc.stderr}")
    return translate_cpp_output(proc.stdout, overlay_ids), warnings


def assemble(gas_text, output, cc, cflags):
    """Assemble GAS text to `output` with `cc -c`; raise S2GError on error."""
    output = Path(output)
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".s",
                                         prefix="s_to_gas_",
                                         encoding="utf-8",
                                         delete=False) as fh:
            fh.write(gas_text)
            tmp = fh.name
        cmd = [cc, "-c", tmp, "-o", str(output)] + list(cflags)
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True)
        except OSError as exc:
            raise S2GError(f"cannot run assembler {cc!r}: {exc}") from exc
        if proc.returncode != 0:
            raise S2GError(
                f"assembling {output} failed (exit {proc.returncode}); "
                f"GAS text kept at {tmp}:\n{proc.stderr}")
    finally:
        if tmp is not None and Path(tmp).exists():
            try:
                Path(tmp).unlink()
            except OSError:
                pass


def build_parser():
    parser = argparse.ArgumentParser(
        description="Convert an MWAS .s file to GAS syntax (or assemble it).")
    parser.add_argument("src", help="input MWAS .s file")
    parser.add_argument("--root", required=True,
                        help="pinned pokeheartgold checkout root")
    parser.add_argument("-o", "--output", default="-",
                        help="output GAS .s file, .o object, or - for stdout "
                             "(default: -)")
    parser.add_argument("--cc", default="cc",
                        help="C compiler used for preprocessing and (for .o "
                             "output) assembling (default: cc)")
    parser.add_argument("--cflags", nargs=argparse.REMAINDER, default=[],
                        metavar="FLAGS",
                        help="extra compiler flags, passed to both stages; "
                             "must come last on the command line, e.g. "
                             "--cflags -m32 -march=armv6k")
    parser.add_argument("--overlay-ids", type=Path,
                        help="ld script with `SDK_OVERLAY_<name>_ID = <n>;` "
                             "lines (tools/game_overlays.py's output). Each "
                             "overlay-ID reference is assembled as its number, "
                             "so the object carries no absolute relocation "
                             "that 3dsxtool would reject.")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    cflags = split_cflags(args.cflags)
    extra_inc = extract_user_includes(cflags)
    overlay_ids = load_overlay_ids(args.overlay_ids) if args.overlay_ids else None
    try:
        gas_text, warnings = convert(args.src, args.root, cc=args.cc,
                                     cflags=cflags,
                                     extra_inc_dirs=extra_inc,
                                     overlay_ids=overlay_ids)
    except S2GError as exc:
        print(f"s_to_gas: error: {exc}", file=sys.stderr)
        return 1
    for warning in warnings:
        print(f"s_to_gas: {warning}", file=sys.stderr)
    if args.output == "-":
        sys.stdout.write(gas_text)
        return 0
    if args.output.endswith(".o"):
        try:
            assemble(gas_text, args.output, args.cc, cflags)
        except S2GError as exc:
            print(f"s_to_gas: error: {exc}", file=sys.stderr)
            return 1
        return 0
    try:
        Path(args.output).write_text(gas_text, encoding="utf-8")
    except OSError as exc:
        print(f"s_to_gas: error: cannot write {args.output}: {exc}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
