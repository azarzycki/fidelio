#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# toolchain/pack.sh --base DIR [--out DIR]
#
# Assembles the release tarball of the armv6-ios3 cross toolchain,
#   armv6-ios3-toolchain-linux-x86_64.tar.xz    (one top directory: armv6-ios3-toolchain/)
# and prints its size and sha256. Two runs on the same inputs give the same bytes.
#
#   --base DIR   an unpacked toolchain (any revision). ONLY the parts that are built from upstream
#                sources come from it, unchanged (spotify/BUILD.md says how they are built):
#                  bin/arm-apple-darwin9-*, bin/ldid      the ELF host tools
#                  lib/libdispatch.so, lib/libBlocksRuntime.so, lib/libplist-2.0.so.4
#                  armv6-ios3/include/c++/v1/             the libc++ headers
#                  armv6-ios3/lib/{libc++,libc++abi,libc++experimental,libios3compat}.a
#   --out DIR    where the tarball and the staging tree go (default: "toolchain-dist" next to the
#                repo; never inside it). DIR/stage is removed and made again on every run.
#
# Everything else comes from this repository (the working tree this script lives in):
#   toolchain/bin/*                         -> bin/                      the three wrapper scripts
#   (made here)                             -> bin/armv6-ios3-<tool>     symlinks to arm-apple-darwin9-<tool>, and
#                                              bin/armv6-ios3-clang++    -> armv6-ios3-clang
#   toolchain/share/armv6-ios3/toolchain.cmake -> share/armv6-ios3/
#   toolchain/src/ios3compat/*              -> share/armv6-ios3/src/ios3compat/
#   toolchain/src/ios3compat/ios3compat.h   -> armv6-ios3/include/       (the header lives in both places)
#   toolchain/include/ios3_tls.hpp          -> armv6-ios3/include/
#   toolchain/startfiles/build-startfiles.sh, run here -> armv6-ios3/lib/startfiles/{crt1,dylib1,bundle1}.o
#   toolchain/startfiles/build-startfiles.sh, notice.s (the notice the script assembles into the objects)
#                                           + the APSL 1.1 and 2.0 texts -> share/armv6-ios3/src/startfiles/
#   spotify/BUILD.md                        -> share/armv6-ios3/BUILD.md
#   spotify/patches/{cctools-port,ldid,llvm-project}-*.patch -> share/armv6-ios3/patches/   (not the cspot patch)
#   toolchain/licenses/*                    -> LICENSES/                 (with MANIFEST.tsv)
#   toolchain/THIRD-PARTY-NOTICES.md, LICENSE (the repo's), toolchain/VERSION -> the top directory
#   toolchain/README.md, its marked text block -> README.txt
#
# Before the tarball is written the staged tree is checked, and the run FAILS if
#   * a start-up object is missing,
#   * a licence file named in LICENSES/MANIFEST.tsv is missing or does not match its sha256 and size,
#     or LICENSES/ holds a file the manifest does not name,
#   * a staged file contains one of the FORBIDDEN strings below (the tarball must carry no link to a
#     file-sharing or web-archive host),
#   * IOS3_SDK names Apple's SDK and a staged file has the sha256 of a file under $IOS3_SDK: nothing
#     of Apple's SDK may be in the tarball. The check runs only when IOS3_SDK is Apple's SDK, known by
#     two files that only it has, SDKSettings.plist and usr/lib/crt1.o (a sysroot made by
#     toolchain/sysroot/build-sysroot.sh has neither). When IOS3_SDK is unset or names another
#     directory, the check does not run, and the script says so.
#
# Determinism: names sorted, one fixed mtime (PACK_MTIME), owner and group 0, modes set here (755 for
# directories and programs, 644 for the rest), ustar format, xz -9 on one thread. The bytes can still
# differ between versions of tar or xz.
#
# Needs: bash, GNU tar and coreutils, xz, awk, and what build-startfiles.sh needs (git, a host clang 18).
# Environment: IOS3_SDK (optional, see above); IOS3_HOST_CLANG and CSU_SRC_DIR as in build-startfiles.sh.
set -euo pipefail
export LC_ALL=C

NAME="armv6-ios3-toolchain"
TARBALL="$NAME-linux-x86_64.tar.xz"
PACK_MTIME=1795305600   # 2026-11-22 00:00:00 UTC, the day revision 3 was put together (revision 2: 1795219200, the day before)
FORBIDDEN=("drive.google" "drive.usercontent" "archive.org")
CCTOOLS_LINKS="ar codesign_allocate install_name_tool ld libtool lipo nm nmedit otool ranlib size strings strip"
HOST_LIBS="libdispatch.so libBlocksRuntime.so libplist-2.0.so.4"
ARM_ARCHIVES="libc++.a libc++abi.a libc++experimental.a libios3compat.a"
TC_PATCHES="cctools-port-0001-no_version_load_command.patch ldid-0001-LDID_CD_VERSION.patch llvm-project-18.1.3-0001-libcxx-libcxxabi-ios3.patch"

