#!/usr/bin/env python3
"""Generate every build-time header the HGSS C/C++/asm build needs, without a ROM or MWCC.

Usage:
    python3 tools/prepare_game_headers.py --root <decomp-checkout>
    python3 tools/prepare_game_headers.py --root build/upstream --ids-output /tmp/overlay_ids.ld --header /tmp/overlay_ids.h

The DS build generates headers as side effects of running its own tools
(msgenc, nitroarc, o2narc, csv2bin, gen_fx_consts, makelcf) under MWCC/wine.
The port needs the same headers to compile the same sources with GCC, but
must not need a ROM, MWCC, or wine to get them. This script reimplements the
*index* outputs of those tools in pure Python:

* ``lib/include/nitro/fx/fx_const.h`` from ``tools/gen_fx_consts/fx_const.csv``
  (byte-identical port of ``MakeFix``/``GetFxWidth``/``main.c`` formatting).
* ``files/msgdata/msg/msg_0729.gmm`` from ``files/poketool/trainer/trainers.json``
  plus ``trname.json.txt`` (the one jsonproc template whose output feeds the
  message build; ``fmtInt(i, 5, 2)`` zero-pads, then ``&`` is escaped to
  ``&amp;`` exactly like the Makefile's ``sed`` step).
* All ``files/msgdata/msg/msg_*.h`` from ``files/msgdata/msg/*.gmm`` (byte port
  of ``GMM::WriteGmmHeader``: guard from the ``-H`` path, ``//`` comments from
  ``SplitMessage`` 300-byte chunks, ``#define <id> <index>`` in row order,
  garbage rows blanked from the Japanese text exactly like ``FromFile``).
* All ``files/**/*.naix`` archive index headers:
  - nitroarc ``--index-namespace`` packs: explicit ``.narcorder`` entries first
    in file order, then the remaining build-time directory members sorted with
    C ``strcmp`` order, minus ``.narcignore``/``--exclude``/default excludes.
    Generated outputs (``.bin`` from ``.gmm``/``.s``/``.json``, graphics from
    ``.png``/``.pal``, ``build-pokegra``/``build-otherpoke`` contents) are
    derived from the ``files/**/*.mk`` rules in Python; no archive is built.
  - o2narc ``-n`` packs: ``NARC_<stem>_<stem>_NNNNNNNN`` with the member count
    derived from the JSON source each ``.mk`` names (trainers, baseStats,
    ``NUM_SPECIES + 1``, encounters, photos, tables, ``gDungeons`` +
    ``gOverworlds`` + per-method/mon pairs, single-symbol ``gData``).
  - o2narc ``-N`` packs (``zukan_data``, ``zukan_data_gira``): member names are
    the 11 ``mon_stats_*`` labels plus ``sort_order_<type>_<id>`` per
    ``zukan_data.json`` sorting entry, in template order.
  - csv2bin ``--naix`` packs: ``NARC_<stem>_NNNN_bin`` with the member count
    from the CSV row count.
  - ``zukan_enc.naix`` / ``zukan_hw_data.naix``: the ``sed 's/_<short>​//g'``
    normalisation of the versioned naix, applied case-sensitively exactly like
    the ``.mk`` rules (uppercase guards keep their suffix).
* Overlay ID header/ld-script via :mod:`game_overlays` (``--ids-output`` /
  ``--header``; same ``render_ids``/``render_ids_header`` the port build uses).

Everything is written into ``--root`` (default ``build/upstream``), never to
git. Files are only rewritten when their content changes. The script prints a
summary of what it generated.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import math
import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_overlays as go  # noqa: E402


# ---------------------------------------------------------------------------
# Generic helpers


def write_if_changed(path: Path, text: str) -> bool:
    """Write ``text`` to ``path`` (creating parents) only if different."""
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return True


def write_bytes_if_changed(path: Path, data: bytes) -> bool:
    """Write ``data`` to ``path`` (creating parents) only if different."""
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def guardify(name: str) -> str:
    """nitroarc's guardify(): keep [0-9A-Za-z_], map anything else to '_'."""
    return re.sub(r"[^0-9A-Za-z_]", "_", name)


def guardify_nhits(name: str, nhits: int) -> str:
    """nitroarc's guardify_nhits(): ``_<nhits>`` suffix for repeat names."""
    base = guardify(name)
    return base if nhits == 0 else f"{base}_{nhits}"


def msgenc_guard(filename: str) -> str:
    """msgenc's WriteGmmHeader guard: ``/``/``.``/``-`` to ``_``, else upper."""
    out = []
    for c in filename:
        if c in "/.-":
            out.append("_")
        else:
            out.append(c.upper())
    return "".join(out)


# ---------------------------------------------------------------------------
# nitroarc --index-namespace output (tools/nitroarc/src/opmode_create.c)


