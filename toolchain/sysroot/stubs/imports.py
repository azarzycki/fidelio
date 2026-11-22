#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
"""imports.py: keeps manifest.tsv, the list of what the stub libraries of the sysroot must offer, in
step with what the ARMv6 binaries of this repository import.

  imports.py update [--check] MANIFEST [PREFIX=]PATH...
      Reads the linked files (thin armv6 Mach-O executables, dylibs and bundles) and rewrites MANIFEST.
      PATH is a file or a directory; in a directory every linked armv6 Mach-O below it is read. The
      label of a file, which the manifest shows in the "imported by" column, is PREFIX plus its path
      below PATH (plus its name, for a single file).
      From each file: the libraries it names (LC_LOAD_DYLIB: install name, current and compatibility
      version) and, for every undefined symbol, the library its two-level namespace ordinal points at.
      The "imported by" column is rebuilt from the files given, so give all of them:
          imports.py update manifest.tsv BUILD/arm-out smoke/=SMOKE-DIR
      Kept as they are in MANIFEST: the header comment, the path and the re-exports of every library
      row, the link rows, and the note of every symbol row. A symbol row that ends up with neither
      an importer nor a note is dropped, and the summary names it. A library row is never dropped.
      --check writes nothing: it prints the difference and exits 1 if MANIFEST would change.
      A manifest that does not exist yet is created, with a default header.

  imports.py validate MANIFEST
      Reads MANIFEST the strict way every command here does (fields, order, names, references
      between rows) and says how much it holds. build-stubs.sh runs this before it builds.

  imports.py stubs MANIFEST SYSROOT
      Compares the libraries under SYSROOT with MANIFEST: each one a thin armv6 MH_DYLIB with the
      install name and versions of its row, LC_REEXPORT_DYLIB commands for exactly its re-exports
      in the order of the row, no other load command that names a library, and an export list
      equal to its symbol rows; each link row a symlink with that target. build-stubs.sh runs this
      on what it built.

  imports.py archives MANIFEST ARCHIVE...
      Lists the symbols that the members of the static archives (the toolchain's libc++.a,
      libc++abi.a, libc++experimental.a, libios3compat.a) leave undefined and no member defines:
      what a link that pulls those members in can import. Exit 1 if the manifest lacks one of them.
      An archive does not say which library provides a symbol; that has to be shown before a row
      is added (see the header of manifest.tsv).

Exit status: 0 success, 1 a check failed or the manifest would change, 2 unusable input.
Needs python3's standard library only: no SDK, no toolchain. The format of the manifest and the rule
for adding a symbol are in the header of manifest.tsv and in ../README.md.
"""
import argparse
import difflib
import os
import re
import struct
import sys

CPU_ARM, CPU_SUBTYPE_ARM_V6 = 12, 6
MH_OBJECT, MH_EXECUTE, MH_DYLIB, MH_BUNDLE = 1, 2, 6, 8
MH_NOUNDEFS, MH_DYLDLINK, MH_TWOLEVEL, MH_NO_REEXPORTED_DYLIBS = 0x1, 0x4, 0x80, 0x100000
LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB, LC_LOAD_DYLIB, LC_ID_DYLIB, LC_REEXPORT_DYLIB = 0x1, 0x2, 0xb, 0xc, 0xd, 0x8000001f
# every load command that makes an image depend on another library
DYLIB_CMDS = {LC_LOAD_DYLIB: "LC_LOAD_DYLIB", 0x80000018: "LC_LOAD_WEAK_DYLIB", LC_REEXPORT_DYLIB: "LC_REEXPORT_DYLIB",
              0x20: "LC_LAZY_LOAD_DYLIB", 0x80000023: "LC_LOAD_UPWARD_DYLIB"}
