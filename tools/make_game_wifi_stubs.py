#!/usr/bin/env python3
"""Write "wireless unavailable" stand-ins for the Wi-Fi SDK functions the game calls.

Usage:
    python tools/make_game_wifi_stubs.py --nm NM --headers DIR [DIR ...] \
        --output wifi_stubs.c OBJECT [OBJECT ...]

The original game links Nintendo's Wi-Fi libraries: NitroDWC (DWC_), the
Pokemon Wi-Fi lobby (PPW_), voice chat (VCT_), NitroWiFi (SOC_, WCM_ ...) and
the wireless-voice helper (WVR_). Online play isn't part of the port, so it
doesn't link them. This lists every such symbol the given game objects use and
none of them defines (from `nm`), plus anything defined by the Wi-Fi SDKs' own
sources (--sources: build/upstream/lib/NitroDWC lib/NitroWiFi lib/libVCT
lib/libCPS, whose names don't all carry a prefix), and writes a C file with
one stand-in each. A stand-in logs "not implemented: <name>" once
(NitroPort_Unimplemented) and returns the SDK's "unavailable" result, so the
game's online menus take their own can't-connect error paths.

WM_* (local wireless) is not covered here: it has its own hand-written
stand-ins in hgss/game/stubs/wm_stubs.c, since most WM_ calls need the exact
"wireless off" error code rather than 0. dsprot likewise lives in
hgss/game/stubs/dsprot_stubs.c.

The stand-ins don't include the SDK headers (they disagree with each
other's types), so the generic ones ignore their arguments and return int.
Where a header in --headers says a function returns a 64-bit value, the
stand-in returns a 64-bit 0 instead, since those come back in two registers
on ARM. A few functions need exact behaviour, not just 0; see OVERRIDES and
the WVR pair below, each audited against its callers in build/upstream/src.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

WIFI = re.compile(r"(DWC|DWCi|PPW|VCT|WVR|SOC|SOCL|WCM|CPS|NHTTP)_\w+\Z")
WIDE_RETURN = re.compile(r"\b(u64|s64|OSTick|int64_t|uint64_t|unsigned\s+long\s+long|long\s+long)\b")

# Stand-in results other than the default 0, audited against src/ callers.
# Every caller below was read; each takes its own failure path with the value
# given, and none hangs waiting for an async callback the stub never makes.
# All values are int-sized (no 64-bit function needs an override).
OVERRIDES = {
    # main.c shows the WFC user-info warning when Comm_LoadAndInitDwc == 3
    # (comm_state.c returns DWC_Init's value); 0 skips it.
    "DWC_Init": "0",
    # No WFC profile / console data on the port: Comm_HasWifiProfile is FALSE
    # and wifi_connection.c takes its "no profile" branch.
    "DWC_CheckHasProfile": "0",
    "DWC_CheckValidConsole": "0",
    # Callers test `== -1` / `< 0` (global_terminal_socket.c,
    # wifi_setup_platform.c, wifi_setup_worker.c, wifi_setup_link.c); 0 would
    # look like a valid socket / success.
    "SOC_Socket": "-1",
    "SOC_Startup": "-1",
    # Polled "is the async step done" predicates. TRUE lets the caller read
    # the status below, which reports NOT_INITIALIZED (0) and takes the error
    # branch; FALSE would spin in the wait state calling a no-op pump.
    "DWC_CheckInet": "1",
    # Poll loops that exit only on SUCCESS/ERROR/CANCELED; 0 (DIRTY/IDLE)
    # would spin, per frame (NASLogin) or hard (SVL's for(;;)).
    "DWC_NASLoginProcess": "4",  # DWC_NASLOGIN_STATE_ERROR
    "DWC_SVLProcess": "4",  # DWC_SVL_STATE_ERROR
    # Connection test: nonzero ends the test, < 0 shows the error screen.
    "DWC_AC_Process": "-1",
    # 1 means "cleanup finished"; the DISCONNECT wait states exit on it.
    "DWC_CleanupInetAsync": "1",
}


def symbols(nm: str, objects: list[str]) -> tuple[set[str], set[str]]:
    # In batches: all ~1,100 objects on one command line would pass Windows'
    # 32 KB command-line limit (building in MSYS2).
    out = "".join(subprocess.run([nm, *objects[i:i + 200]], check=True, capture_output=True,
                                 text=True).stdout
                  for i in range(0, len(objects), 200))
    defined: set[str] = set()
    undefined: set[str] = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            undefined.add(_strip_underscore(parts[1]))
        elif len(parts) == 3:
            defined.add(_strip_underscore(parts[2]))
    return defined, undefined


def _strip_underscore(name: str) -> str:
    # Mach-O nm prefixes every C symbol with one '_'; ELF nm does not.
    # Match and emit the C spelling so one output links on both.
    return name[1:] if name.startswith("_") else name


def wifi_source_names(source_dirs: list[Path]) -> set[str]:
    """Functions defined in the Wi-Fi SDKs' own sources (NitroDWC's GameSpy
    code ...), whose names don't all carry a prefix: B64Encode, ..."""
    names: set[str] = set()
    definition = re.compile(r"^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*(?://[^\n]*)?\s*\{", re.M)
    for folder in source_dirs:
        for path in list(folder.rglob("*.c")) + list(folder.rglob("*.cpp")):
            names.update(definition.findall(path.read_text(encoding="utf-8", errors="replace")))
    return names