def render_nitroarc_naix(narc_rel: str, members: list[str]) -> str:
    """Byte port of nitroarc's write_index() with ``--index-namespace``.

    ``narc_rel`` is the archive path exactly as make passes it (e.g.
    ``files/msgdata/msg.narc``); ``members`` are archive-relative member
    names in pack order.
    """
    dot = narc_rel.rfind(".")
    stem = narc_rel[:dot]
    iname = stem + ".naix"
    guard = guardify(iname)
    slash = narc_rel.rfind("/")
    bname = narc_rel[slash + 1:dot] + "_"
    lines = [
        "/*",
        f" * This file was generated by nitroarc for '{narc_rel}'",
        " * DO NOT MODIFY IT; MANUAL EDITS WILL BE LOST",
        " */",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    seen: dict[str, int] = {}
    for i, member in enumerate(members):
        nhits = seen.get(member, 0)
        seen[member] = nhits + 1
        lines.append(f"#define NARC_{bname}{guardify_nhits(member, nhits)} {i}")
    lines += ["", f"#define {guard[:-5]}_COUNT {len(members)}", "",
              f"#endif /* {guard} */", ""]
    return "\n".join(lines)


def nitroarc_members(input_dir: Path, order: list[str] | None,
                     excludes: list[str]) -> list[str]:
    """Pack order for a nitroarc ``--index-namespace <dir>`` invocation.

    Explicit ``.narcorder`` entries first (in file order), then every other
    build-time member of ``input_dir`` in C ``strcmp`` (byte) order, skipping
    fnmatch excludes. ``input_dir`` holds the build-time file set: callers
    pass the checkout directory plus derived generated outputs (see
    :func:`generated_outputs`); exclusion uses basenames-or-relative paths
    exactly like nitroarc, which runs with the input dir as cwd.
    """
    explicit = list(order or [])
    have = set(explicit)
    rest = sorted(
        (m for m in _walk_members(input_dir)
         if m not in have and not _excluded(m, excludes)),
        key=lambda s: s.encode("utf-8"),
    )
    return explicit + rest


def _walk_members(input_dir: Path) -> list[str]:
    """All files under ``input_dir`` as nitroarc sees them (cwd-relative).

    nitroarc walks subdirectories breadth-first but sorts only the implicit
    tail, so enumeration order never matters here; plain recursive listing is
    equivalent. Symlinks are followed the way ``stat()`` follows them.
    """
    out = []
    for path in sorted(input_dir.rglob("*")):
        # Skip directories and dangling symlinks, like stat() failures do.
        if path.is_file():
            out.append(path.relative_to(input_dir).as_posix())
    return out


def _excluded(name: str, excludes: list[str]) -> bool:
    """fnmatch(3) without flags, like nitroarc's should_exclude()."""
    return any(fnmatch.fnmatchcase(name, pat) for pat in excludes)


def read_dotfile(path: Path) -> list[str]:
    """Parse a ``.narcorder``/``.narcignore`` file like nitroarc does.

    Lines are split on ``\\n``; leading/trailing space, tab and ``\\r`` are
    stripped; empty lines are skipped.
    """
    entries = []
    for line in path.read_text(encoding="utf-8").split("\n"):
        value = line.strip(" \t\r")
        if value:
            entries.append(value)
    return entries


# ---------------------------------------------------------------------------
# o2narc / csv2bin output


def render_o2narc_naix(narc_rel: str, names: list[str]) -> str:
    """Byte port of o2narc's WriteNaix() without ``--naix-names``.

    ``names`` are the member symbols in pack order (numeric
    ``<stem>_<8 digits>`` entries are derived by :func:`o2narc_numeric_names`).
    """
    stem = Path(narc_rel).name.rsplit(".", 1)[0]
    upper = stem.upper()
    lines = [
        "/*",
        " * THIS FILE WAS AUTOMATICALLY",
        " *  GENERATED BY tools/o2narc",
        " *      DO NOT MODIFY!!!",
        " */",
        "",
        f"#ifndef NARC_{upper}_NAIX_",
        f"#define NARC_{upper}_NAIX_",
        "",
        "enum {",
    ]
    lines += [f"    NARC_{stem}_{name} = {i}," for i, name in enumerate(names)]
    lines += ["};", "", f"#endif //NARC_{upper}_NAIX_", ""]
    return "\n".join(lines)


def o2narc_numeric_names(stem: str, count: int) -> list[str]:
    """The ``<stem>_<8-digit counter>`` symbols o2narc emits without ``-N``."""
    return [f"{stem}_{i:08d}" for i in range(count)]


def render_csv2bin_naix(narc_rel: str, count: int) -> str:
    """Byte port of csv2bin's ``--naix`` output (``<stem>_NNNN_bin``)."""
    stem = Path(narc_rel).name.rsplit(".", 1)[0]
    guard = f"NARC_{stem}_NAIX_".upper()
    lines = [
        "/*",
        " * THIS FILE WAS AUTOMATICALLY",
        " *  GENERATED BY tools/csv2bin",
        " *      DO NOT MODIFY!!!",
        " */",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "enum {",
    ]
    lines += [f"    NARC_{stem}_{i:04d}_bin = {i}," for i in range(count)]
    lines += ["};", "", f"#endif // {guard}", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# fx_const.h (tools/gen_fx_consts/{fx_data,fx_encode,main}.c)


FX_SPECS = {
    "fx32": (19, 12),
    "fx64": (51, 12),
    "fx64c": (31, 32),
    "fx16": (3, 12),
}


def make_fix(value: float, fracpart: int) -> int:
    """Byte-exact port of ``MakeFix`` (all HGSS constants fit in s64)."""
    if value == 0:
        return 0
    magnitude = abs(value)
    int_part = math.floor(magnitude)
    frac = magnitude - int_part
    whole = int(magnitude) * (1 << fracpart)
    partial = int(frac * float(1 << fracpart) + 0.5)
    whole &= ~((1 << fracpart) - 1)
    partial &= (1 << fracpart) - 1
    result = whole + partial
    return -result if value < 0 else result


def fx_width(fxtype: str) -> int:
    """Port of ``GetFxWidth``: intpart + fracpart + 1."""
    intpart, fracpart = FX_SPECS[fxtype]
    return intpart + fracpart + 1


def render_fx_const_h(csv_text: str, guard_source: str) -> str:
    """Port of ``gen_fx_consts/main.c``. ``guard_source`` is the output path
    string exactly as passed on the tool command line (checkout-relative)."""
    guard = guardify(guard_source).upper()
    lines = [f"#ifndef {guard}_", f"#define {guard}_", ""]
    for raw in csv_text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, fxtype, text = [field.strip() for field in line.split(",", 2)]
        if fxtype not in FX_SPECS:
            raise ValueError(f"invalid fx type {fxtype!r}")
        value = float(text)
        fixed = make_fix(value, FX_SPECS[fxtype][1])
        width = fx_width(fxtype)
        macro = f"{fxtype}_{name}".upper()
        if width == 16:
            lines.append(f"#define {macro:<32} (({fxtype}) 0x{fixed & 0xFFFF:04x})    // {value:.12f}")
        elif width == 32:
            lines.append(f"#define {macro:<32} (({fxtype}) 0x{fixed & 0xFFFFFFFF:08x}L)    // {value:.12f}")
        elif width == 64:
            lines.append(f"#define {macro:<32} (({fxtype}) 0x{fixed & 0xFFFFFFFFFFFFFFFF:016x}LL)  // {value:.12f}")
        else:
            raise ValueError(f"invalid integer width: {width}")
    lines += ["", f"#endif //{guard}_", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# msgdata: msg_0729.gmm + msg_*.h (tools/msgenc/Gmm.cpp, files/msgdata/msg.mk)


def fmt_int(num: int, ndigits: int, format: int) -> str:
    """Literal port of jsonproc's ``fmtInt`` callback (format 2 zero-pads)."""
    buf = ""
    printing_zeros = (format == 2)
    pow10 = 1
    for _ in range(1, ndigits):
        pow10 *= 10
    while pow10 > 0:
        quot, num = divmod(num, pow10)
        pow10 //= 10
        if quot != 0:
            printing_zeros = True
        elif not printing_zeros:
            if pow10 == 0:
                printing_zeros = True
            else:
                if format == 1:
                    buf += " "
                continue
        if quot != 0 or printing_zeros:
            buf += chr(ord("0") + quot)
    return buf


def render_trname_gmm(trainers: list[dict]) -> str:
    """Render ``trname.json.txt`` for ``trainers`` (then escape ``&``).

    The template only uses ``{% set %}``, ``## for``, ``fmtInt(i, 5, 2)``
    and ``{{ }}`` substitution, so a purpose-built renderer replaces a full
    inja port; ``test_prepare_game_headers`` pins it against hand-built JSON.
    """
    lines = ['<?xml version="1.0"?>', '<body language="English">']
    for i, trainer in enumerate(trainers):
        lines.append(f'\t<row id="msg_0729_{fmt_int(i, 5, 2)}" index="{i}">')
        lines.append('\t\t<attribute name="window_context_name">used</attribute>')
        lines.append(f'\t\t<language name="English">{trainer["name"]}</language>')
        lines.append("\t</row>")
    lines.append("</body>")
    # files/msgdata/msg.mk: $(SED) -i 's/&/&amp;/g'
    return "\n".join(lines).replace("&", "&amp;") + "\n"


def gmm_rowname_pref(filename: str) -> str:
    """The ``rowname_pref`` fallback for rows without ``id`` (GMM::FromFile).

    Upstream quirk, ported literally: the length bound is the position of the
    first ``.`` in the *full* filename but is applied to the already-sliced
    basename, so for normal ``files/.../<base>.gmm`` paths the whole basename
    (extension included) survives.
    """
    base = filename[filename.rfind("/") + 1:]
    dot = filename.find(".")
    if dot < 0:
        return base
    # C++ substr clamps: a bound past the end keeps the whole basename.
    return base[:dot] if dot < len(base) else base


def parse_gmm_rows(gmm_path: str, text: str) -> list[tuple[str, bytes]]:
    """``(id, message-bytes)`` per ``<row>`` in document order (FromFile).

    Garbage rows take the Japanese text blanked to spaces, byte for byte.
    """
    root = ET.fromstring(text)
    body = root if root.tag == "body" else root.find("body")
    if body is None:
        raise ValueError(f"{gmm_path}: no <body>")
    rows = []
    serial = 0
    for row in body.iter("row"):
        english = ""
        japanese = ""
        garbage = False
        for lang in row.iter("language"):
            if lang.get("name") == "English":
                english = lang.text or ""
            if lang.get("name") == "日本語":
                japanese = lang.text or ""
        for attr in row.iter("attribute"):
            if attr.get("name") == "window_context_name" and (attr.text or "") == "garbage":
                garbage = True
        message = japanese if garbage else english
        data = message.encode("utf-8")
        if garbage:
            data = b" " * len(data)
        row_id = row.get("id") or f"{gmm_rowname_pref(gmm_path)}_{serial:05d}"
        rows.append((row_id, data))
        serial += 1
    return rows


def split_message(data: bytes, limit: int = 300) -> list[bytes]:
    """Port of ``GMM::SplitMessage`` (byte-based ``substr`` chunks)."""
    return [data[i:i + limit] for i in range(0, len(data), limit)]


def render_msg_header(header_rel: str, rows: list[tuple[str, bytes]]) -> bytes:
    """Byte port of ``GMM::WriteGmmHeader`` for one ``msg_*.h``.

    Returns raw bytes: message chunks are byte-split like C++ ``substr``,
    which can cut multibyte UTF-8 sequences, so the result is not always
    valid UTF-8 (msgenc's output isn't either).
    """
    guard = msgenc_guard(header_rel)
    out: list[bytes] = [
        b"/***************************************************",
        b" * WARNING: This file was autogenerated by msgenc. *",
        b" *               DO NOT MODIFY                     *",
        b" ***************************************************/",
        b"",
        f"#ifndef MSGENC_{guard}".encode(),
        f"#define MSGENC_{guard}".encode(),
        b"",
    ]
    for i, (row_id, data) in enumerate(rows):
        for chunk in split_message(data):
            out.append(b"// " + chunk)
        out.append(f"#define {row_id} {i}".encode())
    out += [b"", f"#endif //MSGENC_{guard}".encode(), b""]
    return b"\n".join(out)


# ---------------------------------------------------------------------------
# naix member sources: one derivation per Makefile rule family


def parse_make_list(mk_text: str, var: str) -> list[str]:
    """Entries of a ``VAR := a b \\`` continuation list in a ``files`` .mk."""
    text = re.sub(r"\\\n", " ", mk_text)
    for line in text.splitlines():
        m = re.match(rf"^{re.escape(var)}\s*:=\s*(.*)$", line.strip())
        if m:
            return m.group(1).split()
    raise ValueError(f"{var} not found")


def default_excludes() -> list[str]:
    """nitroarc's built-in excludes (prepare_exclusions)."""
    return [".*ignore", ".*keep", ".*order"]


def dir_files(root: Path, rel: str) -> list[str]:
    """Checked-in files under ``rel`` (paths relative to ``rel``)."""
    base = root / rel
    return sorted(p.relative_to(base).as_posix()
                  for p in base.rglob("*") if p.is_file())


def _checked_in_members(root: Path, rel: str, ignore_file: str | None,
                        extra_excludes: list[str] | None = None) -> list[str]:
    """Checked-in files surviving the excludes (diagnostic aid)."""
    excludes = default_excludes() + list(extra_excludes or [])
    if ignore_file is not None:
        ignore = root / rel / ignore_file
        if ignore.is_file():
            excludes += read_dotfile(ignore)
    return [m for m in dir_files(root, rel) if not _excluded(m, excludes)]


def generated_outputs(root: Path, narc_rel: str) -> list[str]:
    """Build-time-only members for nitroarc packs, derived from ``files``.

    ``.mk`` rules; each entry documents the rule it mirrors. Checked-in
    files come from the directory walk in :func:`nitro_members`.
    """
    msgdir = "files/msgdata/msg"
    if narc_rel == "files/msgdata/msg.narc":
        # msg.mk: MSGFILE_BIN, one .bin per .gmm (incl. generated 0729).
        return sorted(p.with_suffix(".bin").name
                      for p in (root / msgdir).glob("*.gmm"))
    if narc_rel in ("files/battledata/script/move_script.narc",
                    "files/battledata/script/subscript.narc",
                    "files/battledata/script/effect_script.narc",
                    "files/fielddata/script/scr_seq.narc"):
        # move_seq.mk / subscript.mk / effect_seq.mk / scr_seq.mk:
        # SCRIPT_BINS, one .bin per checked-in .s.
        stem = narc_rel.removesuffix(".narc")
        return sorted(p.with_suffix(".bin").name
                      for p in (root / stem).glob("*.s"))
    if narc_rel == "files/fielddata/eventdata/zone_event.narc":
        # zone_event.mk: ZONE_EVENT_BIN, one .bin per checked-in .json.
        stem = narc_rel.removesuffix(".narc")
        return sorted(p.with_suffix(".bin").name
                      for p in (root / stem).glob("*.json"))
    if narc_rel == "files/data/mmodel/mmodel.narc":
        # mmodel.mk: MMODEL_BINS, one .bin per checked-in .json
        # (NSBTX/NSBMD ship checked in).
        stem = narc_rel.removesuffix(".narc")
        return sorted(p.with_suffix(".bin").name
                      for p in (root / stem).glob("*.json"))
    if narc_rel == "files/data/resdat.narc":
        # resdat.mk: DATA_RESDAT_BIN, one .bin per checked-in .json.
        stem = narc_rel.removesuffix(".narc")
        return sorted(p.with_suffix(".bin").name
                      for p in (root / stem).glob("*.json"))
    if narc_rel == "files/data/gs_areawindow.narc":
        # gs_areawindow.mk: NCGR+NCLR per checked-in .png.
        stem = narc_rel.removesuffix(".narc")
        out = []
        for p in sorted((root / stem).glob("*.png")):
            out += [p.with_suffix(".NCGR").name, p.with_suffix(".NCLR").name]
        return out
    if narc_rel == "files/poketool/icongra/poke_icon/poke_icon.narc":
        # poke_icon.mk: NCLR per .pal, fixed NANR/NCER trios, NCGR per .png.
        stem = (root / narc_rel).parent
        out = [p.with_suffix(".NCLR").name for p in sorted(stem.glob("*.pal"))]
        out += ["poke_icon_00000001.NANR", "poke_icon_00000003.NANR",
                "poke_icon_00000005.NANR", "poke_icon_00000002.NCER",
                "poke_icon_00000004.NCER", "poke_icon_00000006.NCER"]
        out += [p.with_suffix(".NCGR").name for p in sorted(stem.glob("*.png"))]
        return out
    if narc_rel == "files/poketool/pokefoot/pokefoot.narc":
        # pokefoot.mk: POKEFOOT_FILES, static trio plus .NCGR.lz per .png.
        # The narc is named for its directory, so the stem is the dir.
        stem = root / narc_rel.removesuffix(".narc")
        out = ["pokefoot_00000000.NCLR", "pokefoot_00000001.NANR.lz",
               "pokefoot_00000002.NCER.lz"]
        out += [p.with_suffix(".NCGR.lz").name
                for p in sorted(stem.glob("pokefoot_*.png"))]
        return out
    raise KeyError(f"no generated-output rule for {narc_rel}")


def pokegra_build_members(root: Path) -> list[str]:
    """Members of the pokegra build dir (pokegra.mk ``%-NN`` mapping).

    Palettes exist when the male sprite is non-empty, else fall back to the
    female sprite (``test -s``); the two ``0000`` palettes are always copied.
    """
    sprites = root / "files/poketool/pokegra/pokegra"
    out = ["0000-04.NCLR", "0000-05.NCLR"]
    for number in sorted(p.name for p in sprites.iterdir() if p.is_dir()):
        get = lambda *parts: sprites / number / Path(*parts)  # noqa: E731
        if get("female", "back.png").is_file():
            out.append(f"{number}-00.NCGR")
        if get("male", "back.png").is_file():
            out.append(f"{number}-01.NCGR")
        if get("female", "front.png").is_file():
            out.append(f"{number}-02.NCGR")
        if get("male", "front.png").is_file():
            out.append(f"{number}-03.NCGR")
        front = get("male", "front.png")
        if not (front.is_file() and front.stat().st_size > 0):
            front = get("female", "front.png")
        if front.is_file() and front.stat().st_size > 0:
            out.append(f"{number}-04.NCLR")
        back = get("male", "back.png")
        if not (back.is_file() and back.stat().st_size > 0):
            back = get("female", "back.png")
        if back.is_file() and back.stat().st_size > 0:
            out.append(f"{number}-05.NCLR")
    # The recipe copies two palettes over same-named generated outputs; the
    # directory (and hence the archive) holds each name once.
    return list(dict.fromkeys(out))


def otherpoke_build_members(root: Path) -> list[str]:
    """Members of the otherpoke build dir (output column of otherpoke.txt)."""
    out = []
    for line in (root / "files/poketool/pokegra/otherpoke.txt").read_text(
            encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 2:
            out.append(Path(parts[1]).name)
    return out


# .mk variable holding the static member list, per nitroarc narc.
STATIC_LISTS = {
    "files/application/annon/puzzle_gra.narc":
        ("files/application/annon/puzzle_gra.mk", "ALPH_PUZZLE_FILES"),
    "files/application/guinness.narc":
        ("files/application/guinness.mk", "GUINNESS_FILES"),
    "files/application/pokegear/configure/pgconf_gra.narc":
        ("files/application/pokegear/configure/pgconf_gra.mk", "PGCONF_GRA_FILES"),
    "files/application/pokegear/map/pgmap_gra.narc":
        ("files/application/pokegear/map/pgmap_gra.mk", "PGMAP_GRA_FILES"),
    "files/application/pokegear/pgear_gra.narc":
        ("files/application/pokegear/pgear_gra.mk", "PGEAR_GRA_FILES"),
    "files/application/pokegear/phone/pgphone_gra.narc":
        ("files/application/pokegear/phone/pgphone_gra.mk", "PGPHONE_GRA_FILES"),
    "files/application/pokegear/radio/pgradio_gra.narc":
        ("files/application/pokegear/radio/pgradio_gra.mk", "PGRADIO_GRA_FILES"),
    "files/graphic/plist_gra.narc":
        ("files/graphic/plist_gra.mk", "PLIST_GRA_FILES"),
    "files/graphic/zukan_gra.narc":
        ("files/graphic/zukan_gra.mk", "ZUKAN_GRA_FILES"),
    "files/data/namein.narc":
        ("files/data/namein.mk", "DATA_NAMEIN_FILES"),
    "files/application/voltorb_flip.narc":
        ("files/application/voltorb_flip.mk", "VOLTORB_FLIP_FILES"),
    "files/demo/opening/gs_opening.narc":
        ("files/demo/opening/gs_opening.mk", "GSOPENING_FILES"),
    "files/demo/title/titledemo.narc":
        ("files/demo/title/titledemo.mk", "TITLEDEMO_DEPS"),
    "files/demo/legend.narc":
        ("files/demo/legend.mk", "LEGEND_DEMO_FILES"),
    "files/demo/intro/intro.narc":
        ("files/demo/intro/intro.mk", "INTRO_DEMO_FILES"),
}

# Narcs packed from an explicit prerequisite list written inline in the rule.
INLINE_LISTS = {
    "files/application/choose_starter/choose_starter_main_res.narc",
    "files/application/choose_starter/choose_starter_sub_res.narc",
}


def static_list_members(root: Path, narc_rel: str) -> list[str]:
    """Members from a static ``_FILES``/``_DEPS`` list (basenames)."""
    mk_rel, var = STATIC_LISTS[narc_rel]
    entries = parse_make_list((root / mk_rel).read_text(encoding="utf-8"), var)
    return [e.split("/")[-1] for e in entries]


def inline_list_members(root: Path, narc_rel: str) -> list[str]:
    """Members from prerequisites written inline in the narc rule."""
    mk_rel = "files/application/choose_starter/choose_starter.mk"
    text = (root / mk_rel).read_text(encoding="utf-8")
    joined = re.sub(r"\\\n", " ", text)
    members: list[str] = []
    for line in joined.splitlines():
        m = re.match(r"^\$\(CHOOSE_STARTER_(?:MAIN|SUB)_NARC\):\s*(.*)$",
                     line.strip())
        if not m:
            continue
        files = [tok for tok in m.group(1).split() if tok.startswith("$(")]
        target = ("choose_starter_main_res" if "MAIN" in line else
                  "choose_starter_sub_res")
        if target in narc_rel:
            members = [tok.split("/")[-1].rstrip(")") for tok in files]
    if not members:
        raise ValueError(f"no inline prerequisites for {narc_rel}")
    return members


def nitro_members(root: Path, narc_rel: str) -> list[str]:
    """Full member list for one nitroarc ``--index-namespace`` narc."""
    if narc_rel == "files/poketool/pokegra/pokegra.narc":
        return nitroarc_members_synthetic(pokegra_build_members(root), [])
    if narc_rel == "files/poketool/pokegra/otherpoke.narc":
        return nitroarc_members_synthetic(otherpoke_build_members(root), [])
    if narc_rel in ("files/application/zukanlist/zkn_data/zukan_hw_data_gold.narc",
                    "files/application/zukanlist/zkn_data/zukan_hw_data_silver.narc",
                    "files/application/zukanlist/zkn_data/zukan_hw_data.narc"):
        # zukan_hw_data.mk: packs zukan_hw_data_0.bin plus the
        # zukan_hw_data_1.bin symlink; --exclude drops the versioned copies.
        return ["zukan_hw_data_0.bin", "zukan_hw_data_1.bin"]
    stem = narc_rel.removesuffix(".narc")
    input_dir = root / stem
    order_file = input_dir / ".narcorder"
    order = read_dotfile(order_file) if order_file.is_file() else None
    excludes = default_excludes()
    ignore_file = input_dir / ".narcignore"
    if ignore_file.is_file():
        excludes += read_dotfile(ignore_file)
    try:
        generated = generated_outputs(root, narc_rel)
    except KeyError:
        generated = []
    # Synthetic lists may name a file twice (pokegra's copied palettes land
    # on generated names); the directory walk nitroarc performs cannot, so
    # dedupe here. Explicit .narcorder entries keep file order verbatim.
    generated = list(dict.fromkeys(generated))
    if narc_rel in STATIC_LISTS:
        generated += static_list_members(root, narc_rel)
    if narc_rel in INLINE_LISTS:
        generated += inline_list_members(root, narc_rel)
    if order is not None:
        # Explicit entries keep file order; everything else follows sorted.
        have = set(order)
        rest = sorted(
            set(generated) | set(_checked_in_members(root, stem, None, excludes))
            - have,
            key=lambda s: s.encode("utf-8"),
        )
        return list(order) + [m for m in rest if not _excluded(m, excludes)]
    present = set(_checked_in_members(root, stem, None, excludes)) | set(generated)
    return sorted((m for m in present if not _excluded(m, excludes)),
                  key=lambda s: s.encode("utf-8"))


def nitroarc_members_synthetic(members: list[str],
                               excludes: list[str]) -> list[str]:
    """Member order for a synthetic (generated-only) input directory."""
    return sorted((m for m in members if not _excluded(m, excludes)),
                  key=lambda s: s.encode("utf-8"))


# ---------------------------------------------------------------------------
# o2narc / csv2bin member counts and names


def num_species(root: Path) -> int:
    """``NUM_SPECIES`` via ``SPECIES_ROTOM_MOW`` (personal/evo sizing)."""
    path = root / "include/constants/species.h"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"{path} not found") from exc
    m = re.search(r"#define\s+SPECIES_ROTOM_MOW\s+(\d+)", text)
    if not m:
        raise ValueError("SPECIES_ROTOM_MOW not found")
    return int(m.group(1))


def json_len(root: Path, rel: str, key: str) -> int:
    """Length of a top-level JSON array (o2narc member counts)."""
    with open(root / rel, encoding="utf-8") as f:
        return len(json.load(f)[key])


def csv_rows(root: Path, rel: str) -> int:
    """Data rows in a csv2bin CSV (header excluded)."""
    with open(root / rel, encoding="utf-8", newline="") as f:
        return sum(1 for line in f if line.strip()) - 1


def zukan_enc_pairs(root: Path) -> int:
    """``gDungeons`` + ``gOverworlds`` + per-method/mon encounter symbols."""
    with open(root / "files/application/zukanlist/zkn_data/zukan_enc.json",
              encoding="utf-8") as f:
        encounters = json.load(f)["encounters"]
    return 2 + sum(len(mons) for mons in encounters.values())


def zukan_data_names(root: Path) -> list[str]:
    """``-N`` symbol names for zukan_data(.naix): 11 fixed plus sort orders."""
    fixed = ["mon_stats_height", "mon_stats_weight", "mon_stats_body_style",
             "mon_stats_player_scale_f", "mon_stats_mon_scale_f",
             "mon_stats_player_scale_m", "mon_stats_mon_scale_m",
             "mon_stats_player_ypos_f", "mon_stats_mon_ypos_f",
             "mon_stats_player_ypos_m", "mon_stats_mon_ypos_m"]
    with open(root / "files/application/zukanlist/zkn_data/zukan_data.json",
              encoding="utf-8") as f:
        sorting = json.load(f)["sorting"]
    names = list(fixed)
    for group in sorting:
        for subgroup in group["options"]:
            names.append(f"sort_order_{group['type']}_{subgroup['id']}")
    return names


# (naix path, kind, payload): kind selects the renderer and member source.
O2NARC_NUMERIC = {
    # naix path -> ("json", rel, key) | ("species_plus_one",) | ("const", n)
    "files/poketool/trainer/trdata.naix": ("json", "files/poketool/trainer/trainers.json", "trainers"),
    "files/poketool/trainer/trpoke.naix": ("json", "files/poketool/trainer/trainers.json", "trainers"),
    "files/poketool/personal/personal.naix": ("json", "files/poketool/personal/personal.json", "baseStats"),
    "files/poketool/personal/evo.naix": ("species_plus_one",),
    "files/fielddata/encountdata/g_enc_data.naix": ("json", "files/fielddata/encountdata/gs_enc_data.json", "encounters"),
    "files/fielddata/encountdata/s_enc_data.naix": ("json", "files/fielddata/encountdata/gs_enc_data.json", "encounters"),
    "files/data/photo_data.naix": ("json", "files/data/photo_data.json", "photos"),
    "files/arc/ppark.naix": ("const", 1),
    "files/arc/headbutt.heartgold.us.naix": ("json", "files/arc/headbutt.json", "tables"),
    "files/arc/headbutt.soulsilver.us.naix": ("json", "files/arc/headbutt.json", "tables"),
    "files/application/zukanlist/zkn_data/zukan_enc_gold.naix": ("zukan_enc",),
    "files/application/zukanlist/zkn_data/zukan_enc_silver.naix": ("zukan_enc",),
}

CSV2BIN_NAIX = {
    "files/poketool/personal/growtbl.naix": "files/poketool/personal/growtbl.csv",
    "files/itemtool/itemdata/item_data.naix": "files/itemtool/itemdata/item_data.csv",
    "files/fielddata/tsurepoke/tp_param.naix": "files/fielddata/tsurepoke/tp_param.csv",
}

SHORTNAMES = ("gold", "silver")


def strip_shortname(text: str, shortname: str) -> str:
    """The ``sed 's/_<shortname>//g'`` normalisation (case-sensitive)."""
    return text.replace(f"_{shortname}", "")


# ---------------------------------------------------------------------------
# Driver


def check_root(root: Path) -> None:
    for name in ("Makefile", "common.mk", "filesystem.mk", "main.lsf",
                 "files/msgdata/msg.mk"):
        if not (root / name).is_file():
            raise SystemExit(f"{root}: {name} not found (not an HGSS checkout?)")


def msg_gmms(root: Path) -> list[Path]:
    """All message sources: checked-in ``*.gmm`` plus generated 0729."""
    return sorted((root / "files/msgdata/msg").glob("*.gmm"))


def generate_fx_const(root: Path) -> tuple[str, bool]:
    rel = "lib/include/nitro/fx/fx_const.h"
    csv_text = (root / "tools/gen_fx_consts/fx_const.csv").read_text(
        encoding="utf-8")
    return rel, write_if_changed(root / rel, render_fx_const_h(csv_text, rel))


def generate_trname_gmm(root: Path) -> tuple[str, bool]:
    rel = "files/msgdata/msg/msg_0729.gmm"
    with open(root / "files/poketool/trainer/trainers.json",
              encoding="utf-8") as f:
        trainers = json.load(f)["trainers"]
    return rel, write_if_changed(root / rel, render_trname_gmm(trainers))


def generate_msg_headers(root: Path) -> list[tuple[str, bool]]:
    results = []
    for gmm in msg_gmms(root):
        rel = f"files/msgdata/msg/{gmm.stem}.h"
        rows = parse_gmm_rows(
            f"files/msgdata/msg/{gmm.name}",
            gmm.read_text(encoding="utf-8"))
        results.append((rel, write_bytes_if_changed(
            root / rel, render_msg_header(rel, rows))))
    return results


def o2narc_count(root: Path, spec: tuple) -> int:
    if spec[0] == "json":
        return json_len(root, spec[1], spec[2])
    if spec[0] == "species_plus_one":
        return num_species(root) + 1
    if spec[0] == "const":
        return int(spec[1])
    if spec[0] == "zukan_enc":
        return zukan_enc_pairs(root)
    raise ValueError(f"bad o2narc spec {spec!r}")


def generate_o2narc_naix(root: Path) -> list[tuple[str, bool]]:
    results = []
    for naix_rel, spec in O2NARC_NUMERIC.items():
        narc_rel = naix_rel.removesuffix(".naix") + ".narc"
        stem = Path(naix_rel).name.removesuffix(".naix")
        names = o2narc_numeric_names(stem, o2narc_count(root, spec))
        results.append((naix_rel, write_if_changed(
            root / naix_rel, render_o2narc_naix(narc_rel, names))))
    for name in ("zukan_data", "zukan_data_gira"):
        naix_rel = f"files/application/zukanlist/zkn_data/{name}.naix"
        narc_rel = naix_rel.removesuffix(".naix") + ".narc"
        names = zukan_data_names(root)
        results.append((naix_rel, write_if_changed(
            root / naix_rel, render_o2narc_naix(narc_rel, names))))
    return results


def generate_csv2bin_naix(root: Path) -> list[tuple[str, bool]]:
    results = []
    for naix_rel, csv_rel in CSV2BIN_NAIX.items():
        narc_rel = naix_rel.removesuffix(".naix") + ".narc"
        count = csv_rows(root, csv_rel)
        results.append((naix_rel, write_if_changed(
            root / naix_rel, render_csv2bin_naix(narc_rel, count))))
    return results


def generate_nitroarc_naix(root: Path) -> list[tuple[str, bool]]:
    # Every nitroarc --index-namespace narc whose naix the build emits
    # (checked-in narcs with empty rules, the index-less pms_aikotoba pack
    # and the flag-less safari_enc pack emit none).
    narc_list = [
        "files/application/annon/puzzle_gra.narc",
        "files/application/choose_starter/choose_starter_main_res.narc",
        "files/application/choose_starter/choose_starter_sub_res.narc",
        "files/application/guinness.narc",
        "files/application/pokegear/configure/pgconf_gra.narc",
        "files/application/pokegear/map/pgmap_gra.narc",
        "files/application/pokegear/pgear_gra.narc",
        "files/application/pokegear/phone/pgphone_gra.narc",
        "files/application/pokegear/radio/pgradio_gra.narc",
        "files/application/record/record.narc",
        "files/application/voltorb_flip.narc",
        "files/battledata/script/effect_script.narc",
        "files/battledata/script/move_script.narc",
        "files/battledata/script/subscript.narc",
        "files/data/gs_areawindow.narc",
        "files/data/mmodel/mmodel.narc",
        "files/data/namein.narc",
        "files/data/resdat.narc",
        "files/data/sbox_gra.narc",
        "files/demo/intro/intro.narc",
        "files/demo/legend.narc",
        "files/demo/opening/gs_opening.narc",
        "files/demo/title/titledemo.narc",
        "files/fielddata/eventdata/zone_event.narc",
        "files/fielddata/graphic/preview_graphic/preview_graphic.narc",
        "files/fielddata/mapmatrix/map_matrix.narc",
        "files/fielddata/script/scr_seq.narc",
        "files/graphic/camera_viewfinder.narc",
        "files/graphic/plist_gra.narc",
        "files/graphic/zukan_gra.narc",
        "files/itemtool/itemdata/item_icon.narc",
        "files/msgdata/msg.narc",
        "files/poketool/icongra/poke_icon/poke_icon.narc",
        "files/poketool/pokefoot/pokefoot.narc",
        "files/poketool/pokegra/otherpoke.narc",
        "files/poketool/pokegra/pokegra.narc",
    ]
    results = []
    for narc_rel in narc_list:
        naix_rel = narc_rel.removesuffix(".narc") + ".naix"
        members = nitro_members(root, narc_rel)
        results.append((naix_rel, write_if_changed(
            root / naix_rel, render_nitroarc_naix(narc_rel, members))))
    for shortname in SHORTNAMES:
        narc_rel = ("files/application/zukanlist/zkn_data/"
                    f"zukan_hw_data_{shortname}.narc")
        naix_rel = narc_rel.removesuffix(".narc") + ".naix"
        members = nitro_members(root, narc_rel)
        results.append((naix_rel, write_if_changed(
            root / naix_rel, render_nitroarc_naix(narc_rel, members))))
    return results


def generate_alias_naix(root: Path, shortname: str) -> list[tuple[str, bool]]:
    """Version-agnostic aliases (the ``sed`` normalisation in the .mk rules)."""
    results = []
    pairs = [
        (f"files/application/zukanlist/zkn_data/zukan_enc_{shortname}.naix",
         "files/application/zukanlist/zkn_data/zukan_enc.naix"),
        (f"files/application/zukanlist/zkn_data/zukan_hw_data_{shortname}.naix",
         "files/application/zukanlist/zkn_data/zukan_hw_data.naix"),
    ]
    for src_rel, dst_rel in pairs:
        text = (root / src_rel).read_text(encoding="utf-8")
        results.append((dst_rel, write_if_changed(
            root / dst_rel, strip_shortname(text, shortname))))
    return results


def generate_overlay_ids(root: Path, ids_output: Path | None,
                         header: Path | None) -> list[tuple[str, bool]]:
    results = []
    if ids_output is None and header is None:
        return results
    names = go.overlay_names((root / "main.lsf").read_text(encoding="utf-8"))
    if ids_output is not None:
        results.append((str(ids_output), write_if_changed(
            ids_output, go.render_ids(names))))
    if header is not None:
        results.append((str(header), write_if_changed(
            header, go.render_ids_header(names))))
    return results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path("build/upstream"),
                        help="decomp checkout to generate into")
    parser.add_argument("--shortname", default="silver",
                        choices=list(SHORTNAMES),
                        help="version for the zukan alias naixes "
                             "(matches the last ROM the observed build made)")
    parser.add_argument("--ids-output", type=Path, default=None,
                        help="write the GNU ld overlay-ID script here")
    parser.add_argument("--header", type=Path, default=None,
                        help="write the overlay-ID C header here")
    args = parser.parse_args(argv)

    root = args.root
    check_root(root)

    changed = 0
    total = 0

    def record(results: list[tuple[str, bool]]) -> None:
        nonlocal changed, total
        for _, was_changed in results:
            total += 1
            changed += was_changed

    fx_rel, fx_changed = generate_fx_const(root)
    total += 1
    changed += fx_changed
    print(f"fx_const: {fx_rel} ({'updated' if fx_changed else 'unchanged'})")

    tr_rel, tr_changed = generate_trname_gmm(root)
    total += 1
    changed += tr_changed
    print(f"trname: {tr_rel} ({'updated' if tr_changed else 'unchanged'})")

    msg_results = generate_msg_headers(root)
    record(msg_results)
    print(f"msg headers: {len(msg_results)} "
          f"({sum(1 for _, c in msg_results if c)} updated)")

    nitro_results = generate_nitroarc_naix(root)
    record(nitro_results)
    print(f"nitroarc naix: {len(nitro_results)} "
          f"({sum(1 for _, c in nitro_results if c)} updated)")

    o2_results = generate_o2narc_naix(root)
    record(o2_results)
    print(f"o2narc naix: {len(o2_results)} "
          f"({sum(1 for _, c in o2_results if c)} updated)")

    csv_results = generate_csv2bin_naix(root)
    record(csv_results)
    print(f"csv2bin naix: {len(csv_results)} "
          f"({sum(1 for _, c in csv_results if c)} updated)")

    alias_results = generate_alias_naix(root, args.shortname)
    record(alias_results)
    print(f"alias naix: {len(alias_results)} "
          f"({sum(1 for _, c in alias_results if c)} updated)")

    overlay_results = generate_overlay_ids(root, args.ids_output, args.header)
    for path, was_changed in overlay_results:
        total += 1
        changed += was_changed
        print(f"overlay ids: {path} ({'updated' if was_changed else 'unchanged'})")

    print(f"done: {total} files, {changed} updated under {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
