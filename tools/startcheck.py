#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
"""Static check of what the start-up object put into a LINKED ARMv6 Mach-O of this repo (thin, 32-bit,
little-endian): the start-up code, and the source-availability notice that comes with it.

Every executable, dylib and bundle begins with one start-up object: crt1.o, dylib1.o or bundle1.o. The
toolchain builds them from Apple's open-source Csu release (toolchain/startfiles/build-startfiles.sh).
This script finds their routines in the linked file by their instruction bytes and checks that every
pc-relative literal the linker resolved points where the Csu source says it must:

  start (executables)        L4 -> the non-lazy pointer bound to _main, L5 -> the one bound to _exit
  dyld_stub_binding_helper   literal 1 -> dyld__mach_header, a pointer holding this image's mach_header
                             literal 2 -> the first word of __DATA,__dyld (0x2fe01000, dyld's lazy binder)
  __dyld_func_lookup         literal   -> the second word of __DATA,__dyld (0x2fe01008, dyld's func lookup)
  __DATA,__dyld              executables: 7 words, words 2..6 = mach_header, &NXArgc, &NXArgv, &environ,
                             &__progname; dylibs and bundles: the 2 dyld words only

It also checks the source-availability notice that the start-up objects carry since toolchain revision 3
(toolchain/startfiles/notice.s): the file must have a section __TEXT,__csu_notice whose contents are,
byte for byte, one NUL-terminated string:

  executables                the text of crt1.o ("This program contains start-up code ...")
  dylibs and bundles         the text of dylib1.o and bundle1.o ("This library contains glue code ...")

A file without the section, or with another text in it, fails. The two texts are NOTICE_PROGRAM and
NOTICE_LIBRARY below; they must stay the same as the strings in notice.s.

Why it exists: start-up objects merged with "ld -r" (the Csu Makefile's own recipe) link with this
toolchain's ld64, and the result passes armv6-ios3-check and tools/isa_scan.py, but the literal of
__dyld_func_lookup is wrong. Only a check of the linked literals sees that. And a binary linked with
start-up objects that lack the notice looks like any other: only a check of the section sees that.

Needs no SDK, no toolchain and no other file: python3's standard library only.

Usage: startcheck.py FILE...
Exit:  0 every check passed in every file, 1 at least one check failed, 2 a file could not be checked.
Each file prints "FILE: MH_... ..." first, one "ok"/"FAIL" line per check, and "=> START-UP CODE OK" or
"=> N FAILURE(S)" last, so scripts can read the verdict from the last line. The line of the notice
check starts with "ok   notice:" or "FAIL notice:"; the ok line names the text ("the text for a
program" or "the text for a library") and ends with the text as found, in double quotes.
"""
import struct
import sys

NUL = bytes(1)
# The notice inside the start-up objects: toolchain/startfiles/notice.s defines these two strings.
NOTICE_SECT = "__TEXT,__csu_notice"
NOTICE_PROGRAM = (
    "This program contains start-up code built from Apple's open-source Csu release, tag Csu-79: "
    "start.s and crt.c under the Apple Public Source License version 1.1, "
    "dyld_glue.s under the Apple Public Source License version 2.0. "
    "The source code is available under the terms of those licenses at "
    "https://github.com/apple-oss-distributions/Csu (commit fd4097d7a8b07792f55d74271d09adedb4748b94).")
NOTICE_LIBRARY = (
    "This library contains glue code built from Apple's open-source Csu release, tag Csu-79: "
    "dyld_glue.s under the Apple Public Source License version 2.0. "
    "The source code is available under the terms of that license at "
    "https://github.com/apple-oss-distributions/Csu (commit fd4097d7a8b07792f55d74271d09adedb4748b94).")
NOTICES = {"program": NOTICE_PROGRAM.encode("ascii") + NUL, "library": NOTICE_LIBRARY.encode("ascii") + NUL}
# The ARM instructions of the three routines, up to their literal words (Csu-79: start.s, dyld_glue.s).
START = bytes.fromhex("00009de504108de2014080e2042181e007d0cde30230a0e1044093e4000054e3fcffff1a"
                      "18c09fe50cc08fe000c09ce53cff2fe10cc09fe50cc08fe000c09ce51cff2fe1")
