"""Guard against raw DS addresses in the port's game code.

The port's DS memory areas are ordinary arrays (patch 1301), so a game-source
address written as a literal points at unmapped memory on the 3DS and on the
PC.  The battle crash in session 11 was exactly that: on the frame a wild
encounter starts, ``WirelessStatusIcon_LoadChar`` read DISPCNT through
``0x04000000`` to pick the OBJ character mapping, and log-10 faulted there
with ``r3 = 0x04000000`` right after the battle overlays loaded.  Patch 1905
converted that literal and the rest of the class to ``reg_*``/``HW_*``.

The scan models the port's preprocessor for the one macro that decides the
branch (``SDK_PORT_3DS``), so the DS ``#else`` copies of these lines are
ignored, and checks every line the port actually compiles.  It covers
``src/**/*.c`` and the ARM7 sound driver under ``sub/lib/src`` -- what the
port links.  The SDK sources are patched by their own stack, and the
wireless ``sub/`` files are not built (the port stubs Wi-Fi).

When a new raw address is genuinely required, add an explicit entry to
``ALLOWED`` with the reason, not a bare line number.
"""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOT = REPO / "build" / "upstream"

# tools/hgss_game.py's PORT_DEFINES plus the 3DS target's own macro.
DEFINED = {
    "SDK_PORT_3DS": 1,
    "SDK_ARM9": 1,
    "__3DS__": 1,
    "__GNUC__": 1,
}

DIRECTIVE = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")
ADDRESS = re.compile(r"0x0[2-7][0-9A-Fa-f]{6}")
# The address immediately follows a pointer cast: (u32 *)0x..., (void *)0x...,
# ((vu16 *)0x...  or  *(u32 *)0x...
CAST = re.compile(
    r"\(\s*(?:const\s+)?(?:volatile\s+)?(?:v)?u(?:8|16|32)\s*\*[^()]*\)\s*$"
    r"|\(\s*void\s*\*[^()]*\)\s*$"
    r"|\(\s*u32\s*\*[^()]*\)\s*$"
)

# Files allowed to keep a DS literal, with the reason.
ALLOWED_FILES = {
    # The port branch #undefs and redefines every sound-register macro it
    # uses (NitroPort_SoundRegisters), and REG_POWCNT2 is only touched by
    # SND_BeginSleep/EndSleep, whose caller, power_manager_driver.c, is not
    # built for the port.
    "sub/lib/src/sound_driver.c": "the port redefines its register macros",
}


def eval_cond(expr):
    """Evaluate the simple #if expressions the sources use under the port."""
    e = expr.strip()
    e = re.sub(
        r"defined\s*\(\s*(\w+)\s*\)",
        lambda m: "1" if m.group(1) in DEFINED else "0",
        e,
    )
    e = re.sub(
        r"defined\s+(\w+)",
        lambda m: "1" if m.group(1) in DEFINED else "0",
        e,
    )
    e = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: str(DEFINED.get(m.group(0), 0)), e)
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    try:
        return bool(eval(e, {"__builtins__": {}}, {}))
    except Exception:
        # An expression this model does not understand: keep the lines in the
        # scan, so an unknown guard can only cause a false positive.
        return True


def active_lines(text):
    """Yield (line number, line) for the lines the port preprocessor keeps."""
    stack = []  # [parent_active, some_branch_taken, active]
    active = True
    for number, line in enumerate(text.splitlines(), 1):
        match = DIRECTIVE.match(line)
        if match is None:
            if active:
                yield number, line
            continue
        kind, rest = match.group(1), match.group(2).strip()
        if kind in ("ifdef", "ifndef", "if"):
            if kind == "ifdef":
                cond = rest in DEFINED
            elif kind == "ifndef":
                cond = rest not in DEFINED
            else:
                cond = eval_cond(rest)
            stack.append([active, cond, active and cond])
            active = stack[-1][2]
        elif kind == "elif":
            if stack:
                parent, taken, _ = stack[-1]
                cond = (not taken) and eval_cond(rest)
                stack[-1] = [parent, taken or cond, parent and cond]
                active = stack[-1][2]
        elif kind == "else":
            if stack:
                parent, taken, _ = stack[-1]
                cond = not taken
                stack[-1] = [parent, True, parent and cond]
                active = stack[-1][2]
        else:  # endif
            if stack:
                stack.pop()
            active = stack[-1][2] if stack else True


def raw_addresses(text, path="<text>"):
    """The (line number, line) uses of a raw DS address in active code."""
    found = []
    for number, line in active_lines(text):
        code = line.split("//")[0]
        for match in ADDRESS.finditer(code):
            if CAST.search(code[: match.start()]) or re.match(
                r"^\s*\*\s*\(\s*(?:vu?32|vu?16|vu?8|u32|u16|u8)\s*\*\)\s*$",
                code[: match.start()],
            ):
                if any(path.endswith(p) for p in ALLOWED_FILES):
                    continue
                found.append((number, line.strip()))
    return found


def scan_tree(root):
    """The raw DS address uses in the sources the port compiles."""
    report = []
    sources = sorted((root / "src").rglob("*.c"))
    sources += sorted((root / "sub" / "lib" / "src").glob("sound_*.c"))
    for path in sources:
        for number, line in raw_addresses(
            path.read_text(errors="replace"), path.as_posix()
        ):
            report.append(
                f"{path.relative_to(root)}:{number}: {line[:120]}"
            )
    return sources, report


class PreprocessorModelTests(unittest.TestCase):
    def test_port_branch_is_scanned(self):
        text = (
            "#ifdef SDK_PORT_3DS\n"
            "    a = *(u32 *)0x04000000;\n"
            "#else\n"
            "    a = *(u32 *)0x04000000;\n"
            "#endif\n"
        )
        self.assertEqual(len(raw_addresses(text)), 1)

    def test_ds_branch_is_ignored(self):
        text = (
            "#ifndef SDK_PORT_3DS\n"
            "    a = *(u32 *)0x04000000;\n"
            "#else\n"
            "    a = 0;\n"
            "#endif\n"
        )
        self.assertEqual(raw_addresses(text), [])

    def test_if_defined_and_else(self):
        text = (
            "#if defined(SDK_PORT_3DS) && 1\n"
            "    a = reg_GX_DISPCNT;\n"
            "#else\n"
            "    a = *(u32 *)0x04000000;\n"
            "#endif\n"
        )
        self.assertEqual(raw_addresses(text), [])

    def test_numeric_constants_are_not_addresses(self):
        text = "    a = value * 0x02E90EDD + 1;\n"
        self.assertEqual(raw_addresses(text), [])

    def test_multiline_parameter_lists_are_scanned(self):
        text = (
            "    MI_CpuClearFast((void *)0x06840000, 0x20000);\n"
            "    b = ((u32 *)0x027E0000)[x] | 1;\n"
        )
        self.assertEqual(len(raw_addresses(text)), 2)


@unittest.skipUnless((ROOT / "main.lsf").is_file(), "needs build/upstream")
class RealSourcesTests(unittest.TestCase):
    def test_no_raw_ds_addresses_in_port_game_code(self):
        sources, report = scan_tree(ROOT)
        # A renamed tree must fail loudly rather than scanning nothing.
        self.assertGreater(len(sources), 500)
        self.assertEqual(
            report,
            [],
            "raw DS addresses in code the port compiles; use the port's "
            "HW_*/reg_* macros (patch 1905 did this for the battle path):\n"
            + "\n".join(report),
        )


if __name__ == "__main__":
    unittest.main()