CMD_NAMES = {LC_SEGMENT: "LC_SEGMENT", LC_SYMTAB: "LC_SYMTAB", LC_DYSYMTAB: "LC_DYSYMTAB", LC_ID_DYLIB: "LC_ID_DYLIB"}
CMD_NAMES.update(DYLIB_CMDS)
N_STAB, N_PEXT, N_TYPE, N_EXT, N_UNDF, N_SECT = 0xe0, 0x10, 0x0e, 0x01, 0x0, 0xe
N_WEAK = 0x0080            # n_desc: N_WEAK_DEF on a definition, N_REF_TO_WEAK on an undefined symbol
DYNAMIC_LOOKUP_ORDINAL = 0xfe
# undefined symbols of an object file that the linker itself defines
LINKER_MADE = {"___dso_handle", "__mh_execute_header", "__mh_dylib_header", "__mh_bundle_header"}

DEFAULT_HEADER = """\
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
#
# manifest.tsv: what the stub libraries of the sysroot offer to the linker (see ../README.md).
"""

RE_PATH = re.compile(r"[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*\Z")
RE_VERSION = re.compile(r"(\d{1,5})\.(\d{1,3})\.(\d{1,3})\Z")
RE_SYMBOL = re.compile(r"[!#-\[\]-~]+\Z")          # printable ASCII without space, double quote and backslash
RE_LABEL = re.compile(r"[!-~]+\Z")
RE_NOTE = re.compile(r"[ -~]+\Z")


class Bad(Exception):
    """unusable input: exit status 2"""


def ver_str(v):
    return "%d.%d.%d" % (v >> 16, (v >> 8) & 0xff, v & 0xff)


def cstring(data, start, end):
    stop = data.find(b"\0", start, end)
    return data[start:stop if stop >= 0 else end].decode("latin-1")


class MachO:
    """A thin 32-bit little-endian Mach-O: header, the commands that name libraries, the symbol table."""

    def __init__(self, data, what):
        if data[:4] == b"\xca\xfe\xba\xbe":
            raise Bad("%s: a fat file (this script reads thin armv6 files)" % what)
        if len(data) < 28 or data[:4] != b"\xce\xfa\xed\xfe":
            raise Bad("%s: not a 32-bit little-endian Mach-O" % what)
        self.what = what
        self.cputype, sub, self.filetype, ncmds, _, self.flags = struct.unpack_from("<iiIIII", data, 4)
        self.cpusubtype = sub & 0xffffff
        self.cmds, self.dylibs, self.id, self.symbols = [], [], None, []
        off = 28
        try:
            for _ in range(ncmds):
                cmd, size = struct.unpack_from("<II", data, off)
                if size < 8 or off + size > len(data):
                    raise Bad("%s: a load command runs past the end of the file" % what)
                self.cmds.append(cmd)
                if cmd == LC_ID_DYLIB or cmd in DYLIB_CMDS:
                    name_off, _, current, compat = struct.unpack_from("<IIII", data, off + 8)
                    entry = (cmd, cstring(data, off + name_off, off + size), ver_str(current), ver_str(compat))
                    if cmd == LC_ID_DYLIB:
                        self.id = entry
                    else:
                        self.dylibs.append(entry)
                elif cmd == LC_SYMTAB:
                    symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", data, off + 8)
                    for i in range(nsyms):
                        strx, n_type, n_sect, n_desc, n_value = struct.unpack_from("<IBBHI", data, symoff + 12 * i)
                        self.symbols.append((cstring(data, stroff + strx, stroff + strsize), n_type, n_sect, n_desc, n_value))
                off += size
        except struct.error:
            raise Bad("%s: truncated Mach-O" % what)

    def is_armv6(self):
        return self.cputype == CPU_ARM and self.cpusubtype == CPU_SUBTYPE_ARM_V6

    def undefined(self):
        """(name, n_desc) of every undefined symbol (a common symbol has a size in n_value and is not one)"""
        return [(n, d) for n, t, _, d, v in self.symbols if not t & N_STAB and t & N_EXT and t & N_TYPE == N_UNDF and v == 0]

    def defined_external(self):
        return [n for n, t, _, _, v in self.symbols
                if not t & N_STAB and t & N_EXT and (t & N_TYPE != N_UNDF or v != 0)]


