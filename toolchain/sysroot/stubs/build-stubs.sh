#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
# build-stubs.sh OUTDIR
#
# Builds the stub libraries of the sysroot from manifest.tsv, the file next to this script, and writes
# them to OUTDIR, which must not exist yet or must be an empty directory:
#   OUTDIR/usr/lib/libSystem.B.dylib, ...                      one stub per "lib" row, at the path that
#   OUTDIR/System/Library/Frameworks/<X>.framework/<X>, ...    its install name gives
#   OUTDIR/usr/lib/libSystem.dylib -> libSystem.B.dylib, ...   one symlink per "link" row: the names
#                                                              that -lSystem and -lobjc look for
#   OUTDIR/SHA256SUMS                                          the sha256 of every stub (sha256sum -c)
# A link that finds these files where it would find the libraries of Apple's iPhone SDK (IOS3_SDK,
# -isysroot) records the same library names, versions and symbol bindings. No file of the SDK is
# read here, and the script works without one. ../README.md says what was compared.
#
# What a stub is: a thin ARMv6 dylib (MH_DYLIB) with
#   - the install name, current version and compatibility version of its row (LC_ID_DYLIB). The
#     linker copies them into the LC_LOAD_DYLIB command of every binary that links the library;
#   - an LC_REEXPORT_DYLIB command for each library its row re-exports, in the order of the row.
#     Foundation re-exports libobjc and CoreFoundation: a binary linked with "-framework Foundation"
#     alone then names all three, as it does with the SDK;
#   - one exported symbol per "sym" row, and nothing else: no code of any library, no other load
#     command that names a library, no LC_UUID, no version command.
#
# The symbols. Every export is a label in __TEXT,__text with a word of its own behind it, so each has
# its own address. The word is 0xe7ffdefe, an instruction that is permanently undefined in ARM state
# (the trap that compilers emit for __builtin_trap). The manifest does not say whether a name is a
# function or a variable, and the stubs do not need to know: at this deployment target a dylib has
# the classic symbol table, of which the linker reads the name of each export and whether it is a weak
# definition. Which section a symbol lies in, and what is stored there, does not reach the binary.
# So one kind serves for all, and the trap is the honest content for a place that must never be
# executed or read: a stub is a link-time file and is never installed on a device. No stub defines a
# symbol weakly, because no binary of this repository is bound to a weak definition; imports.py
# stops if one ever is.
#
# The recipe, per library:
#   1. the host clang assembles a file of labels, in the order of the manifest;
#   2. the toolchain's linker makes the dylib:
#        ld -dylib -arch armv6 -iphoneos_version_min 3.0 -no_version_load_command -no_function_starts
#           -no_data_in_code_info -no_uuid -Z -install_name NAME -current_version V
#           -compatibility_version C LABELS.o [-reexport_library CHILD]... seed.dylib -o STUB
#      A library is built after the libraries it re-exports.
# The seed: this ld64 refuses to write a dylib that links no dylib ("dynamic executables or dylibs
# must link with libSystem.dylib"). A stub must not depend on anything, so every stub is linked
# against one more dylib, the seed, which is made first and never leaves the work directory:
#   - it is empty and has the install name /usr/lib/system/libsystem_kernel.dylib, the one name for
#     which the linker waives that rule (the lowest layer of libSystem links nothing), so the seed
#     itself can be made;
#   - it is marked MH_DEAD_STRIPPABLE_DYLIB (-mark_dead_strippable_dylib). The linker writes no load
#     command for such a dylib when nothing is taken from it, and a stub takes nothing.
# -dead_strip_dylibs on the stub's own link would also drop the seed, but it drops every dylib that
# provides no symbol, the re-exported ones too: Foundation would lose its two LC_REEXPORT_DYLIB
# commands. With the mark on the seed one recipe serves every stub.
# -Z keeps the host's /usr/lib and /usr/local/lib out of the linker's search path.
#
# The result is deterministic: no time stamp, no UUID, no path of the build machine; two runs give the
# same bytes (SHA256SUMS says which). Before anything is written to OUTDIR, the script checks what it
# built: the tree holds exactly the files and links of the manifest, and "imports.py stubs" reads
# every stub back and compares its type, install name, versions, load commands and export list with
# its rows. The linker must not print anything; a warning stops the script.
#
# Needs: the host clang 18 (as assembler; it is called directly, because the armv6-ios3-clang wrapper
#   refuses to run without an SDK), the toolchain's arm-apple-darwin9-ld, python3 (imports.py),
#   awk, sha256sum. No SDK, no network.
#
# Environment:
#   IOS3_HOST_CLANG  the host clang (default: clang-18 if present, else clang), as in armv6-ios3-clang
#   IOS3_TOOLCHAIN   the armv6-ios3-toolchain directory (default: arm-apple-darwin9-ld from PATH)
set -euo pipefail

