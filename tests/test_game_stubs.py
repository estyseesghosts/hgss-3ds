"""Unit tests for tools/make_game_wifi_stubs.py and hgss/game/stubs/.

Synthetic inputs only (no game data): tiny C objects built in a temp dir,
fake SDK headers/sources, and a shim nitro_port.h. Tests that need a C
toolchain (cc, nm) skip cleanly when it is missing.
"""
import shutil
import subprocess
import sys
import re
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
STUBS = REPO / "hgss" / "game" / "stubs"
sys.path.insert(0, str(TOOLS))
import make_game_wifi_stubs as wifi  # noqa: E402

CC = shutil.which("cc")
NM = shutil.which("nm")

# Symbols the synthetic "game" object references but never defines.
# DWC_CheckUserData is defined by DEF_C (so no stub); WM_Initialize and
# SomeGameFunc must never be stubbed (WM lives in wm_stubs.c; game symbols
# are out of scope for the generator).
REF_C = """\
int DWC_Init(void);
int DWC_CheckHasProfile(void);
int DWC_CheckValidConsole(void);
int DWC_CheckUserData(void);
int WVR_StartUpAsync(int mode, void (*cb)(void *, int), void *arg);
int WVR_TerminateAsync(void (*cb)(void *), void *arg);
int SOC_Socket(int domain, int type, int protocol);
int SOC_Startup(void *config);
int DWC_CheckInet(void);
int DWC_NASLoginProcess(void);
int DWC_SVLProcess(void);
int DWC_AC_Process(void);
int DWC_CleanupInetAsync(void);
unsigned long long DWC_CreateFriendKey(void);
unsigned long long NHTTP_WideFunc(void);
unsigned long long DWCi_TestWide(void);
int VCT_CreateSession(int aid);
int WCM_SearchAsync(void);
int CPS_TestFunc(void);
int NHTTP_TestFunc(void);
int PPW_TestFunc(void);
int DWCi_TestFunc(void);
int B64Encode(void);
int WM_Initialize(void);
int SomeGameFunc(void);
static void StartCb(void *arg, int result) { (void)arg; (void)result; }
static void TermCb(void *arg) { (void)arg; }
int use_all(void) {
    return DWC_Init() + DWC_CheckHasProfile() + DWC_CheckValidConsole()
        + DWC_CheckUserData() + SOC_Socket(0, 0, 0) + SOC_Startup((void *)0)
        + DWC_CheckInet() + DWC_NASLoginProcess() + DWC_SVLProcess()
        + DWC_AC_Process() + DWC_CleanupInetAsync()
        + (int)DWC_CreateFriendKey() + (int)NHTTP_WideFunc() + (int)DWCi_TestWide()
        + VCT_CreateSession(0) + WCM_SearchAsync() + CPS_TestFunc()
        + NHTTP_TestFunc() + PPW_TestFunc() + DWCi_TestFunc() + B64Encode()
        + WM_Initialize() + SomeGameFunc()
        + WVR_StartUpAsync(8, StartCb, (void *)0)
        + WVR_TerminateAsync(TermCb, (void *)0);
}
"""

DEF_C = """\
int DWC_CheckUserData(void) { return 0; }
"""

FAKE_H = """\
typedef unsigned long long u64;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef struct DWCUserData DWCUserData;
u64 DWC_CreateFriendKey(const DWCUserData *user);
uint64_t NHTTP_WideFunc(void);
int64_t DWCi_TestWide(void);
int DWC_Init(void *work);
int SOC_Socket(int domain, int type, int protocol);
int WM_Initialize(void *buffer, void (*cb)(void *), unsigned dma);
"""

FAKE_SDK_C = """\
int B64Encode(void *in, int inlen, char *out, int outlen) {
    (void)in; (void)inlen; (void)out; (void)outlen;
    return 0;
}
"""

SHIM_NITRO_PORT_H = """\
#ifndef NITRO_PORT_H_
#define NITRO_PORT_H_
int NitroPort_Unimplemented(const char *what);
#endif
"""

