#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# build-startfiles.sh OUTDIR
#
# Builds the three start-up objects of an ARMv6 iPhone OS 3 link from Apple's open-source Csu release
# and writes them to OUTDIR:
#   crt1.o     first object of every executable: `start` (the LC_UNIXTHREAD entry point, which calls
#              main and then exit), dyld_stub_binding_helper, __dyld_func_lookup, the __DATA,__dyld
#              section, and the variables NXArgc, NXArgv, environ and __progname
#   dylib1.o   first object of every dylib:  dyld_stub_binding_helper and __dyld_func_lookup
#   bundle1.o  first object of every bundle: the same two routines
# In the toolchain they are armv6-ios3/lib/startfiles/*.o. armv6-ios3-clang puts that directory in
# front of the SDK's usr/lib on every link, so the SDK's own copies of these objects are not linked.
#
# Each object also carries a source-availability notice (since toolchain revision 3): the string of
# notice.s, the file next to this script, in a section of its own, __TEXT,__csu_notice. The linker
# copies it into every executable, dylib and bundle, also with -dead_strip, and "strip -x" leaves
# it there. crt1.o has the text for a program (start.s, crt.c, dyld_glue.s); dylib1.o and bundle1.o
# have the text for a library (dyld_glue.s). notice.s says why, and how the section is kept.
#
# Source: fetched here, by commit, and not kept in the project repository.
#   https://github.com/apple-oss-distributions/Csu   tag Csu-79 = commit fd4097d7a8b07792f55d74271d09adedb4748b94
#   Files used, and the licence each one states in its header:
#     start.s      Apple Public Source License, version 1.1
#     crt.c        Apple Public Source License, version 1.1
#     dyld_glue.s  Apple Public Source License, version 2.0
#   They are used as published. notice.s is this project's own file (MIT) and is read from the
#   directory of this script.
#   The sha256 of the three sources and of the three outputs are pinned below. The script fails, and
#   writes nothing to OUTDIR, if any of them differs.
#
# Needs: git (for the fetch), sha256sum, and the toolchain's compiler: the host clang 18 that
#   armv6-ios3-clang drives. It is called directly, with the wrapper's target flags and no sysroot,
#   because the wrapper refuses to run without IOS3_SDK and this build needs no SDK: the only header
#   opened is clang's own <stddef.h>. (That holds for Csu-79; Csu-85 includes <Availability.h>.)
#   No linker runs.
#
# Environment:
#   IOS3_HOST_CLANG  the host clang (default: clang-18 if present, else clang), as in armv6-ios3-clang
#   CSU_SRC_DIR      a directory that already holds start.s, crt.c and dyld_glue.s. It replaces the
#                    fetch; the sha256 pins are checked all the same.
#
# The recipe. The release Makefile (with RC_PURPLE set, which selects the iPhone build) has
#     crt1.o    <- crt1.v2.o:    $(CC) -r -Os -miphoneos-version-min=2.0 -nostdlib -keep_private_externs start.s crt.c dyld_glue.s -DCRT
#     dylib1.o  <- dylib1.v2.o:  $(CC) -r -Os -miphoneos-version-min=2.0 -nostdlib -keep_private_externs dyld_glue.s -DCFM_GLUE
#     bundle1.o <- bundle1.v1.o: $(CC) -r -Os -nostdlib -keep_private_externs dyld_glue.s
# This script keeps the sources, the order, -Os and the -D flags, and changes four things:
#   1. No "-r": nothing is merged by a linker (see below). crt1.o is ONE assembly unit instead: start.s,
#      then the compiler's assembly output for crt.c, then dyld_glue.s, in the Makefile's order.
#      dylib1.o and bundle1.o have one Csu source, dyld_glue.s, so there was nothing to merge in the
#      first place.
#   2. crt.c is compiled with -fno-zero-initialized-in-bss. Its four zero-initialised globals then stay
#      in __DATA,__data, where the SDK's crt1.o has them. Without the flag clang 18 moves them to the
#      zero-fill section __DATA,__common.
#   3. The deployment target is the toolchain's 3.0, not 2.0. None of the three files tests it.
#   4. notice.s is the last part of every unit, after the Csu sources: crt1.o is start.s, crt.c,
#      dyld_glue.s, notice.s; dylib1.o and bundle1.o are dyld_glue.s, notice.s. -DCRT, which the
#      Makefile passes for crt1.o only, also selects the text of the notice. In the object the notice
#      is one more section, the last one, and one more local symbol (__csu_source_notice). The
#      sections that come from the Csu sources keep their order, addresses, sizes, contents and
#      relocations, and their symbols are unchanged (compared with the objects of revision 2, which
#      were built without notice.s).
#
# WHY THE MAKEFILE'S "ld -r" STEP MUST NOT BE USED with this toolchain's linker (cctools-port,
# ld64-956.6): the objects it writes are wrong, and the existing checks do not notice.
#   * The ARM code reaches its data through pc-relative literals of the form
#     "label - (base + 8)", which the assembler stores as a SECTDIFF relocation and a PAIR entry that
#     holds the address of `base`.
#   * "ld -r" re-encodes each of them with the + 8 folded into the PAIR address. For the literal of
#     __dyld_func_lookup, the last routine, that address is then the first byte past the end of
#     __TEXT,__text, which belongs to the next section. In the final link the literal comes out as
#     the constant 4 instead of the distance to the second word of __DATA,__dyld.
#   * Such an object links, and the binary passes armv6-ios3-check and tools/isa_scan.py. A call
#     through __dyld_func_lookup would then take its jump target from the wrong word (seen in the
#     linked bytes; not tried on a device).
#     tools/startcheck.py reports it: "__dyld_func_lookup literal -> ... (expect __dyld+4 = ...)".
#   * The assembler's own output keeps each PAIR address on the base label. Built without notice.s
#     (toolchain revision 2), these objects gave binaries that were byte-identical to binaries linked
#     with the SDK's copies. With the notice a binary has one more section, so it is no longer the
#     same bytes; tools/startcheck.py checks the start-up code and the notice inside each binary.
set -euo pipefail

