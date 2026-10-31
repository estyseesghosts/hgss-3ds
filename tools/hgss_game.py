#!/usr/bin/env python3
"""Build the HeartGold/SoulSilver game sources with GCC, then try to link.

Usage (inside the dev container, after tools/bootstrap.py):

    python3 tools/hgss_game.py --target host32 --keep-going --out /work/out/game1
    python3 tools/hgss_game.py --target 3ds    --keep-going --out /work/out/game1

This is the Phase 2 harness: it compiles the game as the port compiles the
SDK, builds the port's libnitro/libnitrosystem, generates the overlay table
and the Wi-Fi stand-ins, and links pokeheartgold-pc (host32) or
pokeheartgold-3ds.elf (3ds, through the libctru/citro3d shell in
hgss/game/app/main_3ds.c).

The source lists come from tools/game_sources.py (game src/ C/C++ and the
ARM9 .s data), the overlay ids and table from tools/game_overlays.py, the
generated .naix/msg/fx headers from tools/prepare_game_headers.py, and the
SDK libraries from tools/hgss_nitro.py. Nothing here writes outside --out
and the generated headers under build/upstream (the decomp's build does the
same).

Not every source compiles under GCC yet; --keep-going compiles what it can,
prints the error count per file, then links the objects that were produced
and prints the unresolved symbols. That list is the work queue for the next
slice. The link targets are deliberately named -pc/-3ds.elf, not .3dsx:
packaging (3dsxtool, the SMDH) is Phase 5. Exit status is 0 only when every
object compiled and the link succeeded; 1 means the run reached the link but
there were errors (the useful case).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
DEFAULT_ROOT = REPO / "build" / "upstream"

sys.path.insert(0, str(TOOLS))
import game_sources as gs  # noqa: E402
import hgss_nitro as nitro  # noqa: E402

# The libraries the game's own build compiles from lib/ but the port's
# libnitro.a/libnitrosystem.a do not contain: NitroCrypto (lib/src) and the
# syscall helpers (lib/syscall/src). Everything else under lib/ is either in
# one of those archives, replaced by newlib (MSL_C) or stubbed
# (NitroDWC/NitroWiFi/libCPS/libVCT/dsprot).
GAME_LIB_PREFIXES = ("lib/src/", "lib/syscall/src/")

# Port-replaced or stubbed SDK trees, and MSL_C, must not be compiled a
# second time next to the archives.
SKIP_LIB_PREFIXES = ("lib/NitroSDK/", "lib/NitroSystem/", "lib/MSL_C/",
                     "lib/NitroDWC/", "lib/NitroWiFi/", "lib/libCPS/",
                     "lib/libVCT/", "lib/dsprot/")

# Objects that must not be linked into the single host/3DS program:
# Metrowerks' C library data (the host libc/newlib owns errno, ctype, ...),
# the stubbed Wi-Fi/DWC/CPS/VCT/dsprot libraries, and MSL's C++ runtime data
# (3ds_port/nitro's port_misc.c owns the destructor chain). They are still
# assembled/compiled, so the conversion is checked; the missing symbols they
# would provide show up in the link's unresolved list.
LINK_EXCLUDED_PREFIXES = ("lib/MSL_C/", "lib/NitroDWC/", "lib/NitroWiFi/",
                          "lib/libCPS/", "lib/libVCT/", "lib/dsprot/",
                          "lib/asm/msl_")

# Pure-data objects from a stubbed library that the game's own link needs:
# the lobby chat protocol tables, format strings and vtable/assert data.
# Their functions remain stubbed (hgss/game/stubs); only the blobs are linked,
# because the lobby code reads them directly. main.lsf names both.
LINK_DATA_EXCEPTIONS = ("lib/NitroDWC/asm/dwc_lobby_chat_data.o",
                        "lib/NitroDWC/asm/dwc_lobby_data.o")

PORT_DEFINES = ["-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
                "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS"]

WARNINGS = ["-Wall", "-Wuninitialized", "-Wmaybe-uninitialized"]
SECTION_FLAGS = ["-ffunction-sections", "-fdata-sections"]

HOST_FLAGS = ["-m32", "-malign-double", "-fno-pie",
              "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast"]
ARM_FLAGS = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp",
             "-mfloat-abi=hard", "-mtp=soft", "-mword-relocations", "-D__3DS__"]

UNDEFINED_RE = re.compile(r"undefined reference to [`']([^']+)'")
ERROR_RE = re.compile(r"(?:^|:\s)(?:fatal )?error:")


# --------------------------------------------------------------- target setup

def devkitpro() -> Path:
    """devkitPro's root (DEVKITPRO, with the container default)."""
    return Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))