RECORDER_C = """\
#include <string.h>
#include "nitro_port.h"
static const char *seen[256];
static int nseen;
int NitroPort_Unimplemented(const char *what) {
    int i;
    for (i = 0; i < nseen; i++) {
        if (strcmp(seen[i], what) == 0) {
            return nseen;
        }
    }
    if (nseen < 256) {
        seen[nseen++] = what;
    }
    return nseen;
}
/* Test hook: nonzero once the stub reported `what`. */
int TestStubReported(const char *what) {
    int i;
    for (i = 0; i < nseen; i++) {
        if (strcmp(seen[i], what) == 0) {
            return 1;
        }
    }
    return 0;
}
"""

WIFI_DRIVER_C = """\
#include <assert.h>
#include <stddef.h>
int DWC_Init(void);
int DWC_CheckHasProfile(void);
int DWC_CheckValidConsole(void);
int SOC_Socket(int domain, int type, int protocol);
int SOC_Startup(void *config);
int DWC_CheckInet(void);
int DWC_NASLoginProcess(void);
int DWC_SVLProcess(void);
int DWC_AC_Process(void);
int DWC_CleanupInetAsync(void);
unsigned long long DWC_CreateFriendKey(void);
unsigned long long NHTTP_WideFunc(void);
unsigned long long DWCi_TestWide(void);
int VCT_CreateSession(int aid);
int WCM_SearchAsync(void);
int CPS_TestFunc(void);
int NHTTP_TestFunc(void);
int PPW_TestFunc(void);
int DWCi_TestFunc(void);
int B64Encode(void);
int WVR_StartUpAsync(int vram, void (*cb)(void *, int), void *arg);
int WVR_TerminateAsync(void (*cb)(void *, int), void *arg);
int TestStubReported(const char *what);
static void *seen_arg;
static int seen_result;
static int ncalls;
static void StartCb(void *arg, int result) {
    seen_arg = arg;
    seen_result = result;
    ncalls++;
}
int main(void) {
    /* WVR_StartUpAsync: boot needs return 1 and cb(arg, 0). */
    assert(WVR_StartUpAsync(8, StartCb, (void *)0x1234) == 1);
    assert(ncalls == 1 && seen_arg == (void *)0x1234 && seen_result == 0);
    assert(WVR_StartUpAsync(8, NULL, NULL) == 1 && ncalls == 1);
    assert(TestStubReported("WVR_StartUpAsync"));
    /* WVR_TerminateAsync: same contract. */
    assert(WVR_TerminateAsync(StartCb, (void *)0x5678) == 1);
    assert(ncalls == 2 && seen_arg == (void *)0x5678 && seen_result == 0);
    assert(WVR_TerminateAsync(NULL, NULL) == 1 && ncalls == 2);
    /* Boot/wireless checks. */
    assert(DWC_Init() == 0);
    assert(DWC_CheckHasProfile() == 0);
    assert(DWC_CheckValidConsole() == 0);
    /* Sockets fail; polls report finished-and-failed. */
    assert(SOC_Socket(0, 0, 0) == -1);
    assert(SOC_Startup(NULL) == -1);
    assert(DWC_CheckInet() == 1);
    assert(DWC_NASLoginProcess() == 4);
    assert(DWC_SVLProcess() == 4);
    assert(DWC_AC_Process() == -1);
    assert(DWC_CleanupInetAsync() == 1);
    /* Wide returns come back zero in both registers. */
    assert(DWC_CreateFriendKey() == 0ull);
    assert(NHTTP_WideFunc() == 0ull);
    assert(DWCi_TestWide() == 0ull);
    /* Generic online functions fail with 0. */
    assert(VCT_CreateSession(0) == 0);
    assert(WCM_SearchAsync() == 0);
    assert(CPS_TestFunc() == 0);
    assert(NHTTP_TestFunc() == 0);
    assert(PPW_TestFunc() == 0);
    assert(DWCi_TestFunc() == 0);
    assert(B64Encode() == 0);
    return 0;
}
"""

