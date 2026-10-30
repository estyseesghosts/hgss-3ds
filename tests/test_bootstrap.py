"""bootstrap.py against a synthetic upstream repo (no game or SDK content)."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import bootstrap  # noqa: E402

ENV = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
       "GIT_COMMITTER_EMAIL": "t@t"}


def run(*a, cwd):
    return subprocess.run(a, cwd=cwd, check=True, capture_output=True, text=True, env=ENV).stdout.strip()


class Bootstrap(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.up = t / "up"
        self.root = t / "port"
        for f in ("lib/NitroSDK/a.c", "lib/NitroSystem/b.c", "src/main.c"):
            (self.up / f).parent.mkdir(parents=True, exist_ok=True)
            (self.up / f).write_text("int x;\n")
        run("git", "init", "-q", "-b", "master", cwd=self.up)
        run("git", "add", "-A", cwd=self.up)
        run("git", "commit", "-q", "-m", "base", cwd=self.up)
        self.pin = run("git", "rev-parse", "HEAD", cwd=self.up)
        for d in ("NitroSDK", "NitroSystem", "pokeheartgold"):
            (self.root / "patches" / d).mkdir(parents=True)
        (self.root / "upstream.lock").write_text(f"# c\nurl={self.up}\ncommit={self.pin}\n")
        # Make one patch per stack on a scratch clone, SDK ones relative to their lib.
        w = t / "work"
        run("git", "clone", "-q", str(self.up), str(w), cwd=t)
        for rel, stack, sub in (("lib/NitroSDK/a.c", "NitroSDK", "lib/NitroSDK"),
                                ("lib/NitroSystem/b.c", "NitroSystem", "lib/NitroSystem"),
                                ("src/main.c", "pokeheartgold", None)):
            (w / rel).write_text("int x;\nint y;\n")
            run("git", "commit", "-q", "-am", f"Patch {stack}\n\nWhy it matters.", cwd=w)
            args = ["git", "format-patch", "-1", "-o", str(self.root / "patches" / stack)]
            if sub:
                args.insert(2, f"--relative={sub}")
            run(*args, cwd=w)

    def tearDown(self):
        self.tmp.cleanup()

    def test_applies_all_stacks_and_is_idempotent(self):
        bootstrap.bootstrap(self.root)
        up = self.root / "build" / "upstream"
        for rel in ("lib/NitroSDK/a.c", "lib/NitroSystem/b.c", "src/main.c"):
            self.assertEqual((up / rel).read_text(), "int x;\nint y;\n")
        self.assertEqual(run("git", "rev-list", "--count", f"{self.pin}..HEAD", cwd=up), "3")
        self.assertTrue((self.root / "build" / "NitroSDK" / "a.c").exists())
        self.assertTrue((self.root / "build" / "NitroSystem" / "b.c").exists())
        bootstrap.bootstrap(self.root)  # second run: no change, no error
        self.assertEqual(run("git", "rev-list", "--count", f"{self.pin}..HEAD", cwd=up), "3")

    def test_message_kept(self):
        bootstrap.bootstrap(self.root)
        log = run("git", "log", "--format=%B", "-3", cwd=self.root / "build" / "upstream")
        self.assertIn("Why it matters.", log)

    def test_bad_patch_fails_hard(self):
        p = next((self.root / "patches" / "NitroSDK").glob("*.patch"))
        p.write_text(p.read_text().replace("int x;", "int WRONG;"))
        with self.assertRaises(SystemExit) as cm:
            bootstrap.bootstrap(self.root)
        self.assertIn("does not apply cleanly", str(cm.exception))

    def test_clean_restarts(self):
        bootstrap.bootstrap(self.root)
        bootstrap.bootstrap(self.root, clean=True)
        self.assertEqual(run("git", "rev-list", "--count", f"{self.pin}..HEAD",
                             cwd=self.root / "build" / "upstream"), "3")


if __name__ == "__main__":
    unittest.main()
