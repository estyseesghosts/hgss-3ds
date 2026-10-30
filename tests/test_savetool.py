"""Unit tests for tools/savetool.py. Synthetic images only: no game data."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import savetool as st  # noqa: E402

SIZE0, SIZE1 = 0x1F28, 0x2310  # arbitrary synthetic slot sizes, footer included


def put_slot(img, mirror, idx, count, fill, size, off, corrupt=False):
    body = bytes([fill]) * (size - 16)
    crc = st.crc16_ccitt(body)
    if corrupt:
        crc ^= 1
    base = mirror + off
    img[base:base + size - 16] = body
    img[base + size - 16:base + size] = struct.pack(st.FOOTER_FMT, count, size, st.SAVE_CHUNK_MAGIC, idx, crc)


def make_image(counts=(5, 4), sub_counts=None, corrupt=()):
    sub_counts = sub_counts or counts
    img = bytearray(b"\xFF" * st.SAVE_SIZE)
    off1 = (SIZE0 + 0xFF) & ~0xFF
    for m in (0, 1):
        put_slot(img, m * st.MIRROR_SIZE, 0, counts[m], 0x10 + m, SIZE0, 0, (m, 0) in corrupt)
        put_slot(img, m * st.MIRROR_SIZE, 1, sub_counts[m], 0x20 + m, SIZE1, off1, (m, 1) in corrupt)
    return bytes(img)


class Crc(unittest.TestCase):
    def test_check_value(self):
        # CRC-16/CCITT-FALSE check value
        self.assertEqual(st.crc16_ccitt(b"123456789"), 0x29B1)


class Normalize(unittest.TestCase):
    def test_raw_passthrough(self):
        raw = make_image()
        self.assertEqual(st.normalize(raw), raw)

    def test_strips_desmume_footer(self):
        raw = make_image()
        foot = st.DESMUME_SNIP + b"\x00" * 10 + st.DESMUME_END
        self.assertEqual(st.normalize(raw + foot), raw)

    def test_rejects_other_sizes(self):
        for n in (0, 512, 131072, st.SAVE_SIZE - 1, st.SAVE_SIZE + 1):
            with self.assertRaises(st.SaveError):
                st.normalize(b"\0" * n)

    def test_rejects_marker_without_snip(self):
        with self.assertRaises(st.SaveError):
            st.normalize(make_image() + st.DESMUME_END)


class Validate(unittest.TestCase):
    def test_good_newer_mirror_is_higher_count(self):
        rep = st.validate(make_image(counts=(5, 4)))
        self.assertEqual(rep["layout"], (SIZE0, SIZE1))
        self.assertEqual((rep["status_name"], rep["newer_mirror"]), ("IS_GOOD", 0))
        rep = st.validate(make_image(counts=(4, 5)))
        self.assertEqual(rep["newer_mirror"], 1)

    def test_counter_wraparound(self):
        rep = st.validate(make_image(counts=(0xFFFFFFFF, 0)))
        self.assertEqual(rep["newer_mirror"], 1)

    def test_explicit_layout(self):
        rep = st.validate(make_image(), (SIZE0, SIZE1))
        self.assertEqual(rep["status_name"], "IS_GOOD")

    def test_corrupt_newer_main_falls_back(self):
        rep = st.validate(make_image(counts=(5, 4), corrupt={(0, 0)}))
        # src/save.c (numGood_main == 1 && numGood_sub == 2): main's only good
        # mirror (1) has the same counter in sub, so the game loads it, flagged SLOT_FAIL.
        self.assertEqual((rep["status_name"], rep["newer_mirror"]), ("SLOT_FAIL", 1))
        rep = st.validate(make_image(counts=(5, 4), sub_counts=(5, 3), corrupt={(0, 0)}))
        self.assertEqual(rep["status_name"], "TOTAL_FAIL")  # counters disagree
        rep = st.validate(make_image(counts=(5, 4), corrupt={(0, 0), (0, 1)}))
        self.assertEqual((rep["status_name"], rep["newer_mirror"]), ("SLOT_FAIL", 1))

    def test_all_invalid_total_fail_or_missing(self):
        blank = bytes([0xFF]) * st.SAVE_SIZE
        rep = st.validate(blank, (SIZE0, SIZE1))
        self.assertEqual(rep["status_name"], "NOT_EXIST")

    def test_discovery_fails_on_blank(self):
        with self.assertRaises(st.SaveError):
            st.validate(bytes([0xFF]) * st.SAVE_SIZE)

    def test_wrong_size_rejected(self):
        with self.assertRaises(st.SaveError):
            st.validate(b"\0" * 100)


class Diff(unittest.TestCase):
    def test_block_runs(self):
        a = bytearray(0x5000)
        b = bytearray(a)
        b[0x1000] = 1
        b[0x2000] = 1
        b[0x4FFF] = 1
        self.assertEqual(list(st.diff_blocks(bytes(a), bytes(b), 0x1000)), [(0x1000, 0x2000), (0x4000, 0x1000)])

    def test_identical(self):
        self.assertEqual(list(st.diff_blocks(b"abc", b"abc", 2)), [])


class Cli(unittest.TestCase):
    def test_cli_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            raw = os.path.join(d, "a.sav")
            out = os.path.join(d, "b.sav")
            open(raw, "wb").write(make_image() + st.DESMUME_SNIP + st.DESMUME_END)
            self.assertEqual(st.main(["normalize", raw, "-o", out]), 0)
            self.assertEqual(st.main(["validate", out]), 0)
            self.assertEqual(st.main(["diff", out, out]), 0)
            self.assertEqual(st.main(["normalize", __file__]), 2)


if __name__ == "__main__":
    unittest.main()
