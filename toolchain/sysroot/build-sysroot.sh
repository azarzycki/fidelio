#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
# build-sysroot.sh [--update-pins] OUTDIR
#
# Builds the complete sysroot of the ARM build, the directory that IOS3_SDK names, from nothing, and
# writes it to OUTDIR, which must not exist yet or must be an empty directory:
#   OUTDIR/usr/lib/...                                  stub libraries (stubs/build-stubs.sh)
#   OUTDIR/System/Library/Frameworks/<X>.framework/<X>  stub libraries of the frameworks (idem)
#   OUTDIR/SHA256SUMS                                   the sha256 of every stub (idem)
#   OUTDIR/usr/include/...                              C library, kernel (headers/build-headers.sh)
#   OUTDIR/System/Library/Frameworks/CoreFoundation.framework/Headers/...   (idem)
#   OUTDIR/System/Library/Frameworks/<X>.framework/Headers/...   this project's own framework headers
#                                                       (frameworks/<X>.framework/Headers, installed as
#                                                       they are)
# No file of Apple's iPhone SDK is read, and the script works without one. README.md (next to this
# script) says what each part is and what was compared.
#
# The framework headers are files of this repository. They are an allow-list, like the stub manifest
# and the header list of stage 2: frameworks/HEADERS.sha256 names every one with its sha256, and the
# directory must hold exactly those files (no other file, no symlink). A header is added or changed
# deliberately: edit it, run "build-sysroot.sh --update-pins OUTDIR", which rewrites
# frameworks/HEADERS.sha256 from the files instead of checking them, and review the change.
#
# Checks: the framework headers match their pins before anything is built; the two scripts called
# check their own parts; the three parts must not provide the same file; the result holds no symlink
# whose target leaves the tree. On any failure nothing is written to OUTDIR. Two runs give the same
# bytes.
#
# Needs what the two scripts need (see their headers): the host clang 18, the toolchain's
# arm-apple-darwin9-ld, python3, git, unifdef, a host C compiler, bison and flex, sha256sum, awk,
# sed. No SDK.
#
# Environment: passed on to the two scripts, among them
#   IOS3_OSS_CACHE   directory with <repo>/<tag>/ checkouts of Apple's releases (build-headers.sh)
#   IOS3_OSS_CACHE_FILL  1 = releases missing from IOS3_OSS_CACHE are fetched into it and kept
#   IOS3_TOOLCHAIN, IOS3_HOST_CLANG, HOST_CC, BISON, FLEX
# tools/build-arm.sh runs this script itself when IOS3_SDK is not set (see README.md).
set -euo pipefail

die() { echo "build-sysroot: ERROR: $*" >&2; exit 1; }

