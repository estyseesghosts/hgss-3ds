"""Tests for tools/bench_report.py (synthetic log lines only).

Covers the P1 instrumentation fields and the guarantee that older logs
still parse: every new field is optional and every old field keeps its
meaning.
"""
import os
import sys
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools import bench_report  # noqa: E402


OLD_GEOMETRY = (
    "[14400] [perf] geometry: 30.0 scenes/s, busy 92% (30.74 ms/scene), "
    "122 skipped, producer wait 0.3 ms, 549 wakeups (longest 48.9 ms); "
    "flow queued 300 done 300 dropped 0"
)
NEW_GEOMETRY = OLD_GEOMETRY + "; built 178 (46.02 ms/built), 14.69 ms/skipped"

OLD_FRAMETIME = (
    "FRAMETIME frame=14400 game_us=5569 state_us=2505 total_us=8074 "
    "hblank_us=2400 handoff_us=100 hb_keep_us=430 hb_dma_us=0 hb_irq_us=866"
)
NEW_FRAMETIME = OLD_FRAMETIME + " hb_ref_us=12 hb_sw_us=3 hb_cmp_us=45"

IDLE_LINE = "[1200] [perf] idle: core0 24.0% core1 64.2%"
GAME_LINE = "[1200] [perf] game: work 108.15 ms, slept 19620.88 ms, late 1"
OFFERS_LINE = "[1200] [perf] offers: 445 dropped (drawer busy)"
DRAWER_OLD = (
    "[1200] [perf] drawer 0 (core 0, both screens): 60.0 frames/s, "
    "2.74 ms/frame, 2.68 ms render, 0.06 ms publish, 600 drawn, 0 lower deferred"
)
DRAWER_NEW = DRAWER_OLD + ", 0.04 ms signal"
TWOD_LINE = (
    "[1200] [perf] 2d: text 3.3%, affine 0.1%, obj 0.0%, merge 0.0%, blend 0.0%, "
    "window 0.0%, finish 0.0%, setup 12.1% (pal 0.1%, key 3.3%, 0 lines, 0 empty, "
    "0 blend, rows 0/0), decide 1.2%, transp 0.0%, check 2.5%, take 0.50 ms, "
    "flatfill 0.01 ms, key 1234/5678 B"
)
PHASE_OLD = (
    "[1200] [perf] compose-phase ms/draw: pick 0.00 prepare 4.94 stage 1.66 "
    "end 0.21; skip 0.01 ms/pass"
)
PHASE_NEW = PHASE_OLD.replace(
    "end 0.21; skip",
    "end 0.21 (xfer 0.05 flight 0.10 lock 0.03 submit 0.02); skip") + \
    "; cause scene 100 set 20 forced 0 other 5"


def sample(*lines):
    text = "frame 1200\n" + "\n".join(lines) + "\n"
    rows = bench_report.parse_log(text)
    assert len(rows) == 1, text
    return rows[0]


class GeometryTests(unittest.TestCase):
    def test_old_geometry_has_no_built_split(self):
        row = sample(OLD_FRAMETIME, OLD_GEOMETRY)
        self.assertEqual(row["geom_scenes_s"], 30.0)
        self.assertEqual(row["geom_busy_pct"], 92)
        self.assertEqual(row["geom_skipped"], 122)
        self.assertNotIn("geom_built", row)

    def test_new_geometry_parses_built_split(self):
        row = sample(OLD_FRAMETIME, NEW_GEOMETRY)
        self.assertEqual(row["geom_built"], 178)
        self.assertAlmostEqual(row["geom_built_ms"], 46.02)
        self.assertAlmostEqual(row["geom_skipped_ms"], 14.69)
        self.assertEqual(row["geom_skipped"], 122)

    def test_built_plus_skipped_spans_done(self):
        row = sample(OLD_FRAMETIME, NEW_GEOMETRY)
        self.assertGreater(row["geom_built"], 0)