# ------------------------------------------------------------------------------- the manifest ----

class Manifest:
    def __init__(self):
        self.header = []    # the comment lines at the top, without their newline
        self.libs = {}      # install name -> [path, current, compat, [re-exported install names]]
        self.links = {}     # path -> target
        self.syms = {}      # (install name, symbol) -> [set of importer labels, note or "-"]

    @classmethod
    def read(cls, path):
        m = cls()
        try:
            text = open(path, "rb").read().decode("ascii")
        except UnicodeDecodeError:
            raise Bad("%s: not plain ASCII" % path)
        if "\r" in text or not text.endswith("\n"):
            raise Bad("%s: needs LF line ends and a newline at the end" % path)
        rows = []
        for no, line in enumerate(text[:-1].split("\n"), 1):
            where = "%s:%d" % (path, no)
            if line.startswith("#"):
                if rows:
                    raise Bad("%s: comment lines belong at the top only" % where)
                m.header.append(line)
                continue
            f = line.split("\t")
            if f[0] == "lib" and len(f) == 6:
                if f[2] in m.libs:
                    raise Bad("%s: second row for %s" % (where, f[2]))
                m.libs[f[2]] = [f[1], f[3], f[4], [] if f[5] == "-" else f[5].split(" ")]
            elif f[0] == "link" and len(f) == 3:
                if f[1] in m.links:
                    raise Bad("%s: second row for %s" % (where, f[1]))
                m.links[f[1]] = f[2]
            elif f[0] == "sym" and len(f) == 5:
                if (f[1], f[2]) in m.syms:
                    raise Bad("%s: second row for %s in %s" % (where, f[2], f[1]))
                m.syms[(f[1], f[2])] = [set() if f[3] == "-" else set(f[3].split(" ")), f[4]]
            else:
                raise Bad("%s: not a lib, link or sym row with the right number of fields" % where)
            rows.append(line)
        m.validate(path)
        if m.text() != text:
            raise Bad("%s: rows are not in canonical form (sorted in byte order, importers sorted, single separators); "
                      "'imports.py update' writes it" % path)
        return m

    def validate(self, what):
        paths = {}
        for name, (path, current, compat, reexports) in self.libs.items():
            if not (name.startswith("/") and RE_PATH.match(name[1:])) or ".." in name.split("/"):
                raise Bad("%s: bad install name %r" % (what, name))
            if path != name[1:]:
                raise Bad("%s: %s: the path in the sysroot must be the install name without its first slash" % (what, name))
            for v in (current, compat):
                g = RE_VERSION.match(v)
                if not g or int(g.group(1)) > 0xffff or int(g.group(2)) > 0xff or int(g.group(3)) > 0xff:
                    raise Bad("%s: %s: bad version %r" % (what, name, v))
            for r in reexports:
                if r not in self.libs or r == name:
                    raise Bad("%s: %s re-exports %r, which has no lib row of its own" % (what, name, r))
            if len(set(reexports)) != len(reexports):
                raise Bad("%s: %s names a re-export twice" % (what, name))
            paths[path] = name
        for path, target in self.links.items():
            if not RE_PATH.match(path) or not RE_PATH.match(target) or ".." in (path + "/" + target).split("/"):
                raise Bad("%s: bad link row %r -> %r" % (what, path, target))
            if path in paths:
                raise Bad("%s: %s is a library and a link" % (what, path))
            if os.path.normpath(os.path.join(os.path.dirname(path), target)) not in paths:
                raise Bad("%s: link %s -> %s does not lead to a library of the manifest" % (what, path, target))
        for (name, sym), (importers, note) in self.syms.items():
            if name not in self.libs:
                raise Bad("%s: symbol %s: no lib row for %s" % (what, sym, name))
            if not RE_SYMBOL.match(sym):
                raise Bad("%s: symbol name %r has a character the manifest does not allow" % (what, sym))
            if not importers and note == "-":
                raise Bad("%s: %s in %s has neither an importer nor a note" % (what, sym, name))
            if not RE_NOTE.match(note) or any(not RE_LABEL.match(i) for i in importers):
                raise Bad("%s: %s in %s: bad importer label or note" % (what, sym, name))

    def text(self):
        # the re-exports stay in the order they were written in: the order of the stub's load commands
        rows = ["lib\t%s\t%s\t%s\t%s\t%s" % (path, name, current, compat, " ".join(reexports) or "-")
                for name, (path, current, compat, reexports) in self.libs.items()]
        rows += ["link\t%s\t%s" % (path, target) for path, target in self.links.items()]
        rows += ["sym\t%s\t%s\t%s\t%s" % (name, sym, " ".join(sorted(importers)) or "-", note)
                 for (name, sym), (importers, note) in self.syms.items()]
        return "".join(line + "\n" for line in self.header + sorted(rows))

    def symbols_of(self, name):
        return sorted(sym for (lib, sym) in self.syms if lib == name)