CSU_URL="https://github.com/apple-oss-distributions/Csu"
CSU_TAG="Csu-79"
CSU_COMMIT="fd4097d7a8b07792f55d74271d09adedb4748b94"
SRC_SHA256="\
b37caea85b6a60c1dde222bce257dccd1ab077a6674cf4cbe225bd2c4d714cf2  start.s
98a4c0765ed0e1785a914abdd84892243d0b6f4b1295fdfb439b9349269897c6  crt.c
8df05b2296b1f23ef754758f8c42d9972d4e11db636aaa94828ff0f4580b5070  dyld_glue.s"
# The same three values are pinned in tools/_common.sh (STARTFILES_SHA256), which tools/build-arm.sh
# checks the installed toolchain against. dylib1.o and bundle1.o are still the same bytes: -DCFM_GLUE
# only selects PowerPC code, and both get the same text of the notice.
# (Revision 2, without the notice: crt1.o c8cf212f..., 1052 bytes; dylib1.o = bundle1.o df1c33d1..., 648 bytes.)
OUT_SHA256="\
29699d8ee4516b27a81ea29691f3e9592a50774976e3dfb746f046c541cbff05  crt1.o
e07e529bd57b927624b4ecfeadd053ea6974eea053238c40b54fb123ac6570fe  dylib1.o
e07e529bd57b927624b4ecfeadd053ea6974eea053238c40b54fb123ac6570fe  bundle1.o"

die() { echo "build-startfiles: ERROR: $*" >&2; exit 1; }