die()  { echo "pack: ERROR: $*" >&2; exit 1; }
note() { echo "pack: $*"; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO="$(cd "$HERE/.." && pwd -P)"
BASE=""; OUT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --base) [ $# -ge 2 ] || die "--base needs a directory"; BASE="$2"; shift 2 ;;
    --out)  [ $# -ge 2 ] || die "--out needs a directory";  OUT="$2";  shift 2 ;;
    -h|--help) awk 'NR > 2 && !/^#/ { exit }  NR > 2 { sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) die "unknown argument '$1' (usage: pack.sh --base DIR [--out DIR])" ;;
  esac
done
[ -n "$BASE" ] || die "usage: pack.sh --base DIR [--out DIR]"
[ -d "$BASE" ] || die "--base '$BASE' is not a directory"
BASE="$(cd "$BASE" && pwd -P)"
for t in tar xz sha256sum awk find sort realpath; do command -v "$t" >/dev/null || die "$t not found"; done
OUT="$(realpath -m "${OUT:-$(dirname "$REPO")/toolchain-dist}")"
case "$OUT/" in "$REPO"/*) die "the output directory ($OUT) must be outside the repo";; esac
case "$OUT/" in "$BASE"/*) die "the output directory ($OUT) must be outside the base toolchain";; esac
mkdir -p "$OUT"
case "$(tar --version 2>/dev/null)" in *"GNU tar"*) ;; *) die "GNU tar is needed (--sort, --mtime, --owner)";; esac

T="$REPO/toolchain"
VERSION="$(head -n 1 "$T/VERSION")"
case "$VERSION" in ''|*[!0-9]*) die "toolchain/VERSION must hold one number, got '$VERSION'";; esac
head="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo '?')"
dirty="$(git -C "$REPO" status --short 2>/dev/null | wc -l)"
note "toolchain revision $VERSION from $REPO @ $head ($( [ "$dirty" = 0 ] && echo 'git status clean' || echo "git status: $dirty entries"))"
note "base: $BASE$( [ -f "$BASE/VERSION" ] && echo " (revision $(head -n 1 "$BASE/VERSION"))" || echo ' (no VERSION file: revision 1)')"

STAGE_ROOT="$OUT/stage"
S="$STAGE_ROOT/$NAME"
rm -rf "$STAGE_ROOT"
mkdir -p "$S/bin" "$S/lib" "$S/armv6-ios3/include" "$S/armv6-ios3/lib/startfiles" "$S/LICENSES" \
         "$S/share/armv6-ios3/patches" "$S/share/armv6-ios3/src/ios3compat" "$S/share/armv6-ios3/src/startfiles"

is_elf()     { [ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; }
is_archive() { [ "$(head -c 8 "$1")" = '!<arch>' ]; }
put()        { [ -f "$1" ] || die "missing: $1"; cp "$1" "$2"; }   # SRC DST (a regular file)

# ---- 1. from the base toolchain: the parts built from upstream sources, unchanged ----------------
n=0
for f in "$BASE"/bin/arm-apple-darwin9-* "$BASE/bin/ldid"; do
  [ -f "$f" ] && [ ! -L "$f" ] || die "base: $f is missing or not a regular file"
  is_elf "$f" || die "base: $f is not an ELF file"
  cp "$f" "$S/bin/"; n=$((n + 1))
done
for l in $HOST_LIBS; do
  [ -f "$BASE/lib/$l" ] || die "base: lib/$l is missing"
  is_elf "$BASE/lib/$l" || die "base: lib/$l is not an ELF file"
  cp "$BASE/lib/$l" "$S/lib/"; n=$((n + 1))
done
for a in $ARM_ARCHIVES; do
  [ -f "$BASE/armv6-ios3/lib/$a" ] || die "base: armv6-ios3/lib/$a is missing"
  is_archive "$BASE/armv6-ios3/lib/$a" || die "base: armv6-ios3/lib/$a is not an ar archive"
  cp "$BASE/armv6-ios3/lib/$a" "$S/armv6-ios3/lib/"
done
[ -f "$BASE/armv6-ios3/include/c++/v1/__config" ] || die "base: armv6-ios3/include/c++/v1 (the libc++ headers) is missing"
[ -z "$(find "$BASE/armv6-ios3/include/c++" ! -type f ! -type d | head -n 1)" ] || die "base: armv6-ios3/include/c++ holds something that is neither a file nor a directory"
cp -R "$BASE/armv6-ios3/include/c++" "$S/armv6-ios3/include/"
note "from the base: $n ELF files, $(echo $ARM_ARCHIVES | wc -w) ARMv6 archives, $(find "$S/armv6-ios3/include/c++" -type f | wc -l) libc++ headers"

# ---- 2. from the repo -----------------------------------------------------------------------------
for f in armv6-ios3-clang armv6-ios3-check armv6-ios3-ldid; do put "$T/bin/$f" "$S/bin/$f"; done
for t in $CCTOOLS_LINKS; do
  [ -f "$S/bin/arm-apple-darwin9-$t" ] || die "base: bin/arm-apple-darwin9-$t is missing (armv6-ios3-$t would dangle)"
  ln -s "arm-apple-darwin9-$t" "$S/bin/armv6-ios3-$t"
done
ln -s armv6-ios3-clang "$S/bin/armv6-ios3-clang++"
put "$T/share/armv6-ios3/toolchain.cmake" "$S/share/armv6-ios3/toolchain.cmake"
put "$T/src/ios3compat/ios3compat.c"      "$S/share/armv6-ios3/src/ios3compat/ios3compat.c"
put "$T/src/ios3compat/ios3compat.h"      "$S/share/armv6-ios3/src/ios3compat/ios3compat.h"
put "$T/src/ios3compat/ios3compat.h"      "$S/armv6-ios3/include/ios3compat.h"
put "$T/include/ios3_tls.hpp"             "$S/armv6-ios3/include/ios3_tls.hpp"
put "$REPO/spotify/BUILD.md"              "$S/share/armv6-ios3/BUILD.md"
for p in $TC_PATCHES; do put "$REPO/spotify/patches/$p" "$S/share/armv6-ios3/patches/$p"; done
put "$T/startfiles/build-startfiles.sh"   "$S/share/armv6-ios3/src/startfiles/build-startfiles.sh"
put "$T/startfiles/notice.s"              "$S/share/armv6-ios3/src/startfiles/notice.s"   # the script reads it from its own directory
put "$T/licenses/APSL-1.1.txt"            "$S/share/armv6-ios3/src/startfiles/APSL-1.1.txt"
put "$T/licenses/cctools-port.APSL-2.0.txt" "$S/share/armv6-ios3/src/startfiles/APSL-2.0.txt"
for f in "$T"/licenses/*; do put "$f" "$S/LICENSES/$(basename "$f")"; done
put "$T/THIRD-PARTY-NOTICES.md" "$S/THIRD-PARTY-NOTICES.md"
put "$REPO/LICENSE"             "$S/LICENSE"
put "$T/VERSION"                "$S/VERSION"
# README.txt: the first fenced text block after the "README.txt" marker line of toolchain/README.md
awk '/^<!-- README.txt/ { m = 1; next }  m && /^```/ { if (b) exit; b = 1; next }  b' "$T/README.md" > "$S/README.txt"
[ -s "$S/README.txt" ] || die "toolchain/README.md has no README.txt block (a fenced block after the '<!-- README.txt' line)"

# ---- 3. the start-up objects, built from the pinned Csu source ------------------------------------
bash "$T/startfiles/build-startfiles.sh" "$S/armv6-ios3/lib/startfiles" || die "build-startfiles.sh failed"

# ---- 4. modes ----------------------------------------------------------------------------------------
find "$S" -type d -exec chmod 755 {} +
find "$S" -type f -exec chmod 644 {} +
find "$S/bin" -type f -exec chmod 755 {} +
chmod 755 "$S/share/armv6-ios3/src/startfiles/build-startfiles.sh"

# ---- 5. self-check of the staged tree ---------------------------------------------------------------
fail=0
bad() { echo "pack: CHECK FAILED: $*" >&2; fail=1; }

for f in crt1.o dylib1.o bundle1.o; do
  [ -s "$S/armv6-ios3/lib/startfiles/$f" ] || bad "start-up object armv6-ios3/lib/startfiles/$f is missing"
done

M="$S/LICENSES/MANIFEST.tsv"
if [ ! -f "$M" ]; then
  bad "LICENSES/MANIFEST.tsv is missing"
else
  rows=0; before="$fail"; fail=0
  while IFS=$'\t' read -r file sha bytes _; do
    [ "$file" = file ] && continue        # the header row
    [ -n "$file" ] || continue
    rows=$((rows + 1))
    if [ ! -f "$S/LICENSES/$file" ]; then bad "licence file LICENSES/$file (named in the manifest) is missing"; continue; fi
    [ "$(sha256sum < "$S/LICENSES/$file" | cut -d' ' -f1)" = "$sha" ] || bad "LICENSES/$file does not have the sha256 in the manifest"
    [ "$(stat -c %s "$S/LICENSES/$file")" = "$bytes" ] || bad "LICENSES/$file does not have the size in the manifest"
  done < "$M"
  [ "$rows" -gt 0 ] || bad "LICENSES/MANIFEST.tsv names no file"
  for f in "$S"/LICENSES/*; do
    b="$(basename "$f")"; [ "$b" = MANIFEST.tsv ] && continue
    awk -F'\t' -v n="$b" '$1 == n { f = 1 }  END { exit !f }' "$M" || bad "LICENSES/$b is not named in the manifest"
  done
  if [ "$fail" = 0 ]; then
    note "licences: $rows files named in LICENSES/MANIFEST.tsv, all present with the stated sha256 and size, and no other file in LICENSES/"
  fi
  [ "$before" = 0 ] || fail=1
fi

pats=(); for s in "${FORBIDDEN[@]}"; do pats+=(-e "$s"); done
hits="$(grep -r -l -a -F "${pats[@]}" "$S" || true)"
if [ -n "$hits" ]; then
  while read -r h; do bad "forbidden string (${FORBIDDEN[*]}) in ${h#"$S"/}"; done <<< "$hits"
else
  note "forbidden strings (${FORBIDDEN[*]}): none in any staged file"
fi

( cd "$S" && find . -type f -print0 | sort -z | xargs -0 sha256sum ) > "$STAGE_ROOT/staged.sha256"
if [ -z "${IOS3_SDK:-}" ]; then
  note "SDK check: NOT RUN (IOS3_SDK is not set): the staged files were not compared with Apple's SDK"
elif [ ! -d "$IOS3_SDK" ]; then
  die "IOS3_SDK=$IOS3_SDK is not a directory"
elif [ ! -f "$IOS3_SDK/SDKSettings.plist" ] || [ ! -f "$IOS3_SDK/usr/lib/crt1.o" ]; then
  note "SDK check: NOT RUN (IOS3_SDK=$IOS3_SDK is not Apple's SDK: it has no SDKSettings.plist and usr/lib/crt1.o): the staged files were not compared with Apple's SDK"
else
  ( cd "$IOS3_SDK" && find . -type f -print0 | xargs -0 sha256sum ) > "$STAGE_ROOT/sdk.sha256"
  # sha256sum lines: 64 hex digits, two spaces, the path (a leading backslash marks an escaped name)
  same="$(awk '{ h = $1; sub(/^\\/, "", h); p = substr($0, index($0, "  ") + 2) }
               NR == FNR { sdk[h] = p; next }
               (h in sdk) { print p "  ==  SDK " sdk[h] }' "$STAGE_ROOT/sdk.sha256" "$STAGE_ROOT/staged.sha256")"
  if [ -n "$same" ]; then
    while read -r l; do bad "a staged file has the bytes of an SDK file: $l"; done <<< "$same"
  else
    note "SDK check: none of the $(wc -l < "$STAGE_ROOT/staged.sha256") staged files has the sha256 of any of the $(wc -l < "$STAGE_ROOT/sdk.sha256") files under $IOS3_SDK"
  fi
  rm -f "$STAGE_ROOT/sdk.sha256"
fi
[ "$fail" = 0 ] || die "the staged tree failed its checks; no tarball written (staged tree: $S)"

# ---- 6. the tarball -----------------------------------------------------------------------------------
rm -f "$OUT/$TARBALL" "$OUT/$TARBALL.tmp"
tar --sort=name --format=ustar --mtime="@$PACK_MTIME" --owner=0 --group=0 --numeric-owner \
    -C "$STAGE_ROOT" -cf - "$NAME" | xz -9 -T1 -c > "$OUT/$TARBALL.tmp"
mv "$OUT/$TARBALL.tmp" "$OUT/$TARBALL"
note "staged: $(find "$S" -type f | wc -l) files, $(find "$S" -type l | wc -l) symlinks, $(find "$S" -type d | wc -l) directories (hashes: $STAGE_ROOT/staged.sha256)"
note "wrote $OUT/$TARBALL"
echo "size:   $(stat -c %s "$OUT/$TARBALL") bytes"
echo "sha256: $(sha256sum < "$OUT/$TARBALL" | cut -d' ' -f1)"
