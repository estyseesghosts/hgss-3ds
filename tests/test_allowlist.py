"""The cross-check allowlist is exact, not a blanket.

tools/symbol_crosscheck.py reports any reference symbol the PC ELF does not
define; the allowlist is the record of symbols the port deliberately replaces,
stubs or drops. These checks keep it from silently growing into wildcards that
would hide a source file left out of the build or one of the MSL helpers
hgss/compat/src/msl_runtime.c now implements.
"""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools import symbol_crosscheck as sc  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
ALLOWLIST = REPO / "tools" / "symbol_crosscheck_allowlist.txt"

# Symbols the host32 link defines. They must never need an allowlist row.
IMPLEMENTED = {
    "_fadd", "_fdiv", "_ffix", "_fflt", "_f_lltof", "_f_ulltof",
    "_ll_mul", "_ll_udiv", "_s32_div_f", "strnicmp", "__lower_mapC",
    "_ZN5BytesD1Ev", "_ZN5DequeD1Ev", "_ZnamRKSt9nothrow_t", "_ZdaPv",
    "_ZNSt15__vector_commonILb1EE18throw_length_errorEv", "_ZSt7nothrow",
    "MB_Init", "MB_End", "MB_GetSegmentLength", "MB_ReadSegment",
    "MB_RegisterFile", "MB_CommResponseRequest", "MB_DisconnectChild",
    "MB_CommIsBootable", "MB_CommGetChildUser", "MB_CommSetParentStateCallback",
    "MB_SetParentCommParam", "MB_StartParentFromIdle",
    "gPokewalkerIrImageSizes", "gPokewalkerIrStrollSizes",
    "gPokewalkerIrMessageQueue", "gPokewalkerIrTransfer",
    "gPokewalkerIrSendBuffer",
}


class AllowlistTests(unittest.TestCase):
    def setUp(self):
        self.entries = sc.load_allowlist(ALLOWLIST)

    def test_every_row_is_an_exact_name_with_a_reason(self):
        # parse_allowlist already rejects a missing reason; this also rejects
        # a pattern, so one row cannot hide a family of symbols.
        for entry in self.entries:
            self.assertNotIn("*", entry.pattern, entry.pattern)
            self.assertNotIn("?", entry.pattern, entry.pattern)
            self.assertNotIn("[", entry.pattern, entry.pattern)
            self.assertTrue(entry.reason.strip(), entry.pattern)

    def test_implemented_symbols_are_not_allowlisted(self):
        patterns = {entry.pattern for entry in self.entries}
        self.assertEqual(patterns & IMPLEMENTED, set())

    def test_no_symbol_is_listed_twice(self):
        patterns = [entry.pattern for entry in self.entries]
        self.assertEqual(len(patterns), len(set(patterns)))


if __name__ == "__main__":
    unittest.main()
