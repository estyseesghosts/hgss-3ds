"""Guard the compat assembly data files against x86-only GAS directives.

hgss/compat/src/*.c embed MWAS tables as `__asm__` strings, built for both the
host32 tests and the 3DS ARM7/ARM11 toolchains. GAS spells a section/symbol type
with `@` on x86, but `@` starts a comment on ARM, so `.section .rodata,"a",@progbits`
is "junk at end of line" and `.type sym,@object` is "unrecognized symbol type".
The files must choose `%progbits`/`%object` under `__arm__` and keep the x86 text
otherwise. Synthetic checks only; no game data.
"""
import re
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
COMPAT_SRC = REPO / "hgss" / "compat" / "src"
X86_TYPE = re.compile(r"@(progbits|object)\b")
ARM_TYPE = re.compile(r"%(progbits|object)\b")
DIRECTIVE = re.compile(r"^\s*(#\s*(?:if|ifdef|ifndef|elif|else|endif))\b(.*)$")


def _is_arm_condition(kind, expr):
    """True/false if the true branch is taken with __arm__ defined, else None."""
    expr = expr.strip()
    if "__arm__" not in expr:
        return None
    negated = expr.startswith("!") or re.match(r"^not\b", expr) is not None
    if kind == "ifndef":
        negated = True
    return not negated


def _in_non_arm_branch(stack):
    """Whether the current line sits in a branch that is only taken without __arm__."""
    for entry in reversed(stack):
        arm_true = entry["arm_true"]
        if arm_true is None:
            continue
        if entry["branch"] == "true":
            return not arm_true
        if entry["branch"] == "else":
            return arm_true
    return False


def strip_comments(text):
    """Blank out C comments, keeping newlines so line numbers stay valid."""
    out = []
    i = 0
    in_block = in_line = in_str = False
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_block:
            if c == "*" and nxt == "/":
                out.append("  ")
                i += 2
                in_block = False
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
            continue
        if in_line:
            if c == "\n":
                out.append("\n")
                in_line = False
            else:
                out.append(" ")
            i += 1
            continue
        if in_str:
            out.append(c)
            if c == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            in_block = True
            continue
        if c == "/" and nxt == "/":
            out.append("  ")
            i += 2
            in_line = True
            continue
        if c == '"':
            in_str = True
        out.append(c)
        i += 1
    return "".join(out)


def scan(path):
    """Return (x86_directives, arm_directives, unguarded): line numbers of @ uses."""
    lines = strip_comments(path.read_text()).splitlines()
    stack = []
    x86, arm, unguarded = [], [], []
    for lineno, line in enumerate(lines, 1):
        m = DIRECTIVE.match(line)
        if m:
            token, rest = m.group(1).replace(" ", ""), m.group(2)
            if token in ("#if", "#ifdef", "#ifndef"):
                kind = token.lstrip("#")
                stack.append({"arm_true": _is_arm_condition(kind, rest) if kind in ("if", "ifndef")
                              else (True if rest.strip() == "__arm__" else None),
                              "branch": "true"})
            elif token == "#elif":
                if stack:
                    stack[-1]["branch"] = "elif"
            elif token == "#else":
                if stack:
                    stack[-1]["branch"] = "else"
            elif token == "#endif":
                if stack:
                    stack.pop()
            continue
        if X86_TYPE.search(line):
            x86.append(lineno)
            if not _in_non_arm_branch(stack):
                unguarded.append(lineno)
        if ARM_TYPE.search(line):
            arm.append(lineno)
    return x86, arm, unguarded


class CompatAsmDirectiveTests(unittest.TestCase):
    def setUp(self):
        self.files = sorted(COMPAT_SRC.glob("*.c"))
        self.assertTrue(self.files, f"no compat sources under {COMPAT_SRC}")

    def test_x86_type_syntax_only_in_a_non_arm_branch(self):
        for path in self.files:
            x86, arm, unguarded = scan(path)
            if not x86:
                continue
            self.assertEqual(
                unguarded, [],
                f"{path.name}: x86-only @progbits/@object outside a non-__arm__ "
                f"branch at lines {unguarded}")
            self.assertTrue(
                arm, f"{path.name}: has x86 type syntax but no %progbits/%object "
                      "ARM branch")
            self.assertTrue(
                any("__arm__" in line for line in path.read_text().splitlines()),
                f"{path.name}: x86 type syntax without an __arm__ guard")

    def test_arm_preprocessing_uses_percent(self):
        cc = shutil.which("cc")
        if cc is None:
            self.skipTest("no C compiler for the preprocessor check")
        for path in self.files:
            x86, _, _ = scan(path)
            if not x86:
                continue
            # Files that include headers (nns_tables_gfd.c) need the SDK include
            # path; the textual scan above already covers them.
            arm = subprocess.run([cc, "-E", "-P", "-D__arm__", str(path)],
                                 capture_output=True, text=True)
            host = subprocess.run([cc, "-E", "-P", "-U__arm__", str(path)],
                                  capture_output=True, text=True)
            if arm.returncode != 0 or host.returncode != 0:
                continue
            self.assertNotRegex(arm.stdout, r"@(progbits|object)",
                                f"{path.name}: __arm__ preprocessing kept an x86 directive")
            self.assertRegex(arm.stdout, r"%(progbits|object)",
                             f"{path.name}: __arm__ preprocessing has no % type")
            self.assertRegex(host.stdout, r"@(progbits|object)",
                             f"{path.name}: the non-ARM text changed")


if __name__ == "__main__":
    unittest.main()