class FrametimeTests(unittest.TestCase):
    def test_old_frametime_has_no_hb_extras(self):
        row = sample(OLD_FRAMETIME)
        self.assertEqual(row["hb_irq_us"], 866)
        self.assertNotIn("hb_ref_us", row)

    def test_new_frametime_parses_hb_extras(self):
        row = sample(NEW_FRAMETIME)
        self.assertEqual(row["hb_ref_us"], 12)
        self.assertEqual(row["hb_sw_us"], 3)
        self.assertEqual(row["hb_cmp_us"], 45)


class NewLineTests(unittest.TestCase):
    def test_idle_game_offers(self):
        row = sample(OLD_FRAMETIME, IDLE_LINE, GAME_LINE, OFFERS_LINE)
        self.assertAlmostEqual(row["idle_c0"], 24.0)
        self.assertAlmostEqual(row["idle_c1"], 64.2)
        self.assertAlmostEqual(row["game_work_ms"], 108.15)
        self.assertAlmostEqual(row["game_slept_ms"], 19620.88)
        self.assertEqual(row["game_late"], 1)
        self.assertEqual(row["offers_dropped"], 445)

    def test_drawer_signal_optional(self):
        self.assertNotIn("signal_ms", sample(OLD_FRAMETIME, DRAWER_OLD)["drawers"][0])
        new = sample(OLD_FRAMETIME, DRAWER_NEW)["drawers"][0]
        self.assertAlmostEqual(new["signal_ms"], 0.04)
        self.assertEqual(new["drawn"], 600)

    def test_twod_suffix(self):
        row = sample(OLD_FRAMETIME, TWOD_LINE)
        self.assertAlmostEqual(row["twod_decide_pct"], 1.2)
        self.assertAlmostEqual(row["twod_check_pct"], 2.5)
        self.assertAlmostEqual(row["twod_take_ms"], 0.50)
        self.assertAlmostEqual(row["twod_flatfill_ms"], 0.01)
        self.assertEqual(row["key_map_bytes"], 1234)
        self.assertEqual(row["key_tile_bytes"], 5678)

    def test_compose_phase_old_and_new(self):
        old = sample(OLD_FRAMETIME, PHASE_OLD)
        self.assertNotIn("cause_scene", old)
        new = sample(OLD_FRAMETIME, PHASE_NEW)
        self.assertAlmostEqual(new["phase_end_xfer_ms"], 0.05)
        self.assertAlmostEqual(new["phase_end_flight_ms"], 0.10)
        self.assertAlmostEqual(new["phase_end_lock_ms"], 0.03)
        self.assertAlmostEqual(new["phase_end_submit_ms"], 0.02)
        self.assertEqual(new["cause_scene"], 100)
        self.assertEqual(new["cause_set"], 20)
        self.assertEqual(new["cause_forced"], 0)
        self.assertEqual(new["cause_other"], 5)
        self.assertEqual(
            new["cause_scene"] + new["cause_set"] + new["cause_forced"] + new["cause_other"],
            125)


class ReportTests(unittest.TestCase):
    def test_row_values_carry_new_fields(self):
        row = sample(NEW_FRAMETIME, NEW_GEOMETRY, IDLE_LINE, OFFERS_LINE)
        values = bench_report.row_values(row)
        self.assertEqual(values["geom_built"], 178)
        self.assertAlmostEqual(values["idle_c0"], 24.0)
        self.assertEqual(values["offers_dropped"], 445)
        # Old keys keep their meaning.
        self.assertEqual(values["geom_ms"], 30.74)

    def test_markdown_and_json_render(self):
        text = "frame 1200\n" + "\n".join(
            [NEW_FRAMETIME, NEW_GEOMETRY, IDLE_LINE]) + "\n"
        rows = bench_report.parse_log(text)
        with redirect_stdout(StringIO()) as out:
            bench_report.print_markdown(rows, None)
        self.assertIn("built", out.getvalue())
        payload = {"samples": [bench_report.row_values(r) for r in rows]}
        self.assertEqual(payload["samples"][0]["geom_built"], 178)


if __name__ == "__main__":
    unittest.main()