def devkitarm_bin() -> Path:
    """devkitARM's bin/ directory (DEVKITARM, with the container default)."""
    return Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin"


def target_tools(target: str) -> dict[str, str]:
    """The compile/archive/inspection tools for one target."""
    if target == "host32":
        return {"cc": os.environ.get("HGSS_HOST_CC", "gcc-10"),
                "ar": "ar", "nm": "nm", "objcopy": "objcopy"}
    if target == "3ds":
        prefix = devkitarm_bin() / "arm-none-eabi-"
        return {"cc": str(prefix) + "gcc", "ar": str(prefix) + "gcc-ar",
                "nm": str(prefix) + "nm", "objcopy": str(prefix) + "objcopy"}
    raise ValueError(f"unknown target {target!r}")


def port_cflags(target: str, cxx: bool = False) -> list[str]:
    """The port's compiler flags (3ds_port/common.mk) for one target.

    The language standard differs for C++ (gcc rejects -std=gnu99 for C++);
    everything else is what every port object is compiled with. The C++
    game files (src/lobby) are built without exceptions and RTTI, so their
    static constructors stay ordinary .init_array entries.
    """
    if target not in ("host32", "3ds"):
        raise ValueError(f"unknown target {target!r}")
    flags = ["-std=gnu++98" if cxx else "-std=gnu99",
             "-fgnu89-inline", "-fno-strict-aliasing", "-fno-short-enums",
             "-fsigned-char", "-fwrapv",
             "-fno-aggressive-loop-optimizations",
             "-fno-delete-null-pointer-checks"]
    flags += PORT_DEFINES
    flags += WARNINGS + SECTION_FLAGS
    if cxx:
        flags += ["-fno-exceptions", "-fno-rtti"]
    flags += HOST_FLAGS if target == "host32" else ARM_FLAGS
    return flags


def hgss_includes(root: Path, sdk: Path, gen: Path) -> list[str]:
    """Include paths for game sources.

    tools/hgss_nitro.py's list (nitro_port + HGSS's flat SDK headers) plus
    the game's own include directory, its library headers, the generated
    naix/msg headers under files/, and the generated overlay-id header.
    """
    nitro_dir = REPO / "3ds_port" / "nitro"
    return nitro.sdk_includes(root, sdk, nitro_dir, gen) + [
        f"-I{REPO / '3ds_port' / 'pak' / 'include'}",
        # After the system headers: HGSS's lib/include has the game's own
        # <dwc/...> and other library headers, but also Metrowerks
        # stdarg.h/stddef.h/string.h. -idirafter lets the real system headers
        # win while still finding the game's library headers.
        f"-idirafter{root}/lib/include",
        f"-I{root}/files",
        f"-I{root}/files/msgdata",
        f"-I{gen}",
    ]


def include_flags(root: Path, out: Path) -> list[str]:
    """Include paths for a real build (creates the SDK shim's symlinks)."""
    sdk, _ = nitro.make_shim(root)
    return hgss_includes(root, sdk, out / "gen")


# ---------------------------------------------------------------- source sets

def select_sources(data: dict) -> dict[str, list[str]]:
    """C, C++ and asm sources this harness compiles, from game_sources data.

    C/C++: every game source under src/ (the original common.mk lists them
    all), plus the game's own lib/ sources that neither libnitro.a nor
    libnitrosystem.a provides (NitroCrypto and syscall). SDK sources are not
    compiled here: the archives own them, and compiling them twice would
    duplicate every symbol at link time.
    """
    c = list(data["c_sources"])
    cpp = list(data["cpp_sources"])
    for rel in data["port_lib_sources"]:
        if rel.startswith(GAME_LIB_PREFIXES) and not rel.startswith(SKIP_LIB_PREFIXES):
            if rel.endswith(".cpp"):
                cpp.append(rel)
            else:
                c.append(rel)
    return {"c": sorted(set(c)), "cpp": sorted(set(cpp)),
            "asm": sorted(set(data["asm_sources"]))}