HELPER = bytes.fromhex("04c02de50cc09fe50cc09fe704c02de504c09fe50cf09fe7")
LOOKUP = bytes.fromhex("00c09fe50cf09fe7")
KINDS = {2: "MH_EXECUTE", 6: "MH_DYLIB", 8: "MH_BUNDLE"}


class CannotCheck(Exception):
    pass


def parse(path):
    d = open(path, "rb").read()
    if len(d) < 28:
        raise CannotCheck("too short for a Mach-O header")
    magic, cpu, sub, ftype, ncmds, szcmds, flags = struct.unpack("<IiiIIII", d[:28])
    if magic != 0xFEEDFACE:
        raise CannotCheck("not a thin 32-bit little-endian Mach-O (magic %#x)" % magic)
    if cpu != 12:
        raise CannotCheck("not an ARM Mach-O (cputype %d)" % cpu)
    if ftype not in KINDS:
        raise CannotCheck("file type %d is not an executable, a dylib or a bundle" % ftype)
    m = dict(d=d, ftype=ftype, sects={}, segs=[], syms=[], indirect=[], entry=None)
    off = 28
    for _ in range(ncmds):
        cmd, size = struct.unpack("<II", d[off:off + 8])
        body = d[off:off + size]
        if cmd == 0x1:                                   # LC_SEGMENT
            segname = body[8:24].rstrip(NUL).decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack("<IIII", body[24:40])
            m["segs"].append((segname, vmaddr, vmsize, fileoff, filesize))
            nsects = struct.unpack("<I", body[48:52])[0]
            for i in range(nsects):
                s = body[56 + 68 * i:56 + 68 * (i + 1)]
                name = s[16:32].rstrip(NUL).decode() + "," + s[:16].rstrip(NUL).decode()
                addr, sz, foff, align, reloff, nreloc, sflags, r1, r2 = struct.unpack("<IIIIIIIII", s[32:68])
                m["sects"][name] = dict(addr=addr, size=sz, off=foff, flags=sflags, r1=r1)
        elif cmd == 0x2:                                 # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack("<IIII", body[8:24])
            strs = d[stroff:stroff + strsize]
            for i in range(nsyms):
                strx, ntype, nsect, ndesc, nvalue = struct.unpack("<IBBHI", d[symoff + 12 * i: symoff + 12 * i + 12])
                m["syms"].append((strs[strx:].split(NUL)[0].decode("utf-8", "replace"), ntype, nsect, nvalue))
        elif cmd == 0xb:                                 # LC_DYSYMTAB: the indirect symbol table
            f = struct.unpack("<18I", body[8:80])
            m["indirect"] = [struct.unpack("<I", d[f[12] + 4 * i: f[12] + 4 * i + 4])[0] for i in range(f[13])]
        elif cmd == 0x5:                                 # LC_UNIXTHREAD: pc is the entry point
            flavor, count = struct.unpack("<II", body[8:16])
            regs = struct.unpack("<%dI" % count, body[16:16 + 4 * count])
            m["entry"] = regs[15]
        off += size
    if "__TEXT,__text" not in m["sects"]:
        raise CannotCheck("no __TEXT,__text section")
    if not any(n == "__TEXT" for n, _, _, _, _ in m["segs"]):
        raise CannotCheck("no __TEXT segment")
    if ftype == 2 and m["entry"] is None:
        raise CannotCheck("executable without LC_UNIXTHREAD")
    return m


def rd32(m, addr):
    """The word stored at a virtual address, or None (outside every section, or in a zero-fill one)."""
    for s in m["sects"].values():
        if s["addr"] <= addr < s["addr"] + s["size"] and (s["flags"] & 0xff) not in (1, 12):
            o = s["off"] + addr - s["addr"]
            return struct.unpack("<I", m["d"][o:o + 4])[0]
    return None


def sect_of(m, addr):
    if addr is None:
        return "?"
    for name, s in m["sects"].items():
        if s["addr"] <= addr < s["addr"] + s["size"]:
            return name
    return "?"