die() { echo "build-stubs: ERROR: $*" >&2; exit 1; }

[ $# = 1 ] && [ -n "$1" ] || { echo "usage: build-stubs.sh OUTDIR" >&2; exit 2; }
OUT="$1"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
MANIFEST="$HERE/manifest.tsv"
IMPORTS="$HERE/imports.py"
[ -f "$MANIFEST" ] || die "$MANIFEST not found (the list of libraries and symbols; it belongs next to this script)"
[ -f "$IMPORTS" ] || die "$IMPORTS not found (it checks the manifest and the stubs; it belongs next to this script)"

HOSTCC="${IOS3_HOST_CLANG:-}"
if [ -z "$HOSTCC" ]; then
  if command -v clang-18 >/dev/null; then HOSTCC=clang-18; else HOSTCC=clang; fi
fi
command -v "$HOSTCC" >/dev/null || die "host compiler '$HOSTCC' not found (apt install clang-18, or set IOS3_HOST_CLANG)"
if [ -n "${IOS3_TOOLCHAIN:-}" ]; then
  LD="$IOS3_TOOLCHAIN/bin/arm-apple-darwin9-ld"
  [ -x "$LD" ] || die "$LD not found (IOS3_TOOLCHAIN=$IOS3_TOOLCHAIN is not an armv6-ios3-toolchain directory)"
else
  LD="$(command -v arm-apple-darwin9-ld || true)"
  [ -n "$LD" ] || die "arm-apple-darwin9-ld not found: put the toolchain's bin/ in PATH, or set IOS3_TOOLCHAIN"
fi
for t in python3 awk sha256sum; do
  command -v "$t" >/dev/null || die "$t not found"
done
if [ -e "$OUT" ] || [ -L "$OUT" ]; then
  [ -d "$OUT" ] && [ ! -L "$OUT" ] && [ -z "$(ls -A "$OUT")" ] \
    || die "$OUT exists and is not an empty directory: the stubs go into a new tree (this script never overwrites or adds to one)"
fi
export PYTHONDONTWRITEBYTECODE=1

# ---- 1. the manifest ----------------------------------------------------------------------------
# imports.py reads it strictly (fields, order, names that are safe to put into an assembly file and
# into a path); the awk calls below rely on that.
python3 -I "$IMPORTS" validate "$MANIFEST" || die "$MANIFEST is not usable"

W="$(mktemp -d "${TMPDIR:-/tmp}/stubs.XXXXXX")"
trap 'rm -rf "$W"' EXIT
mkdir "$W/obj" "$W/root"

AS=("$HOSTCC" -target armv6-apple-ios3.0 -c)
LINK=("$LD" -dylib -arch armv6 -iphoneos_version_min 3.0 -no_version_load_command -no_function_starts
      -no_data_in_code_info -no_uuid -Z)
run() { printf '+ %s\n' "$*"; "$@" < /dev/null; }
# The linker runs inside the work directory, on relative names, and must stay silent.
run_ld() {
  printf '+ ld %s\n' "$*"
  ( cd "$W" && "${LINK[@]}" "$@" ) < /dev/null > "$W/ld.out" 2>&1 || { cat "$W/ld.out" >&2; die "the linker failed"; }
  [ ! -s "$W/ld.out" ] || { cat "$W/ld.out" >&2; die "the linker printed a message (this recipe links without warnings)"; }
}

# ---- 2. the seed (see the header) -----------------------------------------------------------------
: > "$W/obj/seed.s"
run "${AS[@]}" "$W/obj/seed.s" -o "$W/obj/seed.o"
run_ld -install_name /usr/lib/system/libsystem_kernel.dylib -mark_dead_strippable_dylib obj/seed.o -o obj/seed.dylib

# ---- 3. the stubs: a library after the libraries it re-exports --------------------------------------
NLIBS="$(awk -F'\t' '$1 == "lib"' "$MANIFEST" | wc -l)"
[ "$NLIBS" -gt 0 ] || die "$MANIFEST has no lib row"
built=" "    # the install names done so far, each between two spaces
n=0
while [ "$n" -lt "$NLIBS" ]; do
  before="$n"
  while IFS=$'\t' read -r _ path name current compat reexports; do
    case "$built" in *" $name "*) continue ;; esac
    children=()
    if [ "$reexports" != - ]; then
      for r in $reexports; do
        case "$built" in *" $r "*) children+=(-reexport_library "root$r") ;; *) continue 2 ;; esac   # not built yet: a later pass
      done
    fi
    n=$((n + 1))
    {
      printf '\t.section\t__TEXT,__text,regular,pure_instructions\n\t.p2align\t2\n'
      awk -F'\t' -v lib="$name" '$1 == "sym" && $2 == lib { printf "\t.globl\t\"%s\"\n\"%s\":\n\t.long\t0xe7ffdefe\n", $3, $3 }' "$MANIFEST"
    } > "$W/obj/$n.s"
    run "${AS[@]}" "$W/obj/$n.s" -o "$W/obj/$n.o"
    mkdir -p "$(dirname "$W/root/$path")"
    run_ld -install_name "$name" -current_version "$current" -compatibility_version "$compat" \
           "obj/$n.o" ${children[@]+"${children[@]}"} obj/seed.dylib -o "root/$path"
    chmod 644 "$W/root/$path"
    built="$built$name "
  done < <(awk -F'\t' '$1 == "lib"' "$MANIFEST")
  [ "$n" -gt "$before" ] || die "the re-exports of the libraries not built yet form a circle"