DSProt_DRIVER_C = """\
#include <assert.h>
#include <stddef.h>
typedef unsigned int u32;
unsigned DSProt_DetectFlashcart(void *callback);
unsigned DSProt_DetectNotFlashcart(void *callback);
unsigned DSProt_DetectEmulator(void *callback);
unsigned DSProt_DetectNotEmulator(void *callback);
unsigned DSProt_DetectDummy(void *callback);
unsigned DSProt_DetectNotDummy(void *callback);
static int ncalls;
static void DummyCb(void) { ncalls++; }
int main(void) {
    int before;
    /* Return values: the genuine-cart answers. */
    assert(DSProt_DetectFlashcart(NULL) == 0);
    assert(DSProt_DetectNotFlashcart(NULL) == 1);
    assert(DSProt_DetectEmulator(NULL) == 0);
    assert(DSProt_DetectNotEmulator(NULL) == 1);
    assert(DSProt_DetectDummy(NULL) == 0);
    assert(DSProt_DetectNotDummy(NULL) == 1);
    /* Callbacks run exactly when the check passes (dsprot_main.c). */
    before = ncalls;
    assert(DSProt_DetectFlashcart(DummyCb) == 0 && ncalls == before);
    assert(DSProt_DetectNotFlashcart(DummyCb) == 1 && ncalls == before + 1);
    assert(DSProt_DetectEmulator(DummyCb) == 0 && ncalls == before + 1);
    assert(DSProt_DetectNotEmulator(DummyCb) == 1 && ncalls == before + 2);
    assert(DSProt_DetectDummy(DummyCb) == 0 && ncalls == before + 2);
    assert(DSProt_DetectNotDummy(DummyCb) == 1 && ncalls == before + 3);
    /* overlay_124.c: key must stay divisible by both primes. */
    {
        u32 key = 2441u * 4073u;
        key += 769u * (!DSProt_DetectNotFlashcart(DummyCb));
        key += 47u * (!DSProt_DetectNotDummy(DummyCb));
        u32 key2 = 929u * DSProt_DetectEmulator(DummyCb);
        assert((key + key2) % 2441u == 0);
        assert((key + key2) % 4073u == 0);
    }
    /* fieldmap.c map-init: offset must stay a multiple of 3433 and 4217. */
    {
        u32 offset = 0xDCE6A1u;
        offset += (0x4CFu * DSProt_DetectDummy(DummyCb));
        offset += (0x6Bu * (DSProt_DetectNotEmulator(DummyCb) == 0));
        offset += DSProt_DetectFlashcart(DummyCb) * 0x3A1u;
        assert(offset % 3433u == 0);
        assert(offset % 4217u == 0);
    }
    /* fieldmap.c map-exit: unkVal must stay a multiple of 2221. */
    {
        int offset = 0x2AAACF;
        offset += 0x23B * DSProt_DetectDummy(DummyCb);
        offset += 0x18D * (DSProt_DetectNotFlashcart(DummyCb) == 0);
        int notEmulator = DSProt_DetectNotEmulator(DummyCb) == 0;
        unsigned unkVal = (unsigned)(notEmulator * 0x8B + offset);
        assert(unkVal % 2221u == 0);
    }
    /* touch_save_app.c, touchscreen_menu.c, pokedex_app.c: none of the
     * heap-clobbering branches may trigger. */
    assert((!DSProt_DetectNotFlashcart(DummyCb)) == 0);
    assert(DSProt_DetectEmulator(DummyCb) == 0);
    assert(DSProt_DetectDummy(DummyCb) == 0);
    assert(DSProt_DetectFlashcart(DummyCb) == 0);
    assert((!DSProt_DetectNotEmulator(DummyCb)) == 0);
    assert((!DSProt_DetectNotDummy(DummyCb)) == 0);
    /* The checks discriminate: a pirated answer trips every tripwire. */
    assert((2441u * 4073u + 769u * 1u) % 2441u != 0);
    assert((2441u * 4073u + 47u * 1u) % 4073u != 0);
    assert((929u * 1u) % 2441u != 0);
    assert((0xDCE6A1u + 0x4CFu * 1u) % 3433u != 0);
    assert((0xDCE6A1u + 0x6Bu * 1u) % 4217u != 0);
    assert((0xDCE6A1u + 0x3A1u * 1u) % 3433u != 0);
    assert(((unsigned)(0x2AAACFu + 0x23Bu * 1u)) % 2221u != 0);
    assert(((unsigned)(0x2AAACFu + 0x18Du * 1u)) % 2221u != 0);
    assert(((unsigned)(1u * 0x8Bu + 0x2AAACFu)) % 2221u != 0);
    return 0;
}
"""

