"""Shared helpers for the player-facing scripts in `scripts/`.

No third-party packages: everything here is in the Python standard library, so
the scripts run on a plain Linux, macOS or WSL Python 3 install.

`DEVKITPRO` defaults to `/opt/devkitpro` (devkitPro's own install location on
Linux and macOS). The scripts add its `tools/bin`, `devkitARM/bin` and
`pacman/bin` to PATH for every child process, so they also work when the
user's shell has not exported the paths yet.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

#: The repository root: `.../pokeheartgold-3ds/`, from `scripts/_util.py`.
REPO = Path(__file__).resolve().parent.parent
SCRIPTS = REPO / "scripts"


def devkitpro() -> Path:
    """The devkitPro install root (`/opt/devkitpro` unless DEVKITPRO says otherwise)."""
    return Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))


def shim_dir() -> Path:
    """A private bin directory the scripts prepend to PATH.

    On macOS the system `make` is GNU Make 3.81, which cannot run the port's
    Makefiles (`$(file ...)` needs 4.0). When Homebrew's `gmake` is installed,
    a `make` symlink here makes every child build use it without changing the
    user's shell.
    """
    return Path.home() / ".cache" / "pokeheartgold-3ds" / "bin"


def _macos_make_shim() -> None:
    candidates = [shutil.which("gmake"), "/opt/homebrew/bin/gmake", "/usr/local/bin/gmake"]
    gmake = next((Path(c) for c in candidates if c and Path(c).is_file()), None)
    if gmake is None:
        return
    directory = shim_dir()
    link = directory / "make"
    try:
        directory.mkdir(parents=True, exist_ok=True)
        if link.is_symlink() and link.resolve() == gmake.resolve():
            return
        if link.exists() or link.is_symlink():
            link.unlink()
        link.symlink_to(gmake.resolve())
    except OSError:
        pass


def tool_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    """The environment child builds run with: devkitPro's tools on PATH."""
    root = devkitpro()
    dirs = [root / "tools" / "bin", root / "devkitARM" / "bin", root / "pacman" / "bin"]
    env = dict(os.environ)
    env["DEVKITPRO"] = str(root)
    env.setdefault("DEVKITARM", str(root / "devkitARM"))
    prefix = [str(d) for d in dirs if d.is_dir()]
    if sys.platform == "darwin":
        _macos_make_shim()
        if shim_dir().is_dir():
            prefix.insert(0, str(shim_dir()))
    env["PATH"] = os.pathsep.join(prefix + [env.get("PATH", "")])
    if extra:
        env.update(extra)
    return env


def which(name: str) -> Path | None:
    """Find a tool on PATH plus devkitPro's usual bin directories."""
    found = shutil.which(name, path=tool_env()["PATH"])
    return Path(found) if found else None


def python() -> str:
    """The interpreter running the scripts (used for the other Python tools)."""
    return sys.executable or "python3"


def run(cmd: list[object], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> int:
    """Run a command in the repository, with devkitPro on PATH; return its code."""
    shown = " ".join(str(c) for c in cmd)
    say(f"$ {shown}")
    return subprocess.call(
        [str(c) for c in cmd],
        cwd=str(cwd or REPO),
        env=env or tool_env(),
    )


def checked(cmd: list[object], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    """Run a command and stop with a clear message if it fails."""
    code = run(cmd, cwd=cwd, env=env)
    if code != 0:
        fail(f"the step above failed (exit {code})")


def capture(cmd: list[object], *, cwd: Path | None = None) -> str:
    """Run a command and return its combined output (used for version checks)."""
    proc = subprocess.run(
        [str(c) for c in cmd],
        cwd=str(cwd or REPO),
        env=tool_env(),
        capture_output=True,
        text=True,
    )
    return (proc.stdout + proc.stderr).strip()


def capture_status(cmd: list[object], *, cwd: Path | None = None) -> tuple[int, str]:
    """Run a command quietly: its exit code and combined output."""
    proc = subprocess.run(
        [str(c) for c in cmd],
        cwd=str(cwd or REPO),
        env=tool_env(),
        capture_output=True,
        text=True,
    )
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def say(message: str = "") -> None:
    print(message, flush=True)


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr, flush=True)


def fail(message: str, code: int = 1) -> None:
    print(f"error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(code)


def human_size(size: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if size < 1024 or unit == "GiB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024.0
    return f"{size:.1f} GiB"


def confirm(question: str, assume_yes: bool) -> bool:
    """Ask before a system-wide change; `--yes` answers it for the user."""
    if assume_yes:
        return True
    if not sys.stdin.isatty():
        fail("this step needs confirmation; re-run with --yes in a script")
    answer = input(f"{question} [y/N] ").strip().lower()
    return answer in ("y", "yes")