# --------------------------------------------------------------------------------- update --------

def linked_armv6_candidate(path):
    """True for a file that starts like a linked ARM Mach-O (executable, dylib or bundle)"""
    try:
        with open(path, "rb") as f:
            head = f.read(28)
    except OSError:
        return False
    if len(head) < 28 or head[:4] != b"\xce\xfa\xed\xfe":
        return False
    cputype, _, filetype = struct.unpack_from("<iiI", head, 4)
    return cputype == CPU_ARM and filetype in (MH_EXECUTE, MH_DYLIB, MH_BUNDLE)


def collect(args):
    """[(label, path)] for the [PREFIX=]PATH arguments"""
    found = []
    for arg in args:
        prefix, sep, path = arg.partition("=")
        if not sep:
            prefix, path = "", arg
        if os.path.isdir(path):
            n = len(found)
            for root, dirs, files in os.walk(path):
                dirs.sort()
                for name in sorted(files):
                    full = os.path.join(root, name)
                    if not os.path.islink(full) and os.path.isfile(full) and linked_armv6_candidate(full):
                        found.append((prefix + os.path.relpath(full, path), full))
            if len(found) == n:
                raise Bad("%s: no linked ARM Mach-O file below it" % path)
        elif os.path.isfile(path):
            if not linked_armv6_candidate(path):
                raise Bad("%s: not a linked ARM Mach-O file (executable, dylib or bundle)" % path)
            found.append((prefix + os.path.basename(path), path))
        else:
            raise Bad("%s: no such file or directory" % path)
    labels = [label for label, _ in found]
    for label in labels:
        if not RE_LABEL.match(label):
            raise Bad("label %r: a label must be printable ASCII without spaces (use PREFIX=PATH)" % label)
        if labels.count(label) > 1:
            raise Bad("label %r stands for two files (use PREFIX=PATH)" % label)
    return found


