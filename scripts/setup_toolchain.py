#!/usr/bin/env python3
"""Set up everything needed to build pokeheartgold-3ds on Linux or macOS.

Usage:
    python3 scripts/setup_toolchain.py            # check, then install what is missing
    python3 scripts/setup_toolchain.py --check    # only check; change nothing
    python3 scripts/setup_toolchain.py --yes      # do not ask before installing

What it installs (through devkitPro's own package manager):

    arm-none-eabi-gcc     the 3DS compiler (devkitARM)
    libctru, citro3d      the 3DS libraries
    3dsxtool, smdhtool    the .3dsx packager
    picasso, bin2s        the shader tools the build uses

It also checks `git`, `make` and Python 3.  The port's Makefiles need **GNU
Make 4.0 or newer**; on macOS the system `make` is 3.81, so the script installs
Homebrew's `make` (`gmake`) and the build scripts use it automatically.  It
does **not** need a ROM, a save, MWCC or the Nitro SDK: those are only for the
optional DS matching build, not for building or playing the port.

Linux: Debian/Ubuntu (and derivatives) are supported automatically, through
devkitPro's apt repository.  macOS: devkitPro's pacman installer is downloaded
from its GitHub releases and installed with `installer`.

Windows is not supported by this script; see docs/WINDOWS.md.
"""
from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _util as u  # noqa: E402

REQUIRED = {
    "git": "git",
    "make": "make",
    "arm-none-eabi-gcc": "devkitARM (the 3DS compiler)",
    "3dsxtool": "devkitPro tools (the .3dsx packager)",
    "smdhtool": "devkitPro tools (the SMDH metadata)",
    "picasso": "devkitPro tools (the shader assembler)",
    "bin2s": "devkitPro tools (the shader blob embedder)",
}
OPTIONAL = {
    "3dsxdump": "checks a built .3dsx (the artifact gate)",
    "gcc-10": "the PC build and the host tests (gcc-multilib)",
}

APT_PACKAGES = ["ca-certificates", "curl", "gnupg", "make", "git", "python3"]
DEVKITPRO_GPG = "https://apt.devkitpro.org/devkitpro-pub.gpg"
DEVKITPRO_LIST = "/etc/apt/sources.list.d/devkitpro.list"
PACMAN_RELEASES = "https://api.github.com/repos/devkitPro/pacman/releases/latest"


def os_release() -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        for line in Path("/etc/os-release").read_text().splitlines():
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip().strip('"')
    except OSError:
        pass
    return values


def is_debian_like() -> bool:
    data = os_release()
    ids = {data.get("ID", ""), *data.get("ID_LIKE", "").split()}
    return bool(ids & {"debian", "ubuntu", "pop", "linuxmint", "raspbian", "zorin"})


def sudo() -> list[str]:
    if os.geteuid() == 0:
        return []
    if shutil.which("sudo") is None:
        u.fail("this needs root (or sudo) to install system packages")
    return ["sudo"]


def run_install(cmd: list[str]) -> None:
    """A system install step: print it, run it, stop on failure."""
    u.say("$ " + " ".join(cmd))
    if subprocess.call(cmd) != 0:
        u.fail("the command above failed; fix its message and run this script again")