WM_DRIVER_C = """\
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static int nreported;
static char reported[256][96];
static int nrec_calls;
int NitroPort_Unimplemented(const char *what) {
    if (nreported < 256) {
        strncpy(reported[nreported], what, 95);
        reported[nreported][95] = '\\0';
        nreported++;
    }
    return nreported;
}
static void RecCb(void *arg) {
    (void)arg;
    nrec_calls++;
}
#define STUBS_PATH "@STUBS_PATH@"
#include STUBS_PATH
static int was_reported(const char *what) {
    int i;
    for (i = 0; i < nreported; i++) {
        if (strcmp(reported[i], what) == 0) {
            return 1;
        }
    }
    return 0;
}
/* Async ARM7 round-trips: WM_ERRCODE_WM_DISABLE, callback never invoked. */
#define CHECK_ASYNC1(fn) do { \\
        int before = nrec_calls; \\
        assert(fn(RecCb) == 4); \\
        assert(nrec_calls == before); \\
        assert(was_reported(#fn)); \\
    } while (0)
int main(void) {
    WMOtherElementResults elems;
    /* Local-only lifecycle and callback registration succeed (real WM
     * answers these without the ARM7; the game hangs or errors otherwise). */
    assert(WM_Init(NULL, 2) == 0);
    assert(WM_Finish() == 0);
    {
        int before = nrec_calls;
        assert(WM_SetIndCallback(RecCb) == 0);
        assert(WM_SetPortCallback(14, RecCb, NULL) == 0);
        assert(nrec_calls == before);
    }
    CHECK_ASYNC1(WM_Enable);
    CHECK_ASYNC1(WM_Disable);
    CHECK_ASYNC1(WM_PowerOn);
    CHECK_ASYNC1(WM_PowerOff);
    CHECK_ASYNC1(WM_Reset);
    CHECK_ASYNC1(WM_End);
    CHECK_ASYNC1(WM_EndParent);
    CHECK_ASYNC1(WM_EndScan);
    CHECK_ASYNC1(WM_EndMP);
    CHECK_ASYNC1(WM_EndDCF);
    assert(WM_Initialize(NULL, RecCb, 2) == 4);
    assert(WM_InitializeForListening(NULL, RecCb, 2, 0) == 4);
    assert(WM_SetParentParameter(RecCb, NULL) == 4);
    assert(WM_StartParent(RecCb) == 4);
    assert(WM_StartScan(RecCb, NULL) == 4);
    assert(WM_StartScanEx(RecCb, NULL) == 4);
    assert(WM_StartConnectEx(RecCb, NULL, NULL, 0, 0) == 4);
    assert(WM_Disconnect(RecCb, 0) == 4);
    assert(WM_Disconnect(NULL, 0) == 4);
    assert(WM_StartMP(RecCb, NULL, 0, NULL, 0, 0) == 4);
    assert(WM_StartMPEx(RecCb, NULL, 0, NULL, 0, 0, 0, 0, 0, 0, 0) == 4);
    assert(WM_SetMPDataToPortEx(RecCb, NULL, NULL, 0, 0, 0, 0) == 4);
    assert(WM_StartDCF(RecCb, NULL, 0) == 4);
    assert(WM_SetDCFData(RecCb, NULL, NULL, 0) == 4);
    assert(WM_StartDataSharing(NULL, 0, 0, 0, 0) == 4);
    assert(WM_EndDataSharing(NULL) == 4);
    assert(WM_StepDataSharing(NULL, NULL, NULL) == 4);
    assert(WM_StartKeySharing(NULL, 0) == 4);
    assert(WM_EndKeySharing(NULL) == 4);
    assert(WM_SetWEPKey(RecCb, 0, NULL) == 4);
    assert(WM_SetWEPKeyEx(RecCb, 0, 0, NULL) == 4);
    assert(WM_SetGameInfo(NULL, NULL, 0, 0, 0, 0) == 4);
    assert(WM_SetBeaconIndication(RecCb, 0) == 4);
    assert(WM_SetLifeTime(RecCb, 0, 0, 0, 0) == 4);
    assert(WM_MeasureChannel(RecCb, 0, 0, 0, 0) == 4);
    assert(WM_SetEntry(RecCb, 0) == 4);
    assert(WM_ReadStatus(NULL) == 4);
    assert(WMi_GetStatusAddress() == NULL);
    assert(nrec_calls == 0);
    /* Getters: the uninitialised-wireless answers. */
    assert(WM_GetAllowedChannel() == 0x8000);
    assert(WM_GetLinkLevel() == 0);
    assert(WM_GetNextTgid() == 0);
    assert(WM_GetAID() == 0);
    assert(WM_GetConnectedAIDs() == 0);
    assert(WM_GetMPSendBufferSize() == 0);
    assert(WM_GetMPReceiveBufferSize() == 0);
    assert(WM_GetDispersionBeaconPeriod() == 0);
    assert(WM_GetDispersionScanPeriod() == 0);
    assert(WM_ReadMPData(NULL, 0) == NULL);
    assert(WM_GetSharedDataAddress(NULL, NULL, 0) == NULL);
    elems = WM_GetOtherElements(NULL);
    assert(elems.count == 0);
    /* By-value layout must match wm_internal.h: with 4-byte pointers (ARM,
     * -m32 host) this is count@0, element@4, id@0, length@1, body@4, stride
     * 8, size 132. Pointer alignment shifts body/element offsets on 64-bit
     * hosts, so only the target-independent offsets are asserted here; the
     * exact declaration text is checked against the SDK header below. */
    assert(offsetof(WMOtherElementResults, count) == 0);
    assert(offsetof(WMOtherElement, id) == 0);
    assert(offsetof(WMOtherElement, length) == 1);
    return 0;
}
"""