done
while IFS=$'\t' read -r _ path target; do
  mkdir -p "$(dirname "$W/root/$path")"
  ln -s "$target" "$W/root/$path"
done < <(awk -F'\t' '$1 == "link"' "$MANIFEST")
find "$W/root" -type d -exec chmod 755 {} +

# ---- 4. what was built against the manifest; only then OUTDIR is touched ----------------------------
if ! diff <(awk -F'\t' '$1 == "lib" || $1 == "link" { print $2 }' "$MANIFEST" | LC_ALL=C sort) \
          <(cd "$W/root" && find . ! -type d | sed 's|^\./||' | LC_ALL=C sort) >&2; then
  die "the tree that was built does not hold exactly the files and links of the manifest; nothing written to $OUT"
fi
python3 -I "$IMPORTS" stubs "$MANIFEST" "$W/root" > "$W/check.out" 2>&1 \
  || { cat "$W/check.out" >&2; die "a stub does not match the manifest; nothing written to $OUT"; }
( cd "$W/root" && awk -F'\t' '$1 == "lib" { print $2 }' "$MANIFEST" | LC_ALL=C sort | while IFS= read -r path; do sha256sum "$path"; done ) > "$W/SHA256SUMS"
mv "$W/SHA256SUMS" "$W/root/SHA256SUMS"
chmod 644 "$W/root/SHA256SUMS"
mkdir -p "$OUT"
cp -R -P --preserve=mode "$W/root/." "$OUT/"

echo "build-stubs: $(tail -n 1 "$W/check.out" | sed 's/^=> //')"
echo "build-stubs: built with $("$HOSTCC" --version | head -n 1) and $("$LD" -v 2>&1 | sed -n '1s/.*PROJECT://p'); manifest sha256 $(sha256sum < "$MANIFEST" | cut -d' ' -f1)"
echo "build-stubs: wrote $OUT:"
( cd "$OUT" && while read -r sum path; do printf '  %s  %6s bytes  %s\n' "$sum" "$(stat -c %s "$path")" "$path"; done < SHA256SUMS )
( cd "$OUT" && awk -F'\t' '$1 == "link" { print $2 }' "$MANIFEST" | LC_ALL=C sort | while IFS= read -r path; do printf '  %s -> %s\n' "$path" "$(readlink "$path")"; done )