def wide_returns(header_dirs: list[Path], names: set[str]) -> set[str]:
    """Names whose declaration in the SDK headers returns a 64-bit value."""
    text = "\n".join(h.read_text(encoding="utf-8", errors="replace")
                     for d in header_dirs for h in d.rglob("*.h"))
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    wide = set()
    for name in names:
        match = re.search(r"([A-Za-z_][\w \t\*]*?)\b" + re.escape(name) + r"\s*\(", text)
        if match and WIDE_RETURN.search(match.group(1)):
            wide.add(name)
    return wide


def _wvr_startup() -> str:
    # comm_wireless.c aborts unless this returns 1 (RESULT_SUCCESS in wvr.c),
    # and its callback aborts unless result == 0. There is no wireless ARM7 on
    # the port, so report success straight away; VRAM bank C/D stay the
    # game's, which is what "voice chat off, wireless on" needs. Signature
    # matches lib/NitroSDK/src/wvr.c (WvrAsyncCallback).
    return "\n".join([
        "int WVR_StartUpAsync(int vram, void (*cb)(void *, int), void *arg)",
        "{",
        '    NitroPort_Unimplemented("WVR_StartUpAsync");',
        "    (void)vram;",
        "    if (cb) {",
        "        cb(arg, 0);",
        "    }",
        "    return 1;",
        "}",
    ])


def _wvr_terminate() -> str:
    # CommWireless_Terminate's callback takes no result argument, but the
    # call is safe either way: on ARM an ignored trailing int arrives in r1.
    # The canonical two-argument signature (wvr.c) is used so a genuine cart
    # and the port behave the same at the call site.
    return "\n".join([
        "int WVR_TerminateAsync(void (*cb)(void *, int), void *arg)",
        "{",
        '    NitroPort_Unimplemented("WVR_TerminateAsync");',
        "    if (cb) {",
        "        cb(arg, 0);",
        "    }",
        "    return 1;",
        "}",
    ])


WVR_BODIES = {
    "WVR_StartUpAsync": _wvr_startup,
    "WVR_TerminateAsync": _wvr_terminate,
}


def render(names: list[str], wide: set[str]) -> str:
    lines = [
        "/* Generated by tools/make_game_wifi_stubs.py: stand-ins for the Wi-Fi SDK",
        " * functions the game calls. Online play is not part of the port; each",
        " * reports itself once and returns the SDK's \"unavailable\" result, so the",
        " * game shows its own connection-error screens. Arguments are ignored,",
        " * except that the WVR pair completes its callback (see below). */",
        "#include <nitro_port.h>",
        "",
        "static int Unavailable(const char *name, int result)",
        "{",
        "    NitroPort_Unimplemented(name);",
        "    return result;",
        "}",
        "",
    ]
    for name in names:
        if name in WVR_BODIES:
            lines.append(WVR_BODIES[name]())
        elif name in wide:
            lines.append(f"unsigned long long {name}(void)"
                         f" {{ return (unsigned long long)Unavailable(\"{name}\", 0); }}")
        else:
            lines.append(f"int {name}(void)"
                         f" {{ return Unavailable(\"{name}\", {OVERRIDES.get(name, '0')}); }}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--headers", type=Path, nargs="+", default=[])
    parser.add_argument("--sources", type=Path, nargs="*", default=[],
                        help="Wi-Fi SDK source folders: anything they define counts as Wi-Fi")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    defined, undefined = symbols(args.nm, args.objects)
    from_sources = wifi_source_names(args.sources)
    names = sorted(s for s in undefined - defined if WIFI.match(s) or s in from_sources)
    text = render(names, wide_returns(args.headers, set(names)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.is_file() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8")
    print(f"  {len(names)} Wi-Fi SDK functions stood in for (online play is not ported)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