def app_sources(target: str) -> list[Path]:
    """The program's own objects: the shared start and stubs, plus the
    target's main (main_pc.c on host32, main_3ds.c on 3ds)."""
    if target not in ("host32", "3ds"):
        raise ValueError(f"unknown target {target!r}")
    main = "main_pc.c" if target == "host32" else "main_3ds.c"
    return [REPO / "hgss" / "game" / "app" / main,
            REPO / "hgss" / "game" / "app" / "heartgold.c",
            REPO / "hgss" / "game" / "stubs" / "wm_stubs.c",
            REPO / "hgss" / "game" / "stubs" / "dsprot_stubs.c"]


def object_path(out: Path, rel: str) -> Path:
    """out/obj/<checkout-relative path with .o> (main.lsf's object spelling)."""
    return out / "obj" / Path(rel).with_suffix(".o")


def lsf_object_set(data: dict) -> set[str]:
    """The object paths main.lsf links (Static, Autoloads, every overlay).

    src/text_0205B4EC.c is compiled by the original build's wildcard but is
    in no block; linking it would duplicate dialog_box.c's functions.
    """
    return {entry["object"] for block in data["overlay_map"]
            for entry in block["objects"]}


def link_objects(objects: list[Path], data: dict, out: Path) -> list[Path]:
    """The objects the link gets, in link order.

    Only main.lsf's objects (the original build's linked set) and only the
    libraries the port provides itself: Metrowerks' C library data (errno,
    ctype, printf tables) would clash with the host libc/newlib, and the
    stubbed Wi-Fi/DWC/CPS/VCT/dsprot libraries are replaced by
    hgss/game/stubs. The excluded objects are still built, so the conversion
    is checked and the symbols they would provide appear as unresolved.
    """
    allowed = lsf_object_set(data)
    result = []
    for obj in objects:
        rel = obj.relative_to(out / "obj").as_posix()
        if rel not in allowed:
            continue
        if rel in LINK_DATA_EXCEPTIONS:
            result.append(obj)
            continue
        if any(rel.startswith(prefix) for prefix in LINK_EXCLUDED_PREFIXES):
            continue
        result.append(obj)
    return result


# ------------------------------------------------------------------ commands

def compile_command(cc: str, flags: list[str], src: Path, obj: Path,
                    language: str) -> list[str]:
    """One C or C++ compile. The language is explicit so a file never
    depends on its extension being recognised."""
    return [cc, *flags, "-x", language, "-c", str(src), "-o", str(obj)]


def asm_command(root: Path, src: Path, obj: Path, cc: str,
                flags: list[str]) -> list[str]:
    """The tools/s_to_gas.py invocation that assembles one ARM9 .s file."""
    return [sys.executable, str(TOOLS / "s_to_gas.py"), "--root", str(root),
            str(src), "-o", str(obj), "--cc", cc, "--cflags", *flags]


def asm_flags(target: str, root: Path, out: Path) -> list[str]:
    """Flags for the assembler stage: the target machine plus every include,
    but no C-only -std/-f flags (s_to_gas passes them to the preprocessor,
    which rejects some of them for assembler input)."""
    machine = list(HOST_FLAGS) if target == "host32" else list(ARM_FLAGS)
    return machine + PORT_DEFINES + include_flags(root, out)


def overlay_command(root: Path, out: Path, objcopy: str) -> list[str]:
    """tools/game_overlays.py: group each overlay's data sections and write
    its table and the overlay-ID ld script."""
    gen = out / "gen"
    return [sys.executable, str(TOOLS / "game_overlays.py"),
            "--lsf", str(root / "main.lsf"), "--upstream", str(root),
            "--objdir", str(out / "obj"), "--objcopy", objcopy,
            "--output", str(gen / "overlay_table.c"),
            "--ids-output", str(gen / "overlay_ids.ld"),
            "--header", str(gen / "overlay_ids.h")]


