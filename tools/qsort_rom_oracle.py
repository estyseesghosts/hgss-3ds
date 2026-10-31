#!/usr/bin/env python3
"""Independent oracle for the SDK_PORT_3DS C version of MATH_QSort.

Runs the ORIGINAL machine code of MATH_QSort, taken from a decompressed ARM9 image of the user's own
retail ROM (see tools/rom_compare.py's blz_decode), in the `unicorn` ARM emulator, logs every call to the
comparator and the final array, and compares with the C version built from the patched math_qsort.c on
the same inputs. This does not depend on any interpretation of the asm text.

Usage (inside the dev container, with `pip install unicorn` in a venv):
  qsort_rom_oracle.py ARM9_DECOMPRESSED.bin MAIN_ELF PATCHED_ROOT
No game data is written to the repo; the cases are synthetic.
"""
import random
import subprocess
import struct
import sys
import tempfile
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

BASE = 0x02000000
HEAP = 0x03000000      # array
WORK = 0x03800000      # optional work buffer
STACK = 0x03F00000
CMP = 0x10000000       # comparator trampoline: a single `bx lr`, r0 set by the hook
RET = 0x10001000


def key(elem, width):
    return int.from_bytes(elem[:min(width, 4)], "little")


def run_rom(code, addr, arr, count, width, use_work):
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(BASE, 0x400000, UC_PROT_ALL)
    mu.mem_write(addr, code)
    mu.mem_map(HEAP, 0x100000, UC_PROT_ALL)
    mu.mem_map(WORK, 0x100000, UC_PROT_ALL)
    mu.mem_map(STACK - 0x100000, 0x100000 + 0x1000, UC_PROT_ALL)
    mu.mem_map(CMP, 0x2000, UC_PROT_ALL)
    mu.mem_write(CMP, struct.pack("<I", 0xE12FFF1E))  # bx lr
    mu.mem_write(RET, struct.pack("<I", 0xE12FFF1E))
    mu.mem_write(HEAP, arr)
    sp = STACK - 16
    mu.reg_write(UC_ARM_REG_SP, sp - 4)
    mu.mem_write(sp - 4, struct.pack("<I", WORK if use_work else 0))  # 5th argument
    mu.reg_write(UC_ARM_REG_R0, HEAP)
    mu.reg_write(UC_ARM_REG_R1, count)
    mu.reg_write(UC_ARM_REG_R2, width)
    mu.reg_write(UC_ARM_REG_R3, CMP)
    mu.reg_write(UC_ARM_REG_LR, RET)
    calls = []

    def hook(uc, address, size, _):
        if address == CMP:
            a = uc.reg_read(UC_ARM_REG_R0)
            b = uc.reg_read(UC_ARM_REG_R1)
            ea = bytes(uc.mem_read(a, width))
            eb = bytes(uc.mem_read(b, width))
            ka, kb = key(ea, width), key(eb, width)
            r = (ka > kb) - (ka < kb)
            calls.append(((a - HEAP) // width, (b - HEAP) // width))
            uc.reg_write(UC_ARM_REG_R0, r & 0xFFFFFFFF)

    mu.hook_add(UC_HOOK_CODE, hook, begin=CMP, end=CMP)
    mu.emu_start(addr, RET, count=50_000_000)
    return calls, bytes(mu.mem_read(HEAP, count * width))


def make_case(rng, count, width, shape):
    elems = []
    for i in range(count):
        if shape == "random":
            k = rng.randrange(0, 1 << min(8 * min(width, 4), 31))
        elif shape == "dups":
            k = rng.randrange(0, 4)
        elif shape == "sorted":
            k = i
        elif shape == "reverse":
            k = count - i
        else:  # equal
            k = 7
        k &= (1 << (8 * min(width, 4))) - 1
        e = bytearray(k.to_bytes(min(width, 4), "little"))
        e += bytes((i >> (8 * j)) & 0xFF for j in range(max(0, width - 4)))  # identity beyond the key
        elems.append(bytes(e))
    return b"".join(elems)


def main():
    arm9 = Path(sys.argv[1]).read_bytes()
    elf, root = sys.argv[2], Path(sys.argv[3]).resolve()
    nm = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True).stdout
    row = next(l.split() for l in nm.splitlines() if l.endswith(" MATH_QSort"))
    addr, size = int(row[0], 16), int(row[1], 16)
    code = arm9[addr - BASE:addr - BASE + size]
    rng = random.Random(0x5EED)
    cases = []
    for width in (1, 2, 3, 4, 5, 6, 8, 12, 16):
        for count in list(range(0, 40)) + [63, 64, 65, 100, 257, 1000]:
            for shape in ("random", "dups", "sorted", "reverse", "equal"):
                if count > 300 and shape != "random" and width not in (4, 16):
                    continue
                cases.append((count, width, rng.random() < 0.5, make_case(rng, count, width, shape)))
    # The real callers: 5 entries, widths 2 and a small struct, no work buffer.
    for _ in range(300):
        cases.append((5, 2, False, make_case(rng, 5, 2, "dups")))
    expected = [run_rom(code, addr, arr, c, w, uw) for c, w, uw, arr in cases]

    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "cases.txt").write_text("".join(f"{c} {w} {int(uw)} {arr.hex() or '-'}\n" for c, w, uw, arr in cases))
        (d / "h.c").write_text(HARNESS)
        flags = subprocess.run(["python3", "-I", "-c", (
            "import sys;sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1]);import sdk_compile_survey as s;"
            "print(' '.join(s.DEFINES+[f for f in s.CFLAGS if f!='-fgnu89-inline']+s.includes(sys.argv[2])))"),
            str(Path(__file__).resolve().parent), str(root)], capture_output=True, text=True).stdout.split()
        subprocess.run(["gcc", "-m32", "-malign-double", *flags, "-c", str(root / "lib/NitroSDK/src/math/math_qsort.c"),
                        "-o", str(d / "q.o")], check=True)
        subprocess.run(["gcc", "-m32", "-O1", str(d / "h.c"), str(d / "q.o"), "-o", str(d / "h")], check=True)
        out = subprocess.run([str(d / "h"), str(d / "cases.txt")], capture_output=True, text=True, check=True).stdout.splitlines()

    bad = 0
    for n, ((c, w, uw, arr), (calls, res), line) in enumerate(zip(cases, expected, out)):
        head, _, tail = line.partition("|")
        got_calls = [tuple(map(int, p.split(","))) for p in head.split()]
        got_res = bytes.fromhex(tail) if tail != "-" else b""
        if got_calls != calls or got_res != res:
            bad += 1
            if bad <= 5:
                print(f"MISMATCH case {n}: count={c} width={w} work={uw}")
                print("  rom calls:", calls[:12], "...", len(calls))
                print("  c   calls:", got_calls[:12], "...", len(got_calls))
                print("  result equal:", got_res == res)
    ncalls = sum(len(e[0]) for e in expected)
    print(f"{len(cases)} cases, {ncalls} comparator calls compared, {bad} mismatches (ROM code vs C)")
    return 1 if bad else 0


HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int (*Cmp)(const void *, const void *);
void MATH_QSort(void *base, unsigned count, unsigned width, Cmp compare, void *work);
static unsigned W; static unsigned char *B; static int n; static char *buf; static size_t cap;
static void put(const char *s){ size_t l=strlen(s); if(n+l+1>cap){cap=(cap+l)*2; buf=realloc(buf,cap);} memcpy(buf+n,s,l+1); n+=(int)l; }
static unsigned key(const unsigned char *e){ unsigned k=0; unsigned m=W<4?W:4; for(unsigned i=0;i<m;i++) k|=(unsigned)e[i]<<(8*i); return k; }
static int cmp(const void *a, const void *b){ char t[64]; snprintf(t,sizeof t,"%d,%d ",(int)(((unsigned char*)a-B)/W),(int)(((unsigned char*)b-B)/W)); put(t);
  unsigned ka=key(a), kb=key(b); return (ka>kb)-(ka<kb); }
int main(int argc,char**argv){ FILE*f=fopen(argv[1],"r"); char *line=malloc(1<<20); unsigned count,use;
  while(fscanf(f,"%u %u %u %s",&count,&W,&use,line)==4){ unsigned len=count*W; B=malloc(len+1); 
    if(line[0]!='-') for(unsigned i=0;i<len;i++){unsigned v; sscanf(line+2*i,"%2x",&v); B[i]=(unsigned char)v;}
    n=0; if(!buf){cap=1<<16;buf=malloc(cap);} buf[0]=0;
    void *work = use ? malloc(1024) : NULL;
    MATH_QSort(B,count,W,cmp,work); free(work);
    fputs(buf,stdout); putchar('|'); if(len==0) putchar('-'); for(unsigned i=0;i<len;i++) printf("%02x",B[i]); putchar('\n'); free(B);} return 0; }
'''

if __name__ == "__main__":
    sys.exit(main())