[ $# = 1 ] && [ -n "$1" ] || { echo "usage: build-startfiles.sh OUTDIR" >&2; exit 2; }
OUT="$1"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
NOTICE="$HERE/notice.s"
[ -f "$NOTICE" ] || die "$NOTICE not found (the notice that goes into every start-up object; it belongs next to this script)"

HOSTCC="${IOS3_HOST_CLANG:-}"
if [ -z "$HOSTCC" ]; then
  if command -v clang-18 >/dev/null; then HOSTCC=clang-18; else HOSTCC=clang; fi
fi
command -v "$HOSTCC" >/dev/null || die "host compiler '$HOSTCC' not found (apt install clang-18, or set IOS3_HOST_CLANG)"
command -v sha256sum >/dev/null || die "sha256sum not found"
# armv6-ios3-clang's target flags, without -isysroot; -nostdlibinc keeps the host's /usr/include out.
CC=("$HOSTCC" -target armv6-apple-ios3.0 -miphoneos-version-min=3.0 -mfloat-abi=softfp -mfpu=vfpv2 -marm -nostdlibinc)

W="$(mktemp -d "${TMPDIR:-/tmp}/startfiles.XXXXXX")"
trap 'rm -rf "$W"' EXIT
mkdir "$W/src" "$W/obj" "$W/out"

# ---- 1. the source, at the pinned commit -------------------------------------------------------
if [ -n "${CSU_SRC_DIR:-}" ]; then
  echo "build-startfiles: source: $CSU_SRC_DIR (CSU_SRC_DIR; no fetch)"
  for f in start.s crt.c dyld_glue.s; do
    [ -f "$CSU_SRC_DIR/$f" ] || die "$CSU_SRC_DIR/$f not found"
    cp "$CSU_SRC_DIR/$f" "$W/src/$f"
  done
else
  command -v git >/dev/null || die "git not found (needed to fetch $CSU_URL; or set CSU_SRC_DIR)"
  echo "build-startfiles: fetching $CSU_URL @ $CSU_COMMIT ($CSU_TAG)"
  git init -q "$W/Csu"
  git -C "$W/Csu" remote add origin "$CSU_URL"
  git -C "$W/Csu" -c core.autocrlf=false fetch -q --depth 1 origin "$CSU_COMMIT" || die "could not fetch $CSU_COMMIT from $CSU_URL"
  git -C "$W/Csu" -c core.autocrlf=false -c advice.detachedHead=false checkout -q --detach FETCH_HEAD
  [ "$(git -C "$W/Csu" rev-parse HEAD)" = "$CSU_COMMIT" ] || die "the fetch did not give commit $CSU_COMMIT"
  cp "$W/Csu/start.s" "$W/Csu/crt.c" "$W/Csu/dyld_glue.s" "$W/src/"
fi
( cd "$W/src" && printf '%s\n' "$SRC_SHA256" | sha256sum -c --strict --quiet - ) \
  || die "a source file does not match its pinned sha256 (start.s, crt.c, dyld_glue.s of $CSU_TAG)"
echo "build-startfiles: sources match their pinned sha256"

# ---- 2. the recipe (variant without ld -r; see the header) --------------------------------------
run() { printf '+ %s\n' "$*"; "$@"; }
# notice.s goes into the directory of the generated unit files, which include it by name: a quoted
# #include looks in the including file's own directory first, so no other notice.s can be picked up.
cp "$NOTICE" "$W/obj/notice.s"
# crt.c -> assembly. It is the only C source: in this configuration it gives four data definitions and no code.
run "${CC[@]}" -S -Os -DCRT -fno-zero-initialized-in-bss "$W/src/crt.c" -o "$W/obj/crt.crt.s"
# crt1.o: the three sources of the Makefile's crt1.v2.o rule, in its order, then the notice, as one assembly unit
printf '%s\n' '#include "start.s"' '#include "crt.crt.s"' '#include "dyld_glue.s"' '#include "notice.s"' > "$W/obj/crt1-unit.S"
run "${CC[@]}" -c -Os -DCRT -I"$W/src" -I"$W/obj" "$W/obj/crt1-unit.S" -o "$W/out/crt1.o"
# dylib1.o and bundle1.o: dyld_glue.s, then the notice, as one assembly unit
printf '%s\n' '#include "dyld_glue.s"' '#include "notice.s"' > "$W/obj/glue-unit.S"
run "${CC[@]}" -c -Os -DCFM_GLUE -I"$W/src" -I"$W/obj" "$W/obj/glue-unit.S" -o "$W/out/dylib1.o"
run "${CC[@]}" -c -Os            -I"$W/src" -I"$W/obj" "$W/obj/glue-unit.S" -o "$W/out/bundle1.o"

# ---- 3. the outputs against their pins; only then OUTDIR is touched ------------------------------
if ! ( cd "$W/out" && printf '%s\n' "$OUT_SHA256" | sha256sum -c --strict --quiet - ); then
  ( cd "$W/out" && sha256sum crt1.o dylib1.o bundle1.o ) >&2
  die "a built object does not match its pinned sha256 (host compiler: $("$HOSTCC" --version | head -n 1)); nothing written to $OUT"
fi
mkdir -p "$OUT"
for f in crt1.o dylib1.o bundle1.o; do
  cp "$W/out/$f" "$OUT/$f.tmp.$$"
  chmod 644 "$OUT/$f.tmp.$$"
  mv -f "$OUT/$f.tmp.$$" "$OUT/$f"
done
echo "build-startfiles: built from $CSU_TAG ($CSU_COMMIT) with $("$HOSTCC" --version | head -n 1); outputs match their pinned sha256:"
( cd "$OUT" && for f in crt1.o dylib1.o bundle1.o; do printf '  %s  %5s bytes  %s\n' "$(sha256sum < "$f" | cut -d' ' -f1)" "$(stat -c %s "$f")" "$f"; done )