def prepare_command(root: Path, out: Path) -> list[str]:
    """tools/prepare_game_headers.py into the checkout, with the overlay ids
    in the build's gen/ (the checkout itself must stay build-generated)."""
    gen = out / "gen"
    return [sys.executable, str(TOOLS / "prepare_game_headers.py"),
            "--root", str(root),
            "--ids-output", str(gen / "overlay_ids.ld"),
            "--header", str(gen / "overlay_ids.h")]


def wifi_stub_command(nm: str, root: Path, objects: list[Path],
                      output: Path) -> list[str]:
    """tools/make_game_wifi_stubs.py over the objects that were compiled."""
    headers = [root / "lib" / "NitroDWC" / "include",
               root / "lib" / "NitroWiFi" / "include",
               root / "lib" / "libVCT" / "include",
               root / "lib" / "libCPS" / "include",
               root / "lib" / "include", root / "include"]
    sources = [root / "lib" / "NitroDWC", root / "lib" / "NitroWiFi",
               root / "lib" / "libCPS", root / "lib" / "libVCT",
               root / "lib" / "dsprot"]
    cmd = [sys.executable, str(TOOLS / "make_game_wifi_stubs.py"),
           "--nm", nm, "--headers"]
    cmd += [str(p) for p in headers if p.is_dir()]
    cmd += ["--sources"] + [str(p) for p in sources if p.is_dir()]
    cmd += ["--output", str(output), "--"]
    cmd += [str(p) for p in objects]
    return cmd


def link_command(cc: str, target: str, app_objects: list[Path],
                 game_objects: list[Path], stub_objects: list[Path],
                 overlay_table: Path, overlay_ids: Path,
                 libs: list[Path], binary: Path,
                 rsp_path: Path) -> list[str]:
    """The link. Game objects go through a response file (over a thousand
    paths); the overlay table is last among the game's objects so its 4-byte
    section tails land at the end (tools/game_overlays.py)."""
    ordered = list(app_objects) + list(game_objects) + list(stub_objects)
    rsp_path.write_text("\n".join(str(p) for p in ordered) + "\n",
                        encoding="utf-8")
    if target == "host32":
        flags = [*HOST_FLAGS, "-no-pie", "-pthread"]
        extra = ["-lm", "-pthread"]
    else:
        # devkitARM's crt0 (3dsx.specs) provides _start and calls main; the
        # program is a normal libctru/citro3d one.
        pro = devkitpro()
        flags = [*ARM_FLAGS, "-specs=3dsx.specs",
                 f"-L{pro / 'libctru' / 'lib'}",
                 f"-L{pro / 'portlibs' / '3ds' / 'lib'}",
                 "-Wl,--undefined=Heartgold_Run", "-Wl,--undefined=NitroMain"]
        extra = ["-lcitro3d", "-lctru", "-lm"]
    cmd = [cc, *flags, "-Wl,--gc-sections",
           "-Wl,--wrap=ErrorHandling_AssertFail", f"@{rsp_path}",
           str(overlay_table), "-Wl,--start-group"]
    cmd += [str(p) for p in libs]
    cmd += ["-Wl,--end-group", str(overlay_ids), *extra, "-o", str(binary)]
    return cmd


# ------------------------------------------------------------------- helpers

def error_count(text: str) -> int:
    """Compiler errors in one file's output (not warnings)."""
    return sum(1 for line in text.splitlines() if ERROR_RE.search(line))


