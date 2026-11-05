"""Guard the wall-time attribution boundaries without treating them as CPU time."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PublishProfileTests(unittest.TestCase):
    def test_publish_records_all_exit_paths_and_separate_stages(self):
        source = (ROOT / "3ds_port/game/app/port_compose_pica.c").read_text()
        body = source[source.index("int NitroPort_ComposePublish(int engine, int screen)"):
                      source.index("/* -------------------------------------------------------------- the quads */")]
        for result in (0, 1, 2, 3):
            self.assertIn(f"record_publish(e, {result},", body)
        self.assertIn("publish_unchanged(e, l, scr, w, h, filter2d, &lockWait)", body)
        self.assertLess(body.index("t = svcGetSystemTick();\n    if (!l->displayOff && filter2d"),
                        body.index("convert = svcGetSystemTick() - t;"))
        self.assertLess(body.index("convert = svcGetSystemTick() - t;"),
                        body.index("bg = svcGetSystemTick() - t;"))
        self.assertIn("lockWait += svcGetSystemTick() - t;", body)

    def test_report_labels_inclusive_wall_time(self):
        source = (ROOT / "hgss/game/app/main_3ds.c").read_text()
        self.assertIn("convert+flush", source)
        self.assertIn("wall, check includes wait", source)
        self.assertIn("p->noBuffer - last->noBuffer", source)
        self.assertIn("memcpy(sPerfPublish, st.publish, sizeof(sPerfPublish));", source)