def run(argv, cwd):
    proc = subprocess.run(argv, capture_output=True, text=True, cwd=cwd)
    assert proc.returncode == 0, "{}\n{}".format(" ".join(argv), proc.stderr)
    return proc


class WifiGeneratorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "ref.c").write_text(REF_C)
        (self.root / "def.c").write_text(DEF_C)
        (self.root / "dwc_account_internal.h").write_text(FAKE_H)
        sdk = self.root / "sdk" / "NitroDWC" / "src"
        sdk.mkdir(parents=True)
        (sdk / "b64.c").write_text(FAKE_SDK_C)
        self.out = self.root / "wifi_stubs.c"

    def needs_toolchain(self):
        if CC is None or NM is None:
            self.skipTest("no C toolchain (cc/nm)")

    def build_objects(self):
        self.needs_toolchain()
        ref_o = self.root / "ref.o"
        def_o = self.root / "def.o"
        run([CC, "-c", str(self.root / "ref.c"), "-o", str(ref_o)], self.root)
        run([CC, "-c", str(self.root / "def.c"), "-o", str(def_o)], self.root)
        return [str(ref_o), str(def_o)]

    def generate(self, objects):
        cmd = [sys.executable, str(TOOLS / "make_game_wifi_stubs.py"),
               "--nm", NM, "--headers", str(self.root),
               "--sources", str(self.root / "sdk" / "NitroDWC"),
               "--output", str(self.out)] + objects
        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=self.root)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return self.out.read_text()

    def test_stub_selection(self):
        text = self.generate(self.build_objects())
        # Needed stand-ins are emitted ...
        for name in ("DWC_Init", "DWC_CheckHasProfile", "DWC_CheckValidConsole",
                     "SOC_Socket", "SOC_Startup", "DWC_CheckInet",
                     "DWC_NASLoginProcess", "DWC_SVLProcess", "DWC_AC_Process",
                     "DWC_CleanupInetAsync", "DWC_CreateFriendKey",
                     "NHTTP_WideFunc", "DWCi_TestWide", "VCT_CreateSession",
                     "WCM_SearchAsync", "CPS_TestFunc", "NHTTP_TestFunc",
                     "PPW_TestFunc", "DWCi_TestFunc", "B64Encode",
                     "WVR_StartUpAsync", "WVR_TerminateAsync"):
            self.assertIn(name, text, name)
        # ... with C spelling (Mach-O nm prefixes symbols with '_') ...
        self.assertIn("int DWC_Init(void)", text)
        self.assertNotIn("_DWC_Init", text)
        # ... while defined, game and WM symbols are left alone.
        for absent in ("DWC_CheckUserData", "WM_Initialize", "SomeGameFunc"):
            self.assertNotIn(absent, text, absent)

    def test_stub_bodies(self):
        text = self.generate(self.build_objects())
        # WVR pair: boot needs return 1 with cb(arg, 0), in both bodies.
        self.assertEqual(text.count("cb(arg, 0)"), 2)
        self.assertEqual(text.count("return 1;"), 2)
        self.assertIn("int WVR_StartUpAsync(int vram, void (*cb)(void *, int), void *arg)", text)
        self.assertIn("int WVR_TerminateAsync(void (*cb)(void *, int), void *arg)", text)
        # 64-bit returns use both registers.
        self.assertIn("unsigned long long DWC_CreateFriendKey(void)", text)
        self.assertIn("unsigned long long NHTTP_WideFunc(void)", text)
        self.assertIn("unsigned long long DWCi_TestWide(void)", text)
        # Audited overrides.
        for snippet in ('SOC_Socket(void) { return Unavailable("SOC_Socket", -1); }',
                        'SOC_Startup(void) { return Unavailable("SOC_Startup", -1); }',
                        'DWC_CheckInet(void) { return Unavailable("DWC_CheckInet", 1); }',
                        'DWC_NASLoginProcess(void) { return Unavailable("DWC_NASLoginProcess", 4); }',
                        'DWC_SVLProcess(void) { return Unavailable("DWC_SVLProcess", 4); }',
                        'DWC_AC_Process(void) { return Unavailable("DWC_AC_Process", -1); }',
                        'DWC_CleanupInetAsync(void) { return Unavailable("DWC_CleanupInetAsync", 1); }',
                        'DWC_Init(void) { return Unavailable("DWC_Init", 0); }'):
            self.assertIn(snippet, text, snippet)
        # Generic online functions fail with 0.
        for snippet in ('VCT_CreateSession(void) { return Unavailable("VCT_CreateSession", 0); }',
                        'WCM_SearchAsync(void) { return Unavailable("WCM_SearchAsync", 0); }',
                        'CPS_TestFunc(void) { return Unavailable("CPS_TestFunc", 0); }',
                        'B64Encode(void) { return Unavailable("B64Encode", 0); }'):
            self.assertIn(snippet, text, snippet)

    def test_generated_stubs_run(self):
        """Compile the output and run it: the boot-critical contract holds."""
        self.needs_toolchain()
        self.generate(self.build_objects())
        inc = self.root / "shim"
        inc.mkdir()
        (inc / "nitro_port.h").write_text(SHIM_NITRO_PORT_H)
        (self.root / "recorder.c").write_text(RECORDER_C)
        (self.root / "driver.c").write_text(WIFI_DRIVER_C)
        exe = self.root / "wifi_driver"
        run([CC, "-Wall", "-Wextra", "-Werror", "-I", str(inc),
             str(self.out), str(self.root / "recorder.c"),
             str(self.root / "driver.c"), "-o", str(exe)], self.root)
        proc = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_wide_returns_textual(self):
        names = {"DWC_CreateFriendKey", "NHTTP_WideFunc", "DWCi_TestWide",
                 "DWC_Init", "SOC_Socket"}
        self.assertEqual(wifi.wide_returns([self.root], names),
                         {"DWC_CreateFriendKey", "NHTTP_WideFunc", "DWCi_TestWide"})

    def test_wifi_source_names(self):
        names = wifi.wifi_source_names([self.root / "sdk"])
        self.assertIn("B64Encode", names)