UPDATE_PINS=0
if [ "${1:-}" = --update-pins ]; then UPDATE_PINS=1; shift; fi
[ $# = 1 ] && [ -n "$1" ] || { echo "usage: build-sysroot.sh [--update-pins] OUTDIR" >&2; exit 2; }
OUT="$1"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
FW="$HERE/frameworks"
PINS="$FW/HEADERS.sha256"
for f in stubs/build-stubs.sh headers/build-headers.sh; do
  [ -x "$HERE/$f" ] || die "$HERE/$f not found (it belongs next to this script)"
done
[ -d "$FW" ] || die "$FW not found (this project's framework headers; it belongs next to this script)"
[ $UPDATE_PINS = 1 ] || [ -f "$PINS" ] || die "$PINS not found (the sha256 of every framework header)"
if [ -e "$OUT" ] || [ -L "$OUT" ]; then
  [ -d "$OUT" ] && [ ! -L "$OUT" ] && [ -z "$(ls -A "$OUT")" ] \
    || die "$OUT exists and is not an empty directory: the sysroot goes into a new tree (this script never overwrites or adds to one)"
fi
for t in sha256sum awk sed sort find realpath; do command -v "$t" >/dev/null || die "$t not found"; done
export LC_ALL=C

W="$(mktemp -d "${TMPDIR:-/tmp}/sysroot.XXXXXX")"
trap 'rm -rf "$W"' EXIT

# ---- 1. the framework headers against their pins (first: it is quick) ------------------------------------
[ -z "$(find "$FW" -type l)" ] || die "$FW holds a symlink"
( cd "$FW" && find . -type f ! -path ./HEADERS.sha256 | sed 's|^\./||' | sort ) > "$W/fw.lst"
if grep -v -E '^[A-Za-z0-9]+\.framework/Headers/[A-Za-z0-9_]+\.h$' "$W/fw.lst" >&2; then
  die "the files above in $FW are not <Name>.framework/Headers/<file>.h"
fi
( cd "$FW" && xargs -d '\n' sha256sum < "$W/fw.lst" ) > "$W/fw.sha256"
if [ $UPDATE_PINS = 1 ]; then
  cp "$W/fw.sha256" "$PINS"
  echo "build-sysroot: --update-pins: wrote $PINS ($(wc -l < "$PINS") files)"
elif ! cmp -s "$W/fw.sha256" "$PINS"; then
  diff "$PINS" "$W/fw.sha256" >&2 || true
  die "the framework headers do not match $PINS; nothing written to $OUT"
fi
echo "build-sysroot: $(wc -l < "$W/fw.lst") framework headers match frameworks/HEADERS.sha256"

# ---- 2. stubs and published headers ------------------------------------------------------------------------
echo "build-sysroot: stubs/build-stubs.sh"
"$HERE/stubs/build-stubs.sh" "$W/stubs" > "$W/stubs.log" 2>&1 || { cat "$W/stubs.log" >&2; die "build-stubs.sh failed"; }
grep -E '^build-stubs: STUBS OK' "$W/stubs.log" | sed 's/^/  /' || true
echo "build-sysroot: headers/build-headers.sh${IOS3_OSS_CACHE:+ (IOS3_OSS_CACHE=$IOS3_OSS_CACHE${IOS3_OSS_CACHE_FILL:+, IOS3_OSS_CACHE_FILL=$IOS3_OSS_CACHE_FILL})}"
"$HERE/headers/build-headers.sh" "$W/headers" > "$W/headers.log" 2>&1 || { cat "$W/headers.log" >&2; die "build-headers.sh failed"; }
grep -E '^build-headers: [^ ]+ [^ ]+: ((from|fetching) |.*: fetching it again$)' "$W/headers.log" | sed 's/^/  /' || true
tail -n 1 "$W/headers.log" | sed 's/^/  /'

# ---- 3. the tree: three parts, no file in two of them --------------------------------------------------------
mkdir "$W/fw"
while IFS= read -r f; do
  mkdir -p "$W/fw/System/Library/Frameworks/$(dirname "$f")"
  cp "$FW/$f" "$W/fw/System/Library/Frameworks/$f"
done < "$W/fw.lst"
for p in stubs headers fw; do ( cd "$W/$p" && find . ! -type d | sed 's|^\./||' ); done | sort | uniq -d > "$W/dup.lst"
[ ! -s "$W/dup.lst" ] || { cat "$W/dup.lst" >&2; die "the files above would come from two parts of the sysroot"; }
mkdir "$W/root"
for p in stubs headers fw; do cp -R -P "$W/$p/." "$W/root/"; done
find "$W/root" -type d -exec chmod 755 {} +
find "$W/root" -type f -exec chmod 644 {} +
while IFS= read -r l; do
  t="$(readlink "$l")"
  case "$t" in /*) die "symlink ${l#"$W"/root/} -> $t is absolute" ;; esac
  r="$(cd "$(dirname "$l")" && realpath -m "$t")"
  case "$r" in "$W/root"/*) ;; *) die "symlink ${l#"$W"/root/} -> $t leaves the tree" ;; esac
  [ -e "$l" ] || die "symlink ${l#"$W"/root/} -> $t points to nothing"
done < <(find "$W/root" -type l)

# ---- 4. OUTDIR -------------------------------------------------------------------------------------------------
mkdir -p "$OUT"
cp -R -P "$W/root/." "$OUT/"
echo "build-sysroot: wrote $OUT:"
( cd "$OUT"
  printf '  %-56s %4s files, %s symlinks\n' "usr/lib (stubs)" "$(find usr/lib -type f | wc -l)" "$(find usr/lib -type l | wc -l)"
  printf '  %-56s %4s header files\n' "usr/include" "$(find usr/include -type f | wc -l)"
  for d in System/Library/Frameworks/*.framework; do
    n="$(basename "$d" .framework)"; s=""; h=0
    [ -f "$d/$n" ] && s=" + stub library"
    [ -d "$d/Headers" ] && h="$(find "$d/Headers" -type f | wc -l)"
    printf '  %-56s %4s header files%s\n' "$d" "$h" "$s"
  done
  printf '  %-56s\n' "SHA256SUMS (the stubs)" )
echo "build-sysroot: OK: $(find "$OUT" -type f | wc -l) files and $(find "$OUT" -type l | wc -l) relative symlinks in $OUT: the stubs, $(find "$W/headers" -type f | wc -l) published headers, $(wc -l < "$W/fw.lst") framework headers of this project; nothing read from an SDK"