def install_linux() -> None:
    if not is_debian_like():
        u.fail(
            "automatic setup supports Debian/Ubuntu and macOS.  On this Linux:\n"
            "  install devkitPro pacman, then run:\n"
            "    sudo dkp-pacman -Syu --noconfirm 3ds-dev\n"
            "  and install git, make and python3 with your package manager.\n"
            "  See https://devkitpro.org/wiki/Getting_Started"
        )
    root = sudo()
    run_install(root + ["apt-get", "update"])
    run_install(root + ["apt-get", "install", "-y", *APT_PACKAGES])
    run_install(root + ["install", "-d", "-m", "0755", "/usr/local/share/keyring"])
    run_install(
        root
        + ["sh", "-c", f"curl -fsSL -A dkp-apt {DEVKITPRO_GPG} -o /usr/local/share/keyring/devkitpro-pub.gpg"]
    )
    run_install(
        root
        + [
            "sh",
            "-c",
            "printf '%s\\n' "
            "'deb [signed-by=/usr/local/share/keyring/devkitpro-pub.gpg] https://apt.devkitpro.org stable main' "
            f"> {DEVKITPRO_LIST}",
        ]
    )
    run_install(root + ["apt-get", "update"])
    run_install(root + ["apt-get", "install", "-y", "devkitpro-pacman"])
    run_install(root + ["ln", "-sf", "/proc/self/mounts", "/etc/mtab"])
    pacman = shutil.which("dkp-pacman") or "/usr/bin/dkp-pacman"
    # general-tools has bin2s; picasso assembles the shaders; 3ds-dev has
    # devkitARM, libctru, citro3d and 3dsxtool.
    run_install(root + [pacman, "-Syu", "--noconfirm", "3ds-dev", "general-tools", "picasso"])


def pacman_on_macos() -> str | None:
    for candidate in (
        u.devkitpro() / "pacman" / "bin" / "dkp-pacman",
        Path("/opt/devkitpro/pacman/bin/dkp-pacman"),
        shutil.which("dkp-pacman"),
    ):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


def install_macos() -> None:
    if shutil.which("git") is None or shutil.which("make") is None:
        u.fail(
            "macOS needs git and make, which come with the Xcode Command Line\n"
            "  Tools.  Install them with:\n"
            "    xcode-select --install\n"
            "  then run this script again."
        )
    pacman = pacman_on_macos()
    if pacman is None:
        u.say("devkitPro pacman is not installed; downloading its installer...")
        try:
            with urllib.request.urlopen(PACMAN_RELEASES, timeout=60) as reply:
                release = json.load(reply)
        except Exception as exc:  # noqa: BLE001 - the user needs the URL, not a traceback
            u.fail(
                f"could not reach GitHub to find the devkitPro installer ({exc}).\n"
                "  Download one from https://github.com/devkitPro/pacman/releases\n"
                "  (devkitpro-pacman-installer.pkg), install it, then run this script again."
            )
        assets = [a for a in release.get("assets", []) if a.get("name", "").endswith(".pkg")]
        if not assets:
            u.fail(
                "the latest devkitPro release has no .pkg installer; download one from\n"
                "  https://github.com/devkitPro/pacman/releases"
            )
        url = assets[0]["browser_download_url"]
        name = assets[0]["name"]
        with tempfile.TemporaryDirectory(prefix="dkp-") as tmp:
            pkg = Path(tmp) / name
            u.say(f"downloading {url}")
            try:
                urllib.request.urlretrieve(url, pkg)
            except Exception as exc:  # noqa: BLE001
                u.fail(f"could not download {url}: {exc}")
            run_install(sudo() + ["installer", "-pkg", str(pkg), "-target", "/"])
        pacman = pacman_on_macos()
        if pacman is None:
            u.fail(
                "the installer finished but dkp-pacman was not found at /opt/devkitpro;\n"
                "  open a new terminal and run this script again."
            )
    run_install(sudo() + [pacman, "-Syu", "--noconfirm", "3ds-dev", "general-tools", "picasso"])


def make_version() -> tuple[int, int] | None:
    """GNU Make's (major, minor), or None when `make` is not GNU Make."""
    path = u.which("make")
    if path is None:
        return None
    text = u.capture([path, "--version"])
    match = re.search(r"GNU Make (\d+)\.(\d+)", text)
    return (int(match.group(1)), int(match.group(2))) if match else None


def make_ok() -> bool:
    version = make_version()
    return version is not None and version >= (4, 0)