def cmd_update(opts):
    if os.path.exists(opts.manifest):
        man = Manifest.read(opts.manifest)
        old_text = man.text()
    else:
        man = Manifest()
        man.header = DEFAULT_HEADER.rstrip("\n").split("\n")
        old_text = ""
    old_syms = set(man.syms)
    old_libs = set(man.libs)
    for entry in man.syms.values():
        entry[0] = set()
    flat = []
    files = collect(opts.paths)
    for label, path in files:
        with open(path, "rb") as f:
            mo = MachO(f.read(), path)
        if not mo.is_armv6():
            raise Bad("%s: an ARM Mach-O, but not armv6 (cpu subtype %d)" % (path, mo.cpusubtype))
        if not mo.flags & MH_TWOLEVEL:
            raise Bad("%s: not a two-level namespace image: its symbols do not record their library" % path)
        for cmd, name, current, compat in mo.dylibs:
            if cmd != LC_LOAD_DYLIB:
                raise Bad("%s: %s %s: only LC_LOAD_DYLIB is handled" % (path, DYLIB_CMDS[cmd], name))
            lib = man.libs.setdefault(name, [name[1:], current, compat, []])
            if (lib[1], lib[2]) != (current, compat):
                raise Bad("%s records %s %s/%s, the manifest has %s/%s: one sysroot cannot serve both"
                          % (path, name, current, compat, lib[1], lib[2]))
        for sym, n_desc in mo.undefined():
            ordinal = (n_desc >> 8) & 0xff
            if ordinal == DYNAMIC_LOOKUP_ORDINAL:
                flat.append((sym, label))
                continue
            if not 1 <= ordinal <= len(mo.dylibs):
                raise Bad("%s: %s has library ordinal %d, which is not one of its %d libraries"
                          % (path, sym, ordinal, len(mo.dylibs)))
            if n_desc & N_WEAK:
                raise Bad("%s: %s is bound to a weak definition (N_REF_TO_WEAK). The manifest has no way to say that a "
                          "library defines a symbol weakly, and a stub that defines it normally gives another binary: "
                          "extend manifest.tsv, build-stubs.sh and this script first" % (path, sym))
            man.syms.setdefault((mo.dylibs[ordinal - 1][1], sym), [set(), "-"])[0].add(label)
    dropped = sorted(key for key, (importers, note) in man.syms.items() if not importers and note == "-")
    for key in dropped:
        del man.syms[key]
    man.validate("the updated manifest")
    new_text = man.text()

    print("read %d linked files: %s" % (len(files), " ".join(label for label, _ in files)))
    for name in sorted(man.libs):
        path, current, compat, reexports = man.libs[name]
        mine = [key for key in man.syms if key[0] == name]
        print("  %-68s %-9s %-8s %3d symbols (%d imported, %d by note only)%s%s" % (
            name, current, compat, len(mine), sum(1 for k in mine if man.syms[k][0]),
            sum(1 for k in mine if not man.syms[k][0]),
            "  re-exports " + " ".join(os.path.basename(r) for r in reexports) if reexports else "",
            "  NEW LIBRARY" if name not in old_libs else ""))
    print("  %d libraries, %d links, %d symbols" % (len(man.libs), len(man.links), len(man.syms)))
    for sym, label in sorted(flat):
        print("  not in the manifest (flat lookup, no library recorded): %s in %s" % (sym, label))
    for name, sym in sorted(set(man.syms) - old_syms):
        print("  new symbol: %s in %s" % (sym, name))
    for name, sym in dropped:
        print("  dropped (no importer left, no note): %s in %s" % (sym, name))

    if new_text == old_text:
        print("%s: up to date" % opts.manifest)
        return 0
    if opts.check:
        sys.stdout.writelines(difflib.unified_diff(old_text.splitlines(True), new_text.splitlines(True),
                                                   opts.manifest, opts.manifest + " (from the files)", n=0))
        print("%s: NOT up to date" % opts.manifest)
        return 1
    tmp = "%s.tmp.%d" % (opts.manifest, os.getpid())
    with open(tmp, "wb") as f:
        f.write(new_text.encode("ascii"))
    os.replace(tmp, opts.manifest)
    print("%s: written" % opts.manifest)
    return 0


# ---------------------------------------------------------------------------- validate, stubs ----

def cmd_validate(opts):
    man = Manifest.read(opts.manifest)
    print("%s: %d libraries, %d links, %d symbols" % (opts.manifest, len(man.libs), len(man.links), len(man.syms)))
    return 0


