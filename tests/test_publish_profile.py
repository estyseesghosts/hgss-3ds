"""Guard the wall-time attribution boundaries without treating them as CPU time."""
from pathlib import Path
import unittest
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


class PublishProfileTests(unittest.TestCase):
    def test_flat_guard_executable(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "flat"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Werror",
                            *shlex.split(os.environ.get("HGSS_FLAT_TEST_CFLAGS", "")),
                            "-I" + str(ROOT / "3ds_port/game/app"),
                            str(ROOT / "tests/fixtures/compose_flat.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_flat_proof_is_invalidated_before_other_publishers(self):
        source = (ROOT / "3ds_port/game/app/port_compose_pica.c").read_text()
        self.assertIn("sShadowFlat[e] = 0; /* this publication may change or bypass the shadow */", source)
        self.assertIn("sShadowFlatColor[e] = l->backdrop;", source)
        shell = (ROOT / "hgss/game/app/main_3ds.c").read_text()
        self.assertIn("layers->flatCpuPlanes = top && split.flatCpuPlanes;", shell)
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

    def test_screen_render_attribution_follows_display_swap(self):
        source = (ROOT / "hgss/game/app/main_3ds.c").read_text()
        drawer = source[source.index("static void DrawerMain"):source.index("static bool StartDrawer")]
        self.assertIn("OnTop(0, d->aOnTop) ? 0 : 1", drawer)
        self.assertIn("OnTop(1, d->aOnTop) ? 0 : 1", drawer)
        self.assertEqual(drawer.count("&d->screenRenderCalls[screen], 1u"), 2)
        self.assertIn("render: %u calls, %.2f ms/call wall (includes preemption)", source)