def install_make_macos() -> None:
    """macOS's make is 3.81; Homebrew's is 4.x (installed as `gmake`)."""
    if shutil.which("brew") is None:
        u.fail(
            "this Mac's make is older than GNU Make 4.0, which the port's\n"
            "  Makefiles need.  Install Homebrew (https://brew.sh), then run\n"
            "    brew install make\n"
            "  and run this script again.  The build scripts use gmake as make."
        )
    u.say("")
    u.say("Installing GNU Make 4 with Homebrew (no password needed) ...")
    run_install(["brew", "install", "make"])
    if not make_ok():
        u.fail(
            "Homebrew's make still was not found.  Run `brew install make`, open\n"
            "  a new terminal, and run this script again."
        )


def versions() -> list[tuple[str, str, str]]:
    """(tool, found path or '-', version line) for the check table."""
    rows = []
    # 3dsxtool, smdhtool and bin2s print usage on --version, so only the tools
    # that really answer with a version are asked.
    asked = {"git", "make", "arm-none-eabi-gcc", "picasso"}
    for tool in REQUIRED:
        path = u.which(tool)
        if path is None:
            rows.append((tool, "-", "MISSING"))
            continue
        text = u.capture([path, "--version"]) if tool in asked else ""
        rows.append((tool, str(path), text.splitlines()[0] if text else "installed"))
    for tool in OPTIONAL:
        path = u.which(tool)
        rows.append((tool, str(path) if path else "-", "optional"))
    return rows


def check(verbose: bool = True) -> bool:
    rows = versions()
    if verbose:
        width = max(len(name) for name, _, _ in rows)
        for name, path, version in rows:
            u.say(f"  {name:<{width}}  {version:<40}  {path}")
    return all(path != "-" for name, path, _ in rows if name in REQUIRED)


def main(argv: list[str]) -> int:
    check_only = "--check" in argv
    assume_yes = "--yes" in argv or "-y" in argv
    if any(arg in ("-h", "--help") for arg in argv):
        print(__doc__)
        return 0

    u.say("pokeheartgold-3ds toolchain")
    u.say("")
    tools_ok = check()
    make_good = make_ok()
    if tools_ok and make_good:
        u.say("")
        u.say("Everything needed to build the port is installed.")
        return 0

    missing = [name for name in REQUIRED if u.which(name) is None]
    u.say("")
    if missing:
        u.say("Missing: " + ", ".join(missing))
    if not make_good:
        version = make_version()
        found = "not found" if version is None else f"{version[0]}.{version[1]}"
        u.say(f"make is {found}; the port needs GNU Make 4.0 or newer.")
    if check_only:
        return 1

    system = platform.system()
    if system == "Linux":
        if missing:
            u.say("")
            u.say("Installing the compiler and 3DS libraries with devkitPro pacman (needs sudo).")
            install_linux()
        elif not make_good:
            run_install(sudo() + ["apt-get", "install", "-y", "make"])
    elif system == "Darwin":
        if missing:
            u.say("")
            u.say("Installing the compiler and 3DS libraries with devkitPro pacman (needs your password).")
            install_macos()
        if not make_ok():
            install_make_macos()
    else:
        u.fail(
            f"{system} is not set up by this script.  On Windows, use WSL2 and run this\n"
            "  script inside it; see docs/WINDOWS.md."
        )

    u.say("")
    u.say("Checking again:")
    if check() and make_ok():
        u.say("")
        u.say("Done.  The build scripts add devkitPro to PATH themselves; for your own")
        u.say("shell, add these lines to ~/.zshrc or ~/.bashrc:")
        u.say("")
        u.say("  export DEVKITPRO=/opt/devkitpro")
        u.say("  export DEVKITARM=$DEVKITPRO/devkitARM")
        u.say('  export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"')
        if platform.system() == "Darwin":
            u.say("")
            u.say("The scripts also find Homebrew's GNU make (gmake) on their own.")
        return 0
    u.say("")
    u.say("Some tools are still missing.  Open a new terminal and run this script again;")
    u.say("if a tool is still missing, send the table above.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