def nlptr_symbol(m, addr):
    """Name of the symbol a non-lazy pointer is bound to, "(local/abs)", or None if addr is not one."""
    for s in m["sects"].values():
        if (s["flags"] & 0xff) == 6 and s["addr"] <= addr < s["addr"] + s["size"]:
            idx = m["indirect"][s["r1"] + (addr - s["addr"]) // 4]
            if idx & 0xC0000000:
                return "(local/abs)"
            return m["syms"][idx][0]
    return None


def symaddr(m, name):
    for n, ntype, nsect, nvalue in m["syms"]:
        if n == name and (ntype & 0xe) == 0xe:
            return nvalue
    return None


def hx(v):
    return "%#x" % v if v is not None else "nothing"


def check_notice(m, res):
    """__TEXT,__csu_notice must hold exactly the notice for this file type (see the module docstring)."""
    want = "program" if m["ftype"] == 2 else "library"
    s = m["sects"].get(NOTICE_SECT)
    if s is None:
        res(False, "notice: no %s section: the file does not carry the source-availability notice of the start-up "
                   "objects (toolchain revision 3 or later puts it into every binary)" % NOTICE_SECT)
        return
    if (s["flags"] & 0xff) in (1, 12):
        res(False, "notice: %s is a zero-fill section, it holds no text" % NOTICE_SECT)
        return
    got = m["d"][s["off"]:s["off"] + s["size"]]
    if got == NOTICES[want]:
        res(True, 'notice: %s at %#x, %d bytes, the text for a %s: "%s"'
            % (NOTICE_SECT, s["addr"], s["size"], want, got[:-1].decode("ascii")))
        return
    other = [k for k, v in NOTICES.items() if v == got]
    if other:
        res(False, "notice: %s holds the text for a %s, but the file is %s and needs the text for a %s"
            % (NOTICE_SECT, other[0], KINDS[m["ftype"]], want))
    else:
        exp = NOTICES[want]
        n = next((i for i, (x, y) in enumerate(zip(got, exp)) if x != y), min(len(got), len(exp)))
        res(False, "notice: %s (%d bytes) does not hold the text for a %s (%d bytes): the first difference is at byte %d, "
                   "where the section has %r" % (NOTICE_SECT, s["size"], want, len(exp), n, got[max(0, n - 12):n + 12]))


def check(path):
    m = parse(path)
    kind = KINDS[m["ftype"]]
    text = m["sects"]["__TEXT,__text"]
    code = m["d"][text["off"]:text["off"] + text["size"]]
    dyld = m["sects"].get("__DATA,__dyld")
    mh = next(v for n, v, _, _, _ in m["segs"] if n == "__TEXT")
    bad = 0
    print("%s: %s  __TEXT at %#x  __DATA,__dyld %s"
          % (path, kind, mh, "at %#x size %d" % (dyld["addr"], dyld["size"]) if dyld else "ABSENT"))

    def res(ok, msg):
        nonlocal bad
        print("  %-4s %s" % ("ok" if ok else "FAIL", msg))
        if not ok:
            bad += 1

    if dyld is None:
        res(False, "no __DATA,__dyld section")
        check_notice(m, res)
        print("  => %d FAILURE(S)" % bad)
        return bad
    w0, w1 = rd32(m, dyld["addr"]), rd32(m, dyld["addr"] + 4)
    res(w0 == 0x2fe01000 and w1 == 0x2fe01008,
        "__dyld[0..1] = %s %s (expect 0x2fe01000 0x2fe01008)" % (hx(w0), hx(w1)))

    # ---- start (executables only)
    if m["ftype"] == 2:
        s = m["entry"]
        so = s - text["addr"]
        found = 0 <= so and code[so:so + len(START)] == START
        res(found, "LC_UNIXTHREAD pc %#x: the 17 instructions of Csu `start` (ARM, non-static branch)" % s)
        if found and so + 0x4c <= len(code):
            l4 = struct.unpack("<I", code[so + 0x44:so + 0x48])[0]
            l5 = struct.unpack("<I", code[so + 0x48:so + 0x4c])[0]
            p_main = (s + 0x28 + 8 + l4) & 0xffffffff
            p_exit = (s + 0x38 + 8 + l5) & 0xffffffff
            bound = nlptr_symbol(m, p_main)
            if bound == "(local/abs)":
                # main is defined in the executable itself: the pointer is INDIRECT_SYMBOL_LOCAL and holds main's address
                held, a_main = rd32(m, p_main), symaddr(m, "_main")
                if a_main is None:
                    res(sect_of(m, held) == "__TEXT,__text",
                        "start L4 -> %#x in %s, local pointer holding %s in %s (_main: symbol stripped, section checked only)"
                        % (p_main, sect_of(m, p_main), hx(held), sect_of(m, held)))
                else:
                    res(held == a_main, "start L4 -> %#x in %s, local pointer holding %s (expect address of _main %#x)"
                        % (p_main, sect_of(m, p_main), hx(held), a_main))
            else:
                res(bound == "_main", "start L4 -> %#x in %s, indirect symbol %s (expect _main)" % (p_main, sect_of(m, p_main), bound))
            res(nlptr_symbol(m, p_exit) == "_exit",
                "start L5 -> %#x in %s, indirect symbol %s (expect _exit)" % (p_exit, sect_of(m, p_exit), nlptr_symbol(m, p_exit)))
        res(dyld["size"] == 28, "__dyld is %d bytes (expect 28: 2 dyld pointers + 5 program-variable pointers)" % dyld["size"])
        if dyld["size"] == 28:
            words = [rd32(m, dyld["addr"] + 4 * i) for i in range(2, 7)]
            res(words[0] == mh, "__dyld[2] = %s (expect mach_header %#x)" % (hx(words[0]), mh))
            names = ["_NXArgc", "_NXArgv", "_environ", "___progname"]
            for w, n in zip(words[1:], names):
                a = symaddr(m, n)
                if a is None:
                    res(sect_of(m, w).startswith("__DATA,"),
                        "__dyld -> %s in %s (%s: symbol stripped, section checked only)" % (hx(w), sect_of(m, w), n))
                else:
                    res(w == a, "__dyld -> %s (expect address of %s, %#x in %s)" % (hx(w), n, a, sect_of(m, a)))
            # the four variables must be four consecutive words (dyld__mach_header follows them)
            res(None not in words and words[2] == words[1] + 4 and words[3] == words[1] + 8 and words[4] == words[1] + 12,
                "NXArgc, NXArgv, environ, __progname are consecutive words at %s" % hx(words[1]))
    else:
        res(dyld["size"] == 8, "__dyld is %d bytes (expect 8: 2 dyld pointers)" % dyld["size"])

    # ---- dyld_stub_binding_helper
    hs = [i for i in range(0, len(code) - len(HELPER) - 8 + 1, 4) if code[i:i + len(HELPER)] == HELPER]
    res(len(hs) == 1, "dyld_stub_binding_helper instruction sequence found %d time(s) in __text" % len(hs))
    for h in hs:
        a = text["addr"] + h
        l1, l2 = struct.unpack("<II", code[h + 0x18:h + 0x20])
        p_mh = (a + 0x08 + 8 + l1) & 0xffffffff
        p_lb = (a + 0x14 + 8 + l2) & 0xffffffff
        res(rd32(m, p_mh) == mh, "helper@%#x literal 1 -> %#x in %s holding %s (expect mach_header %#x)"
            % (a, p_mh, sect_of(m, p_mh), hx(rd32(m, p_mh)), mh))
        res(p_lb == dyld["addr"], "helper@%#x literal 2 -> %#x (expect __dyld+0 = %#x)" % (a, p_lb, dyld["addr"]))
        if m["ftype"] == 2:
            res(a == m["entry"] + 0x4c, "helper directly follows start (start+0x4c)")

    # ---- __dyld_func_lookup: the two instructions right after the helper's literals
    for h in hs:
        f = h + 0x20
        a = text["addr"] + f
        if code[f:f + 8] != LOOKUP or f + 12 > len(code):
            # nothing in the image references it and the link used -dead_strip: the routine is simply not there
            print("  note __dyld_func_lookup is not present after the helper at %#x (dead-stripped); nothing to check" % a)
            continue
        res(True, "__dyld_func_lookup instruction pair at %#x (directly after the helper)" % a)
        lit = struct.unpack("<I", code[f + 8:f + 12])[0]
        p = (a + 4 + 8 + lit) & 0xffffffff
        res(p == dyld["addr"] + 4, "__dyld_func_lookup literal -> %#x (expect __dyld+4 = %#x)" % (p, dyld["addr"] + 4))

    # ---- the source-availability notice of the start-up object
    check_notice(m, res)
    print("  => %s" % ("START-UP CODE OK" if not bad else "%d FAILURE(S)" % bad))
    return bad


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    total, unreadable = 0, 0
    for p in argv:
        try:
            total += check(p)
        except (CannotCheck, OSError, struct.error, IndexError, UnicodeDecodeError) as e:
            print("%s: CANNOT CHECK: %s" % (p, e))
            print("  => NOT CHECKED")
            unreadable += 1
    return 2 if unreadable else (1 if total else 0)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
