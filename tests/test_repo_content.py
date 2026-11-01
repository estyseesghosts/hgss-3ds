"""The tracked-index content gate (`tools/check_repo_content.py`)."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools import check_repo_content as check  # noqa: E402

REPO = Path(__file__).resolve().parent.parent


class RepoContentTests(unittest.TestCase):
    def test_the_real_index_is_clean(self):
        self.assertEqual(check.problems(REPO), [])

    def test_a_forbidden_suffix_is_caught(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "heartgold.sav").write_bytes(b"x")
            subprocess.run(["git", "-C", str(root), "add", "heartgold.sav"], check=True)
            self.assertIn("heartgold.sav", "\n".join(check.problems(root)))

    def test_an_unlisted_image_is_caught(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "screenshot.png").write_bytes(b"x")
            subprocess.run(["git", "-C", str(root), "add", "screenshot.png"], check=True)
            self.assertIn("screenshot.png", "\n".join(check.problems(root)))

    def test_the_allowlisted_assets_are_allowed(self):
        for rel in check.ALLOWED_BINARY:
            self.assertTrue((REPO / rel).is_file(), rel)
            self.assertNotIn(rel, check.problems(REPO))


if __name__ == "__main__":
    unittest.main()
