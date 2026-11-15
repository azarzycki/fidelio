#!/usr/bin/env python3
"""Post-ARMv6 instruction scan for the ARMv6 (ARM1176JZF-S, iPhone 2G) Mach-O binaries of this repo.

Why not grep the disassembly for movw/movt/udiv/...: disassembled as armv6, llvm-objdump prints those
encodings as "<unknown>", so a grep for their names can never match (a planted movw passes it). This
scan decodes every word of the code sections twice, as the device CPU (armv6k, -mcpu=arm1176jzf-s:
ARMv6KZ + VFPv2) and as ARMv7-A with every extension, and reports the words that only ARMv7 can decode.
Literal-pool words are data, not code: a word that is the target of a pc-relative ldr/vldr (or the
second half of a 64-bit constant loaded that way) is skipped even if it happens to be a valid ARMv7
encoding. Code is ARM mode (the toolchain wrapper passes -marm).

Usage: isa_scan.py [-q] FILE...     (-q: one summary line per file, no per-word detail)
Env:   LLVM_OBJDUMP (default llvm-objdump-18, else llvm-objdump)
Exit:  0 no post-ARMv6 instruction in any file, 1 at least one found, 2 a file could not be scanned.
Each file prints "FILE: N post-ARMv6 ..." first, so scripts can read N from the line.
"""
import os, re, shutil, subprocess, sys

LINE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.*)$")
PCREL = re.compile(r"\[pc(?:, #(-?)(0x[0-9a-f]+|\d+))?\]")   # objdump prints ldr offsets in hex, vldr in decimal
V7 = "+hwdiv-arm,+hwdiv,+neon,+vfp4,+mp,+trustzone,+virtualization,+crc,+crypto"


def objdump():
    for c in (os.environ.get("LLVM_OBJDUMP"), "llvm-objdump-18", "llvm-objdump"):
        if c and shutil.which(c):
            return c
    return None


def disassemble(tool, path, *args):
    p = subprocess.run([tool, *args, "-d", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    words = {}
    for l in p.stdout.decode("utf-8", "replace").splitlines():
        m = LINE.match(l)
        if m:
            words[int(m.group(1), 16)] = (m.group(2), m.group(3).strip())
    return p.returncode, words


def scan(tool, path):
    rc6, v6 = disassemble(tool, path, "--triple=armv6k-apple-darwin", "--mcpu=arm1176jzf-s")
    rc7, v7 = disassemble(tool, path, "--triple=armv7-apple-darwin", "--mattr=" + V7)
    if rc6 != 0 or rc7 != 0 or not v6 or len(v6) != len(v7):
        return None
    pool = set()
    for a, (w, t) in v6.items():
        m = PCREL.search(t)
        if m and t.split()[0].startswith(("ldr", "vldr")):
            off = int(m.group(2), 0) if m.group(2) else 0
            pool.add(a + 8 + (-off if m.group(1) else off))
    undecodable = [a for a, (w, t) in v6.items() if t.startswith("<unknown>")]
    hits, skipped = [], 0
    for a in undecodable:
        t7 = v7.get(a, ("", "<unknown>"))[1]
        if t7.startswith("<unknown>"):
            continue
        if a in pool or a - 4 in pool:
            skipped += 1
            continue
        hits.append((a, v7[a][0], t7))
    return len(v6), len(undecodable), skipped, hits


def main(argv):
    quiet = "-q" in argv
    files = [a for a in argv if a != "-q"]
    if not files:
        print(__doc__.strip().splitlines()[-4]); return 2
    tool = objdump()
    if not tool:
        print("isa_scan: no llvm-objdump found (set LLVM_OBJDUMP)"); return 2
    worst = 0
    for f in files:
        r = scan(tool, f)
        if r is None:
            print(f"{f}: SCAN FAILED (not an ARM Mach-O, or {tool} produced no disassembly)"); worst = 2; continue
        words, undec, skipped, hits = r
        print(f"{f}: {len(hits)} post-ARMv6 instruction(s) in code ({words} words; {undec} not decodable on the ARM1176, "
              f"{skipped} of them literal-pool data valid as ARMv7)")
        if hits:
            worst = max(worst, 1)
            if not quiet:
                for a, w, t in hits[:20]:
                    print(f"    {a:8x}: {w}  {t}")
                if len(hits) > 20:
                    print(f"    ... {len(hits) - 20} more")
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