def check_stub(man, name, sysroot):
    """problems of one stub library, as a list of strings"""
    path, current, compat, reexports = man.libs[name]
    full = os.path.join(sysroot, path)
    if os.path.islink(full) or not os.path.isfile(full):
        return ["not a regular file"]
    try:
        with open(full, "rb") as f:
            mo = MachO(f.read(), full)
    except Bad as e:
        return [str(e)]
    bad = []
    if not mo.is_armv6() or mo.filetype != MH_DYLIB:
        bad.append("not an armv6 MH_DYLIB (cpu %d/%d, file type %d)" % (mo.cputype, mo.cpusubtype, mo.filetype))
    if mo.id is None or mo.id[1:] != (name, current, compat):
        bad.append("LC_ID_DYLIB is %s, expected %s %s %s" % (" ".join(mo.id[1:]) if mo.id else "missing", name, current, compat))
    want = [(LC_REEXPORT_DYLIB, r, man.libs[r][1], man.libs[r][2]) for r in reexports]
    if mo.dylibs != want:
        bad.append("names these libraries: %s; expected only LC_REEXPORT_DYLIB, in this order, for: %s" % (
            ", ".join("%s %s %s/%s" % (DYLIB_CMDS[c], n, a, b) for c, n, a, b in mo.dylibs) or "none",
            " ".join(reexports) or "none"))
    stray = sorted(set(c for c in mo.cmds if c not in (LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB, LC_ID_DYLIB, LC_REEXPORT_DYLIB)))
    if stray:
        bad.append("unexpected load commands: %s" % " ".join(CMD_NAMES.get(c, "%#x" % c) for c in stray))
    flags = MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL | (0 if reexports else MH_NO_REEXPORTED_DYLIBS)
    if mo.flags != flags:
        bad.append("header flags %#x, expected %#x" % (mo.flags, flags))
    odd = [n for n, t, _, d, _ in mo.symbols if t & N_STAB or t & N_PEXT or not t & N_EXT or t & N_TYPE != N_SECT or d & N_WEAK]
    if odd:
        bad.append("%d symbols are not plain exported definitions, e.g. %s" % (len(odd), odd[0]))
    got, expect = sorted(n for n, _, _, _, _ in mo.symbols), man.symbols_of(name)
    if got != expect:
        missing, extra = sorted(set(expect) - set(got)), sorted(set(got) - set(expect))
        bad.append("exports differ from the manifest: %d missing%s, %d not in the manifest%s%s" % (
            len(missing), " (e.g. %s)" % missing[0] if missing else "", len(extra), " (e.g. %s)" % extra[0] if extra else "",
            "" if missing or extra else ", a name is there twice"))
    if len(set(v for _, _, _, _, v in mo.symbols)) != len(mo.symbols):
        bad.append("two symbols share an address")
    return bad


def cmd_stubs(opts):
    man = Manifest.read(opts.manifest)
    if not os.path.isdir(opts.sysroot):
        raise Bad("%s: not a directory" % opts.sysroot)
    failures = 0
    for name in sorted(man.libs, key=lambda n: man.libs[n][0]):
        path, current, compat, reexports = man.libs[name]
        bad = check_stub(man, name, opts.sysroot)
        failures += len(bad)
        for b in bad:
            print("FAIL %s: %s" % (path, b))
        if not bad:
            print("ok   %-66s %-9s %-8s %3d symbols%s" % (path, current, compat, len(man.symbols_of(name)),
                  "  re-exports " + " ".join(os.path.basename(r) for r in reexports) if reexports else ""))
    for path in sorted(man.links):
        full = os.path.join(opts.sysroot, path)
        if os.path.islink(full) and os.readlink(full) == man.links[path]:
            print("ok   %s -> %s" % (path, man.links[path]))
        else:
            failures += 1
            print("FAIL %s: not a symlink to %s" % (path, man.links[path]))
    if failures:
        print("=> %d FAILURE(S)" % failures)
        return 1
    print("=> STUBS OK: %d libraries, %d symbols, %d links match %s" % (len(man.libs), len(man.syms), len(man.links), opts.manifest))
    return 0