def unresolved_symbols(text: str) -> list[str]:
    """Unique undefined symbols in a linker's output, in first-seen order."""
    seen: dict[str, None] = {}
    for name in UNDEFINED_RE.findall(text):
        seen.setdefault(name, None)
    return list(seen)


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a child with captured output (tasks print their own summaries)."""
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def compile_one(cc: str, flags: list[str], src: Path, obj: Path,
                language: str, log_dir: Path) -> tuple[bool, int, str]:
    obj.parent.mkdir(parents=True, exist_ok=True)
    proc = run(compile_command(cc, flags, src, obj, language))
    text = proc.stdout + proc.stderr
    failures = error_count(text)
    if proc.returncode != 0 and failures == 0:
        failures = 1  # e.g. the compiler itself failed
    if proc.returncode != 0:
        log = log_dir / (str(obj.relative_to(log_dir.parent)) + ".log")
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(text, encoding="utf-8")
        return False, failures, str(log)
    return True, 0, ""


def compile_objects(cc: str, flags: list[str], sources: list[tuple[Path, Path, str]],
                    jobs: int, log_dir: Path, keep_going: bool
                    ) -> tuple[list[Path], list[tuple[str, int, str]]]:
    """Compile (src, obj, language) triples; return objects and failures."""
    built: list[Path] = []
    failed: list[tuple[str, int, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(compile_one, cc, flags, src, obj, language, log_dir):
                   (src, obj) for src, obj, language in sources}
        for future in concurrent.futures.as_completed(futures):
            src, obj = futures[future]
            ok, errors, log = future.result()
            if ok:
                built.append(obj)
            else:
                failed.append((str(src), errors, log))
                if not keep_going:
                    for pending in futures:
                        pending.cancel()
                    break
    built.sort()
    failed.sort()
    return built, failed


def assemble_objects(root: Path, cc: str, flags: list[str], out: Path,
                     sources: list[str], jobs: int, log_dir: Path,
                     keep_going: bool) -> tuple[list[Path], list[tuple[str, int, str]]]:
    """Assemble every .s through tools/s_to_gas.py."""
    built: list[Path] = []
    failed: list[tuple[str, int, str]] = []

    def one(rel: str):
        obj = object_path(out, rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        proc = run(asm_command(root, root / rel, obj, cc, flags))
        text = proc.stdout + proc.stderr
        errors = error_count(text)
        if proc.returncode != 0 and errors == 0:
            errors = 1
        return rel, obj, proc.returncode == 0, errors, text

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(one, rel): rel for rel in sources}
        for future in concurrent.futures.as_completed(futures):
            rel, obj, ok, errors, text = future.result()
            if ok:
                built.append(obj)
            else:
                log = log_dir / (str(obj.relative_to(out / "obj")) + ".log")
                log.parent.mkdir(parents=True, exist_ok=True)
                log.write_text(text, encoding="utf-8")
                failed.append((rel, errors, str(log)))
                if not keep_going:
                    for pending in futures:
                        pending.cancel()
                    break
    built.sort()
    failed.sort()
    return built, failed


def weaken_duplicate_symbols(nm: str, objcopy: str, objects: list[Path],
                             out: Path) -> tuple[list[str], int]:
    """Make every symbol defined in more than one object weak, once.

    The original game is one program, so a global defined twice can only be
    a header inline (Metrowerks kept one copy; GCC exports one from every
    object that includes the header) or a symbol of two overlays that never
    coexist on the DS. Both must not stop the port's link. The Platinum
    build does the same census and weaken pass; this is the HGSS harness's
    copy. Returns the duplicate names and how many objects were touched.

    nm is run on batches of files, not one process per object: spawning
    hundreds of children from a thread pool deadlocked once under the
    container's emulation.
    """
    defs: dict[str, list[Path]] = {}
    batch = 100
    for start in range(0, len(objects), batch):
        chunk = objects[start:start + batch]
        proc = subprocess.run([nm, "-g", "--defined-only",
                               *[str(obj) for obj in chunk]],
                              capture_output=True, text=True)
        current: Path | None = chunk[0] if len(chunk) == 1 else None
        for line in proc.stdout.splitlines():
            parts = line.split()
            if line.endswith(":") and len(parts) == 1:
                name = line[:-1]
                current = next((o for o in chunk if str(o) == name), None)
                continue
            if current is not None and len(parts) == 3 and parts[1] in "TDBR":
                defs.setdefault(parts[2], []).append(current)
    duplicates = sorted(name for name, owners in defs.items() if len(owners) > 1)
    if not duplicates:
        return [], 0
    symfile = out / "gen" / "duplicate_symbols.txt"
    symfile.write_text("\n".join(duplicates) + "\n", encoding="utf-8")
    affected = sorted({obj for name in duplicates for obj in defs[name]})
    for obj in affected:
        subprocess.run([objcopy, f"--weaken-symbols={symfile}", str(obj)],
                       check=False, capture_output=True)
    return duplicates, len(affected)


def build_libraries(root: Path, target: str, out: Path, jobs: int) -> tuple[bool, list[Path]]:
    """libnitro.a and libnitrosystem.a through tools/hgss_nitro.py.

    Returns whether both built and the archives that exist. A failed archive
    does not stop the harness: the link then reports the symbols it would
    have provided, which is part of the Phase 2 work queue.
    """
    sdk_out = out / "sdk"
    nitro_cmd = [sys.executable, str(TOOLS / "hgss_nitro.py"), "make",
                 "--target", target, "--out", str(sdk_out), "--subproject", "nitro",
                 "lib", "-k", "-j", str(jobs)]
    nns_cmd = [sys.executable, str(TOOLS / "hgss_nitro.py"), "make",
               "--target", target, "--out", str(sdk_out), "--subproject", "nitrosystem",
               "lib", "-k", "-j", str(jobs)]
    ok = True
    for cmd in (nitro_cmd, nns_cmd):
        print("+ " + " ".join(cmd), flush=True)
        proc = subprocess.run(cmd)
        ok = ok and proc.returncode == 0
    libs = [p for p in (sdk_out / "libnitrosystem.a", sdk_out / "libnitro.a")
            if p.is_file()]
    if not ok:
        print("   warning: the vendored library build failed; linking with "
              f"{', '.join(p.name for p in libs) or 'no archives'} and reporting "
              "what is missing", flush=True)
    return ok and len(libs) == 2, libs


# ---------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", choices=["host32", "3ds"], default="host32")
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT,
                    help="patched pokeheartgold checkout (default: build/upstream)")
    ap.add_argument("--out", type=Path, required=True,
                    help="private output directory (objects, libraries, binary)")
    ap.add_argument("--keep-going", action="store_true",
                    help="compile every file, report per-file error counts, then link")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--skip-prepare", action="store_true",
                    help="reuse the generated headers already in --root")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    out = args.out.resolve()
    if not (root / "main.lsf").is_file():
        print(f"hgss_game: {root} is not a bootstrapped checkout "
              f"(run python3 tools/bootstrap.py)", file=sys.stderr)
        return 2
    tools = target_tools(args.target)
    out.mkdir(parents=True, exist_ok=True)
    log_dir = out / "logs"
    log_dir.mkdir(exist_ok=True)
    gen = out / "gen"
    gen.mkdir(exist_ok=True)

    if not args.skip_prepare:
        cmd = prepare_command(root, out)
        print("+ " + " ".join(cmd), flush=True)
        if subprocess.run(cmd).returncode != 0:
            print("hgss_game: header generation failed", file=sys.stderr)
            return 2

    data = gs.all_data(root)
    sources = select_sources(data)
    print(f"== {args.target}: {len(sources['c'])} C, {len(sources['cpp'])} C++, "
          f"{len(sources['asm'])} .s")

    cflags = port_cflags(args.target)
    cxxflags = port_cflags(args.target, cxx=True)
    includes = include_flags(root, out)
    overlay_header = out / "gen" / "overlay_ids.h"
    cflags += includes + ["-include", str(overlay_header)]
    cxxflags += includes + ["-include", str(overlay_header)]

    # Game objects: src/**/*.c, src/**/*.cpp, and the game-only lib sources.
    # C and C++ need different standards and, for C++, no exceptions/RTTI.
    c_jobs = [(root / rel, object_path(out, rel), "c") for rel in sources["c"]]
    cxx_jobs = [(root / rel, object_path(out, rel), "c++") for rel in sources["cpp"]]
    print(f"== compiling C/C++ ({len(c_jobs) + len(cxx_jobs)} files, -j{args.jobs})",
          flush=True)
    c_objects, c_failed = compile_objects(tools["cc"], cflags, c_jobs, args.jobs,
                                          log_dir, args.keep_going)
    cxx_objects, cxx_failed = compile_objects(tools["cc"], cxxflags, cxx_jobs,
                                              args.jobs, log_dir, args.keep_going)
    c_objects += cxx_objects
    c_failed += cxx_failed
    print(f"   {len(c_objects)} compiled, {len(c_failed)} failed")
    if c_failed:
        print(f"   {sum(e for _, e, _ in c_failed)} compiler errors in "
              f"{len(c_failed)} files")
        for src, errors, log in c_failed:
            print(f"     {errors:4d}  {src}  ({log})")

    print(f"== assembling ARM9 .s ({len(sources['asm'])} files)", flush=True)
    aflags = asm_flags(args.target, root, out)
    asm_objects, asm_failed = assemble_objects(
        root, tools["cc"], aflags, out, sources["asm"], args.jobs, log_dir,
        args.keep_going)
    print(f"   {len(asm_objects)} assembled, {len(asm_failed)} failed")
    if asm_failed:
        print(f"   {sum(e for _, e, _ in asm_failed)} errors in "
              f"{len(asm_failed)} files")
        for src, errors, log in asm_failed:
            print(f"     {errors:4d}  {src}  ({log})")

    print("== port libraries", flush=True)
    _, libs = build_libraries(root, args.target, out, args.jobs)

    print("== overlay table", flush=True)
    proc = subprocess.run(overlay_command(root, out, tools["objcopy"]),
                          capture_output=True, text=True)
    print(proc.stdout.rstrip())
    if proc.returncode != 0:
        print(proc.stderr.rstrip(), file=sys.stderr)

    game_objects = link_objects(sorted(c_objects + asm_objects), data, out)

    print("== duplicate symbols", flush=True)
    duplicates, touched = weaken_duplicate_symbols(
        tools["nm"], tools["objcopy"], game_objects, out)
    print(f"   {len(duplicates)} symbols defined in more than one object "
          f"({touched} objects weakened)")
    for name in duplicates[:40]:
        print(f"     {name}")
    if len(duplicates) > 40:
        print(f"     ... and {len(duplicates) - 40} more in "
              f"{out / 'gen' / 'duplicate_symbols.txt'}")

    print("== Wi-Fi stand-ins", flush=True)
    wifi_c = gen / "wifi_stubs.c"
    proc = run(wifi_stub_command(tools["nm"], root, game_objects, wifi_c))
    print(proc.stdout.rstrip())
    if proc.returncode != 0:
        print(proc.stderr.rstrip(), file=sys.stderr)

    app_jobs = [(src, out / "app" / (src.stem + ".o"), "c")
                for src in app_sources(args.target)]
    if wifi_c.is_file():
        app_jobs.append((wifi_c, out / "gen" / "wifi_stubs.o", "c"))
    print(f"== compiling the app ({len(app_jobs)} files)", flush=True)
    app_cflags = list(cflags)
    if args.target == "3ds":
        # main_3ds.c is the only file here that includes libctru's headers;
        # the game and the port keep NitroSDK's.
        app_cflags.append(f"-I{devkitpro() / 'libctru' / 'include'}")
    app_objects, app_failed = compile_objects(
        tools["cc"], app_cflags, app_jobs, args.jobs, log_dir, args.keep_going)
    for src, errors, log in app_failed:
        print(f"     {errors:4d}  {src}  ({log})")

    # The overlay table includes <nitro_port.h> only.
    table_src = gen / "overlay_table.c"
    table_obj = gen / "overlay_table.o"
    table_ok, table_errors, table_log = compile_one(
        tools["cc"], port_cflags(args.target) + includes,
        table_src, table_obj, "c", log_dir) if table_src.is_file() else (False, 0, "")
    if not table_ok:
        print(f"     {table_errors:4d}  overlay_table.c  ({table_log})")

    print(f"== link -> {out / 'pokeheartgold-pc' if args.target == 'host32' else out / 'pokeheartgold-3ds.elf'}",
          flush=True)
    binary = out / ("pokeheartgold-pc" if args.target == "host32"
                    else "pokeheartgold-3ds.elf")
    rsp = out / "link.rsp"
    link = link_command(tools["cc"], args.target, app_objects, game_objects,
                        [], table_obj, gen / "overlay_ids.ld",
                        libs, binary, rsp)
    proc = subprocess.run(link, capture_output=True, text=True)
    text = proc.stderr + proc.stdout
    unresolved = unresolved_symbols(text)
    (out / "link.log").write_text(text, encoding="utf-8")
    if proc.returncode == 0 and binary.is_file():
        print(f"   linked {binary}")
        return 0
    print(f"   link failed ({len(unresolved)} unresolved symbols); log: {out / 'link.log'}")
    for name in unresolved:
        print(f"     {name}")
    if not unresolved:
        print(text[-4000:])
    return 1


if __name__ == "__main__":
    sys.exit(main())
