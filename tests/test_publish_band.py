"""Half-A L5: engine B publishes through the band diff with 0-byte uploads
for unchanged screens. Older texture sets may need historical bands.

port_compose_pica.c cannot link on the host (libctru), so these are
structural tests over the publish path, in the style of
test_publish_profile.py: the routing, the byte accounting and the
current-frame dirty-band check must be present and ordered so that an unchanged
lower screen converts, tiles and flushes nothing.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "3ds_port/game/app/port_compose_pica.c").read_text()


def bands_body():
    start = SOURCE.index("static void publish_bands(int e, const ComposeLayers *l, Screen *scr, LayerSet *set, int w, int h)")
    end = SOURCE.index("/* Whether the shadow's picture is the picture in l", start)
    return SOURCE[start:end]


class BandPublishTests(unittest.TestCase):
    def test_switch_exists_and_defaults_off(self):
        self.assertIn("void NitroPort_ComposeSetBandPublish(int enabled, int diagnostic)", SOURCE)
        self.assertIn("static int sBandPublishB = 0, sBandPublishDiag = 0;", SOURCE)

    def test_distinct_startup_log_lines(self):
        self.assertIn("[compose] bandPubB=%s (engine-B band-diff upload accounting; default off)", SOURCE)
        self.assertIn("[compose] bandPubBDiag=%s (dirty-band subset violations logged)", SOURCE)

    def test_engine_b_consumes_l4_band(self):
        body = bands_body()
        self.assertIn("NitroPort_Render2DPartialBand(1, &hintKind, &hintY0, &hintY1);", body)

    def test_converted_flushed_bytes_counted(self):
        body = bands_body()
        self.assertIn("sBandConvBytes[1] += (u64)w * (u64)rows * 2u;", body)
        self.assertIn("sBandFlushBytes[1] += (u64)scr->texW * (u64)rows * 2u;", body)
        # Counted on the conversion path only, never on the unchanged path.
        self.assertNotIn("sBandConvBytes", SOURCE[SOURCE.index("static int publish_unchanged"):SOURCE.index("static void publish_bands")])

    def test_subset_check_against_diff(self):
        body = bands_body()
        # Count physical uploads, but only this generation can violate the
        # current-frame hint; an older texture may need historical bands.
        self.assertIn("if (sh->underChanged[b] <= prevUnderGen)", body)
        self.assertIn("if (sh->underChanged[b] == gen && (b * 4 < hintY0 || b * 4 + 4 > hintY1))", body)
        self.assertIn("sBandUploaded[1] += (u32)uploaded;", body)
        self.assertIn("sBandSubsetViol[1] += (u32)bad;", body)
        # A fresh texture uploads everything by design and is not checked.
        self.assertIn("hintKind == 2 && known && prevUnderGen != 0 && shadowValid", body)

    def test_engine_b_1x_path_uses_band_publish(self):
        publish = SOURCE[SOURCE.index("int NitroPort_ComposePublish(int engine, int screen)"):]
        self.assertIn("if (scr->texScale == 1) {\n            publish_bands(e, l, scr, set, w, h);", publish)

    def test_unchanged_returns_before_any_upload_work(self):
        publish = SOURCE[SOURCE.index("int NitroPort_ComposePublish(int engine, int screen)"):]
        unchanged = publish.index("publish_unchanged(e, l, scr, w, h, filter2d, &lockWait)")
        self.assertLess(unchanged, publish.index("publish_bands(e, l, scr, set, w, h);"))
        self.assertLess(unchanged, publish.index("NitroPort_PublishDoubledOver("))
        self.assertLess(unchanged, publish.index("NitroPort_Tile16("))


if __name__ == "__main__":
    unittest.main()