# ------------------------------------------------------------------------------- archives --------

def ar_members(path):
    """(member name, bytes) for a BSD or GNU ar archive; the symbol index members are left out"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"!<arch>\n":
        raise Bad("%s: not an ar archive" % path)
    off, longnames = 8, b""
    while off + 60 <= len(data):
        head = data[off:off + 60]
        off += 60
        name = head[:16].decode("latin-1").rstrip(" ")
        try:
            size = int(head[48:58])
        except ValueError:
            raise Bad("%s: damaged member header" % path)
        body = data[off:off + size]
        off += size + (size & 1)
        if name.startswith("#1/"):                       # BSD: the name is the first bytes of the body
            n = int(name[3:])
            name, body = body[:n].rstrip(b"\0").decode("latin-1"), body[n:]
        elif name == "//":                               # GNU: table of long names
            longnames = body
            continue
        elif name.startswith("/") and name[1:].isdigit():
            start = int(name[1:])
            name = longnames[start:longnames.find(b"/\n", start)].decode("latin-1")
        elif name.endswith("/") and name != "/":
            name = name[:-1]
        if name in ("/", "/SYM64/") or name.startswith("__.SYMDEF"):
            continue
        yield name, body


def cmd_archives(opts):
    man = Manifest.read(opts.manifest)
    in_manifest = {}
    for name, sym in man.syms:
        in_manifest.setdefault(sym, []).append(name)
    defined, undefined = set(), {}        # undefined: symbol -> {archive: [members]}
    for path in opts.archives:
        archive = os.path.basename(path)
        for member, body in ar_members(path):
            mo = MachO(body, "%s(%s)" % (path, member))
            if mo.filetype != MH_OBJECT or not mo.is_armv6():
                raise Bad("%s(%s): not an armv6 object file" % (path, member))
            defined.update(mo.defined_external())
            for sym, _ in mo.undefined():
                undefined.setdefault(sym, {}).setdefault(archive, []).append(member)
    missing = 0
    for sym in sorted(undefined):
        if sym in defined or sym in LINKER_MADE:
            continue
        where = " ".join("%s(%s)" % (a, ",".join(sorted(set(ms)))) for a, ms in sorted(undefined[sym].items()))
        libs = in_manifest.get(sym)
        missing += not libs
        print("%s\t%s\t%s" % (sym, " ".join(sorted(libs)) if libs else "NOT IN THE MANIFEST", where))
    total = sum(1 for s in undefined if s not in defined and s not in LINKER_MADE)
    print("=> %d symbols the archives can import; %d of them not in the manifest" % (total, missing))
    return 1 if missing else 0


def main():
    ap = argparse.ArgumentParser(description="manifest.tsv of the stub libraries against linked Mach-O files",
                                 epilog="The header of this script and ../README.md say more.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("update", help="rewrite the manifest from the imports of linked files")
    p.add_argument("--check", action="store_true", help="write nothing; exit 1 if the manifest would change")
    p.add_argument("manifest")
    p.add_argument("paths", nargs="+", metavar="[PREFIX=]PATH")
    p.set_defaults(func=cmd_update)
    p = sub.add_parser("validate", help="read the manifest strictly")
    p.add_argument("manifest")
    p.set_defaults(func=cmd_validate)
    p = sub.add_parser("stubs", help="compare the libraries of a sysroot with the manifest")
    p.add_argument("manifest")
    p.add_argument("sysroot")
    p.set_defaults(func=cmd_stubs)
    p = sub.add_parser("archives", help="what static archives can import, against the manifest")
    p.add_argument("manifest")
    p.add_argument("archives", nargs="+", metavar="ARCHIVE")
    p.set_defaults(func=cmd_archives)
    opts = ap.parse_args()
    try:
        return opts.func(opts)
    except Bad as e:
        print("imports.py: ERROR: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("imports.py: ERROR: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