class DsprotStubsTest(unittest.TestCase):
    def test_genuine_cart(self):
        if CC is None:
            self.skipTest("no C toolchain (cc)")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "driver.c").write_text(DSProt_DRIVER_C)
            exe = root / "dsprot_driver"
            run([CC, "-Wall", "-Wextra", "-Werror",
                 str(STUBS / "dsprot_stubs.c"), str(root / "driver.c"),
                 "-o", str(exe)], root)
            proc = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)


class WmStubsTest(unittest.TestCase):
    UPSTREAM_WM_H = (REPO / "build" / "upstream" / "lib" / "include"
                     / "nitro" / "wm" / "wm_internal.h")

    def test_struct_layout_matches_sdk(self):
        """The by-value WMOtherElement* copies track wm_internal.h exactly."""

        def block(text, struct):
            m = re.search(r"typedef struct " + struct + r" \{(.*?)\} " + struct + ";",
                          text, re.S)
            self.assertIsNotNone(m, struct)
            return " ".join(m.group(1).split())

        if not self.UPSTREAM_WM_H.is_file():
            self.skipTest("no build/upstream checkout")
        sdk = self.UPSTREAM_WM_H.read_text(encoding="utf-8", errors="replace")
        stub = (STUBS / "wm_stubs.c").read_text(encoding="utf-8")
        for struct in ("WMOtherElement", "WMOtherElementResults"):
            self.assertEqual(block(stub, struct), block(sdk, struct), struct)

    def test_wireless_off(self):
        if CC is None:
            self.skipTest("no C toolchain (cc)")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            driver = WM_DRIVER_C.replace("@STUBS_PATH@", str(STUBS / "wm_stubs.c"))
            (root / "driver.c").write_text(driver)
            exe = root / "wm_driver"
            run([CC, "-Wall", "-Wextra", "-Werror",
                 str(root / "driver.c"), "-o", str(exe)], root)
            proc = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_covers_full_wm_api(self):
        """Every public WM_ function the game can call has a stand-in, so
        the linker never pulls the SDK's real wm_*.o (whose async callbacks
        would never fire)."""
        text = (STUBS / "wm_stubs.c").read_text()
        for name in ("WM_Init", "WM_Finish", "WM_Enable", "WM_Disable",
                     "WM_PowerOn", "WM_PowerOff", "WM_Initialize",
                     "WM_InitializeForListening", "WM_Reset", "WM_End",
                     "WM_SetParentParameter", "WM_StartParent", "WM_EndParent",
                     "WM_StartScan", "WM_StartScanEx", "WM_EndScan",
                     "WM_StartConnectEx", "WM_Disconnect", "WM_StartMP",
                     "WM_StartMPEx", "WM_EndMP", "WM_SetMPDataToPortEx",
                     "WM_StartDCF", "WM_SetDCFData", "WM_EndDCF",
                     "WM_StartDataSharing", "WM_EndDataSharing",
                     "WM_StepDataSharing", "WM_GetSharedDataAddress",
                     "WM_StartKeySharing", "WM_EndKeySharing", "WM_SetWEPKey",
                     "WM_SetWEPKeyEx", "WM_SetGameInfo", "WM_SetBeaconIndication",
                     "WM_SetLifeTime", "WM_MeasureChannel", "WM_SetEntry",
                     "WM_SetIndCallback", "WM_SetPortCallback", "WM_ReadStatus",
                     "WM_GetMPSendBufferSize", "WM_GetMPReceiveBufferSize",
                     "WM_ReadMPData", "WM_GetAllowedChannel", "WM_GetLinkLevel",
                     "WM_GetDispersionBeaconPeriod", "WM_GetDispersionScanPeriod",
                     "WM_GetOtherElements", "WM_GetNextTgid", "WM_GetAID",
                     "WM_GetConnectedAIDs", "WMi_GetStatusAddress"):
            self.assertRegex(text, r"[\s*]" + name + r"\s*\(", name)


if __name__ == "__main__":
    unittest.main()
