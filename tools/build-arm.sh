#!/usr/bin/env bash
# tools/build-arm.sh [REPO]   (REPO defaults to the repo this script lives in)
#
# Cross-builds every ARMv6 / iPhone OS 3 artifact of the monorepo with the armv6-ios3 toolchain,
# collects them in ARM_OUT (default $BUILD_ROOT/arm-out) and verifies each binary:
#   size, sha256, armv6-ios3-check, tools/isa_scan.py (post-ARMv6 instructions), and for the entitled
#   binaries the entitlement keys (armv6-ios3-ldid -e) against common/fidelio-springboard.entitlements.plist;
#   the binary's link map: no .o file and no archive member under $IOS3_SDK was linked, and exactly one
#   start-up object, the one for its file type, from the toolchain's armv6-ios3/lib/startfiles;
#   tools/startcheck.py: the start-up code inside the binary (its linked literals and __DATA,__dyld),
#   and the source-availability notice of the start-up object: the section __TEXT,__csu_notice must
#   hold the text that fits the file type. A binary without it fails the verification.
# It also checks the toolchain's three start-up objects against the sha256 pins in _common.sh.
#
# Toolchain: revision 3 or later (toolchain/README.md; TOOLCHAIN_MIN_REVISION in _common.sh). The
# script stops before building anything if the toolchain is older: revision 1 has no
# armv6-ios3/lib/startfiles and links the SDK's start-up objects; the start-up objects of revision 2
# carry no notice, so every binary would lack it.
# Link maps: every ARM build step runs with IOS3_LINK_MAP_DIR=$BUILD_ROOT/arm/maps/<step>, so the
# toolchain wrapper writes <output name>.map there for each link (a directory per step, because both
# lock bundles link an output named "Fidelio"). A binary without its map fails the verification.
#
#   fidelio-audiod, fidelio-lockctl  audiod/CMakeLists.txt + toolchain file (POST_BUILD strips and entitlement-signs)
#   fidelio-raop                    airplay/CMakeLists.txt, then strip -x + ldid -S
#   cspot-ios3                       spotify/cli-src against deps/cspot (upstream + repo patch), then strip -x + ldid -S
#   NowPlaying.app                   nowplaying/src/build.sh
#   Fidelio.lockbundle, Fidelio.bundle   nowplaying/lockplugin/build.sh (EXT lockbundle and bundle)
#   DockRemote.dylib                 tweak/DockRemote/build.sh
#   voltest                          audiod/probe/build.sh (a device probe of the system volume categories, B1.16.12)
#
# Exit status: 0 all built and verified; 1 a verification failed (see report); 3 setup / build error.
#
# Environment
#   IOS3_SDK       required: the iPhoneOS3.1.2.sdk directory (BUILD.md)
#   IOS3_TOOLCHAIN the armv6-ios3-toolchain directory (default: found from armv6-ios3-clang in PATH)
#   BUILD_ROOT     where everything is built (default: "build" next to the repo; never inside it)
#   ARM_OUT        where the verified binaries are collected (default $BUILD_ROOT/arm-out)
#
# Reproducibility notes:
#  * DockRemote.dylib embeds the -o path as its LC_ID_DYLIB install name, so its hash depends on
#    the outdir STRING handed to tweak/DockRemote/build.sh (and with it LC_UUID and one signature
#    page hash). This script always uses $BUILD_ROOT/arm/dockremote.
#  * cspot-ios3 embeds absolute source paths (deps/cspot/..., <REPO>/spotify/cli-src/...), so its hash
#    is only comparable between builds at the same paths. The other binaries embed no build paths:
#    three clean rebuilds gave identical hashes.
#  * cspot-ios3 also embeds a build date: civetweb's build identifier is __DATE__. cspot is therefore
#    built with SOURCE_DATE_EPOCH = CSPOT_SOURCE_DATE_EPOCH (_common.sh), which fixes that string
#    whatever day the build runs on. The value is part of the build dir's stamp.
#  * The toolchain's -L for the start-up objects and its link maps do not change a linked byte.
#    ninja does not relink when the toolchain changes, so the toolchain's path, its revision and a
#    hash of its wrapper and start-up objects are part of every CMake build dir's stamp too.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

REPO="$(resolve_repo "${1:-}")"
init_build_root
[ -n "${IOS3_SDK:-}" ] || die "set IOS3_SDK to the iPhoneOS3.1.2.sdk directory (BUILD.md)"
export IOS3_SDK
if [ -z "${IOS3_TOOLCHAIN:-}" ]; then
  c="$(command -v armv6-ios3-clang || true)"
  [ -n "$c" ] || die "set IOS3_TOOLCHAIN, or put the toolchain's bin/ in PATH"
  IOS3_TOOLCHAIN="$(cd "$(dirname "$c")/.." && pwd)"
fi
TC="$IOS3_TOOLCHAIN"
ARM="$BUILD_ROOT/arm"
ARM_OUT="${ARM_OUT:-$BUILD_ROOT/arm-out}"
TCFILE="$TC/share/armv6-ios3/toolchain.cmake"
STARTFILES="$TC/armv6-ios3/lib/startfiles"
MAPS="$ARM/maps"
ISA_SCAN="$TOOLS_DIR/isa_scan.py"
STARTCHECK="$TOOLS_DIR/startcheck.py"
export PYTHONDONTWRITEBYTECODE=1

case "$BUILD_ROOT$REPO$ARM_OUT" in *[[:space:]]*) die "paths with whitespace are not supported";; esac
[ -f "$IOS3_SDK/usr/lib/libSystem.B.dylib" ] || die "IOS3_SDK=$IOS3_SDK is not the iPhoneOS3.1.2 SDK"
[ -f "$TCFILE" ] || die "toolchain not found at $TC"
[ -d "$STARTFILES" ] || die "the toolchain at $TC is revision 1: it has no armv6-ios3/lib/startfiles, so its links take the start-up objects from the SDK. Install revision $TOOLCHAIN_MIN_REVISION or later (toolchain/README.md)"
for f in crt1.o dylib1.o bundle1.o; do
  [ -f "$STARTFILES/$f" ] || die "the toolchain at $TC is incomplete: $STARTFILES/$f is missing"
done
TC_REV="$(head -n 1 "$TC/VERSION" 2>/dev/null || true)"; TC_REV="${TC_REV:-unknown}"
case "$TC_REV" in
  ''|*[!0-9]*) die "the toolchain at $TC has no VERSION file with a revision number (got '$TC_REV'). Install revision $TOOLCHAIN_MIN_REVISION or later (toolchain/README.md)" ;;
esac
[ "$TC_REV" -ge "$TOOLCHAIN_MIN_REVISION" ] || die "the toolchain at $TC is revision $TC_REV: its start-up objects carry no source-availability notice (the section __TEXT,__csu_notice), so every binary would be built without it. Install revision $TOOLCHAIN_MIN_REVISION or later (toolchain/README.md)"
export PATH="$TC/bin:$PATH"
need_tools cmake ninja git protoc python3 llvm-objdump-18 armv6-ios3-clang armv6-ios3-strip armv6-ios3-ldid armv6-ios3-check
exec 9>"$BUILD_ROOT/.build-arm.lock"
flock -n 9 || die "another build-arm.sh is running"

SECONDS=0
mkdir -p "$ARM" "$LOGS"
note "repo: $(repo_describe "$REPO")"
note "toolchain: $TC (revision $TC_REV)"
STATUS_BEFORE="$(git -C "$REPO" status --short --ignored 2>/dev/null || true)"

# The toolchain a build dir was linked with (see the reproducibility notes): part of the CMake stamps.
TC_STAMP="toolchain=$TC@r$TC_REV:$(cat "$TC/bin/armv6-ios3-clang" "$STARTFILES/crt1.o" "$STARTFILES/dylib1.o" "$STARTFILES/bundle1.o" | sha256sum | cut -c1-16)"
# Every step's links write their ld64 map to $MAPS/<step>/ (the toolchain wrapper, IOS3_LINK_MAP_DIR).
maps_for() { export IOS3_LINK_MAP_DIR="$MAPS/$1"; }

# ------------------------------------------------------------------ CMake builds (C++17 on libc++) ---
# spotify/CSPOT.md + airplay/AIRPLAY.md: IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1 in the environment.
cmake_env() { env IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1 "$@"; }

note "mbedTLS $MBEDTLS_TAG for ARMv6"
maps_for mbedtls
( export IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1; build_mbedtls arm -DCMAKE_TOOLCHAIN_FILE="$TCFILE" )

note "fidelio-audiod + fidelio-lockctl"
maps_for audiod
stamp_dir "$ARM/audiod" "$REPO $TC_STAMP"
[ -f "$ARM/audiod/build.ninja" ] || logged "$LOGS/arm-audiod-configure.log" cmake_env \
  cmake -S "$REPO/audiod" -B "$ARM/audiod" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DCMAKE_BUILD_TYPE=Release
grep -q '^IOS3_LDID:FILEPATH=.*armv6-ios3-ldid$' "$ARM/audiod/CMakeCache.txt" || die "audiod CMake did not find armv6-ios3-ldid: the binaries would not be entitlement-signed"
logged "$LOGS/arm-audiod-build.log" cmake_env ninja -C "$ARM/audiod" fidelio-audiod fidelio-lockctl

note "fidelio-raop"
maps_for airplay
stamp_dir "$ARM/airplay" "$REPO $TC_STAMP"
[ -f "$ARM/airplay/build.ninja" ] || logged "$LOGS/arm-airplay-configure.log" cmake_env \
  cmake -S "$REPO/airplay" -B "$ARM/airplay" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DMBEDTLS_PREFIX="$ARM/sysroot"
logged "$LOGS/arm-airplay-build.log" cmake_env ninja -C "$ARM/airplay"

note "cspot-ios3"
ensure_cspot_tree
ensure_nanopb_python
# A fixed __DATE__ for civetweb's build identifier (see CSPOT_SOURCE_DATE_EPOCH in _common.sh). The
# date is in the stamp, so a build dir made with another date, or with the wall clock, is rebuilt.
cspot_env() { cmake_env env SOURCE_DATE_EPOCH="$CSPOT_SOURCE_DATE_EPOCH" "$@"; }
maps_for cspot
stamp_dir "$ARM/cspot" "$REPO $(cat "$DEPS/cspot.stamp") date=$CSPOT_SOURCE_DATE_EPOCH $TC_STAMP"
[ -f "$ARM/cspot/build.ninja" ] || PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/arm-cspot-configure.log" cspot_env \
  cmake -S "$REPO/spotify/cli-src" -B "$ARM/cspot" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DCMAKE_BUILD_TYPE=Release \
    -DCSPOT_DIR="$DEPS/cspot" -DBELL_EXTERNAL_MBEDTLS="$ARM/sysroot/lib/cmake/MbedTLS" -DMBEDTLS_RELEASE=RELEASE
PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/arm-cspot-build.log" cspot_env ninja -C "$ARM/cspot"

# ------------------------------------------------------------------ the repo's own build.sh scripts ---
# They document only IOS3_SDK + PATH, so the two libc++/compat variables are NOT set for them.
script_env() { env -u IOS3_CXX_RUNTIME -u IOS3_COMPAT_INCLUDE "$@"; }

note "NowPlaying.app"
maps_for nowplaying
logged "$LOGS/arm-nowplaying.log" script_env bash "$REPO/nowplaying/src/build.sh" "$ARM/nowplaying"
note "Fidelio.lockbundle + Fidelio.bundle"
maps_for lockplugin-lockbundle
logged "$LOGS/arm-lockplugin-lockbundle.log" script_env bash "$REPO/nowplaying/lockplugin/build.sh" "$ARM/lockplugin" lockbundle
maps_for lockplugin-bundle
logged "$LOGS/arm-lockplugin-bundle.log" script_env bash "$REPO/nowplaying/lockplugin/build.sh" "$ARM/lockplugin" bundle
note "DockRemote.dylib"
maps_for dockremote
logged "$LOGS/arm-dockremote.log" script_env bash "$REPO/tweak/DockRemote/build.sh" "$ARM/dockremote"
note "voltest (probe)"
maps_for probe
logged "$LOGS/arm-voltest.log" script_env bash "$REPO/audiod/probe/build.sh" "$ARM/probe"
unset IOS3_LINK_MAP_DIR

# ------------------------------------------------------------------ collect ---------------------------
note "collecting into $ARM_OUT"
mkdir -p "$ARM_OUT"
# (fidelio-audiod-openurlswhenlocked: the B1.16.4 entitlement A/B, gone since B1.16.6; removed if an older run left it)
rm -rf "$ARM_OUT"/{fidelio-audiod,fidelio-audiod-openurlswhenlocked,fidelio-lockctl,fidelio-raop,cspot-ios3,NowPlaying.app,Fidelio.lockbundle,Fidelio.bundle,DockRemote.dylib,DockRemote.plist,voltest,verify,report.txt,SHA256SUMS}
mkdir -p "$ARM_OUT/verify"
cp "$ARM/audiod/fidelio-audiod" "$ARM/audiod/fidelio-lockctl" "$ARM_OUT/"      # already stripped + entitlement-signed (POST_BUILD)
for b in "$ARM/airplay/fidelio-raop" "$ARM/cspot/cspot-ios3"; do               # CSPOT.md: strip -x, then ldid -S, then check
  cp "$b" "$ARM_OUT/"; armv6-ios3-strip -x "$ARM_OUT/$(basename "$b")"; armv6-ios3-ldid -S "$ARM_OUT/$(basename "$b")"
done
cp -R "$ARM/nowplaying/NowPlaying.app" "$ARM/lockplugin/Fidelio.lockbundle" "$ARM/lockplugin/Fidelio.bundle" "$ARM_OUT/"
cp "$ARM/dockremote/DockRemote.dylib" "$ARM/dockremote/DockRemote.plist" "$ARM_OUT/"
cp "$ARM/probe/voltest" "$ARM_OUT/"
ENTS="$REPO/common/fidelio-springboard.entitlements.plist"

# ------------------------------------------------------------------ verify ----------------------------
BINARIES="fidelio-audiod fidelio-lockctl fidelio-raop cspot-ios3 NowPlaying.app/NowPlaying Fidelio.lockbundle/Fidelio Fidelio.bundle/Fidelio DockRemote.dylib voltest"
keys_of() { grep -o '<key>[^<]*</key>' "$1" | sed 's/<[^>]*>//g' | sort | tr '\n' ' ' || true; }
want_ents="$(keys_of "$ENTS")"
[ -n "$want_ents" ] || die "no <key> entries in $ENTS"
# The link map of each collected binary: $MAPS/<build step>/<name of the link's -o output>.map
map_of() {
  case "$1" in
    fidelio-audiod|fidelio-lockctl) echo "$MAPS/audiod/$1.map" ;;
    fidelio-raop)                   echo "$MAPS/airplay/$1.map" ;;
    cspot-ios3)                     echo "$MAPS/cspot/$1.map" ;;
    NowPlaying.app/NowPlaying)      echo "$MAPS/nowplaying/NowPlaying.map" ;;
    Fidelio.lockbundle/Fidelio)     echo "$MAPS/lockplugin-lockbundle/Fidelio.map" ;;
    Fidelio.bundle/Fidelio)         echo "$MAPS/lockplugin-bundle/Fidelio.map" ;;
    DockRemote.dylib)               echo "$MAPS/dockremote/$1.map" ;;
    voltest)                        echo "$MAPS/probe/$1.map" ;;
    *)                              echo "$MAPS/no-map-known-for/$1" ;;
  esac
}
# Paths in a map are compared as the linker printed them and with symlinks resolved.
SDK_REAL="$(cd "$IOS3_SDK" && pwd -P)"
STARTFILES_REAL="$(cd "$STARTFILES" && pwd -P)"
bad=0
REPORT="$ARM_OUT/report.txt"
FAILS="$ARM_OUT/verify/link-failures.txt"
: > "$FAILS"

{
  echo "repo:      $(repo_describe "$REPO")"
  echo "toolchain: $TC (revision $TC_REV)   SDK: $IOS3_SDK   host clang: $(clang-18 --version 2>/dev/null | head -1)"
  echo
  printf '%-34s %8s  %-64s  %-18s %8s  %-10s %-9s %-8s %s\n' binary size sha256 check post-v6 link-map start-up notice entitlements
  for b in $BINARIES; do
    f="$ARM_OUT/$b"; tag="$(echo "$b" | tr '/' '_')"
    [ -f "$f" ] || { echo "$b: MISSING"; bad=1; continue; }
    size="$(stat -c %s "$f")"; sha="$(sha256sum < "$f" | cut -d' ' -f1)"
    # armv6-ios3-check. The lock plugin subclasses SpringBoard's private SBAwayViewPluginController, whose
    # class + metaclass symbols are left undefined on purpose (-U): the raw check prints 2 ERRORs and FAIL
    # for it. Like nowplaying/lockplugin/build.sh, accept exactly those and nothing else, but say so.
    crc=0; armv6-ios3-check "$f" > "$ARM_OUT/verify/$tag.check.txt" 2>&1 || crc=$?
    nerr="$(grep -c 'ERROR' "$ARM_OUT/verify/$tag.check.txt" || true)"
    nother="$(grep 'ERROR' "$ARM_OUT/verify/$tag.check.txt" | grep -vc 'SBAwayViewPluginController' || true)"
    if [ "$crc" = 0 ] && grep -q '=> PASS' "$ARM_OUT/verify/$tag.check.txt"; then check="PASS"
    elif [ "$nerr" = 2 ] && [ "$nother" = 0 ] && [[ "$b" == Fidelio.* ]]; then check="FAIL(2 by-design)"
    else check="FAIL($nerr errors)"; bad=1; fi
    # post-ARMv6 instructions: tools/isa_scan.py (decodes as the ARM1176 and as ARMv7; exit 2 = could not scan)
    irc=0; python3 -I "$ISA_SCAN" "$f" > "$ARM_OUT/verify/$tag.isa.txt" 2>&1 || irc=$?
    isa="$(sed -n '1s/.*: \([0-9][0-9]*\) post-ARMv6.*/\1/p' "$ARM_OUT/verify/$tag.isa.txt")"
    [ "$irc" = 2 ] || [ -z "$isa" ] && isa="ERR"
    [ "$isa" = 0 ] || bad=1
    # start-up code and notice: tools/startcheck.py on the binary itself (exit 1 = a check failed, 2 = could not check)
    src=0; python3 -I "$STARTCHECK" "$f" > "$ARM_OUT/verify/$tag.startcheck.txt" 2>&1 || src=$?
    if [ "$src" = 0 ] && grep -q '=> START-UP CODE OK' "$ARM_OUT/verify/$tag.startcheck.txt"; then startup="PASS"
    else
      [ "$src" = 1 ] && startup="FAIL($(grep -c '^  FAIL' "$ARM_OUT/verify/$tag.startcheck.txt" || true))" || startup="ERR"
      bad=1
      grep -E '^  FAIL|CANNOT CHECK|Traceback' "$ARM_OUT/verify/$tag.startcheck.txt" | sed "s|^ *|$b: startcheck: |" >> "$FAILS" || true
    fi
    # the notice: startcheck's "ok   notice:" line names the text it found in __TEXT,__csu_notice. No such
    # line means the section is missing or holds something else (the FAIL line is in the list above).
    notice="$(sed -n 's/^  ok   notice: .* the text for a \(program\|library\): ".*"$/\1/p' "$ARM_OUT/verify/$tag.startcheck.txt" | head -n 1)"
    [ -n "$notice" ] || { notice="FAIL"; bad=1; }
    case "$(sed -n '1s/^.*: \(MH_[A-Z]*\) .*/\1/p' "$ARM_OUT/verify/$tag.startcheck.txt")" in
      MH_EXECUTE) want_start=crt1.o ;; MH_DYLIB) want_start=dylib1.o ;; MH_BUNDLE) want_start=bundle1.o ;; *) want_start="?" ;;
    esac
    # link map: the object-file table of the link that made this binary
    map="$(map_of "$b")"; linkmap="FAIL"
    : > "$ARM_OUT/verify/$tag.linkmap.txt"
    if [ ! -f "$map" ]; then
      echo "$b: link map: none at $map (a build dir from before the maps? remove $ARM and build again)" >> "$FAILS"
    elif [ "$(basename "$(sed -n '1s/^# Path: //p' "$map")")" != "$(basename "$b")" ]; then
      echo "$b: link map: $map belongs to another output ($(head -n 1 "$map"))" >> "$FAILS"
    else
      linkmap_objects "$map" > "$ARM_OUT/verify/$tag.linkmap.txt"
      sdk_objs="$(awk -v a="$IOS3_SDK/" -v r="$SDK_REAL/" '(index($0, a) == 1 || index($0, r) == 1) && /(\.o|\))$/' "$ARM_OUT/verify/$tag.linkmap.txt")"
      starts="$(awk -v a="$STARTFILES/" -v r="$STARTFILES_REAL/" 'index($0, a) == 1 { print substr($0, length(a) + 1); next } index($0, r) == 1 { print substr($0, length(r) + 1) }' "$ARM_OUT/verify/$tag.linkmap.txt")"
      nstarts="$(printf '%s' "$starts" | grep -c . || true)"
      if [ -n "$sdk_objs" ]; then
        printf '%s\n' "$sdk_objs" | sed "s|^|$b: link map: an object of the SDK was linked statically: |" >> "$FAILS"
      fi
      if [ "$nstarts" != 1 ]; then
        echo "$b: link map: $nstarts start-up objects from $STARTFILES (need exactly 1)" >> "$FAILS"
      elif [ "$starts" != "$want_start" ]; then
        echo "$b: link map: start-up object $starts, but the file type needs $want_start" >> "$FAILS"
      elif [ -z "$sdk_objs" ]; then
        linkmap="$starts"
      fi
    fi
    [ "$linkmap" != FAIL ] || bad=1
    ents="-"
    case "$b" in fidelio-audiod|fidelio-lockctl)
      armv6-ios3-ldid -e "$f" > "$ARM_OUT/verify/$tag.entitlements.xml" 2>/dev/null || true
      got="$(keys_of "$ARM_OUT/verify/$tag.entitlements.xml")"
      ents="$(echo $got | wc -w) keys: $(echo $got | sed 's/com\.apple\.springboard\.//g')"
      [ -n "$got" ] && [ "$got" = "$want_ents" ] || { ents="$ents  != expected"; bad=1; } ;;
    esac
    printf '%-34s %8s  %-64s  %-18s %8s  %-10s %-9s %-8s %s\n' "$b" "$size" "$sha" "$check" "$isa" "$linkmap" "$startup" "$notice" "$ents"
    echo "$sha  $b" >> "$ARM_OUT/SHA256SUMS"
  done
  echo
  echo "start-up objects of the toolchain ($STARTFILES) against the sha256 pins in tools/_common.sh:"
  while read -r name want; do
    [ -n "$name" ] || continue
    if [ -f "$STARTFILES/$name" ]; then
      got="$(sha256sum < "$STARTFILES/$name" | cut -d' ' -f1)"; osize="$(stat -c %s "$STARTFILES/$name")"
    else
      got="(missing)"; osize="-"
    fi
    if [ "$got" = "$want" ]; then pin="OK"; else pin="MISMATCH: the pin is $want"; bad=1; fi
    printf '  %-10s %6s  %-64s  %s\n' "$name" "$osize" "$got" "$pin"
  done <<< "$STARTFILES_SHA256"
  if [ -s "$FAILS" ]; then
    echo
    echo "link-map and start-up failures:"
    sed 's/^/  /' "$FAILS"
  fi
  echo
  echo "check   = armv6-ios3-check (raw output: $ARM_OUT/verify/*.check.txt)."
  echo "  FAIL(2 by-design): only _OBJC_CLASS_\$_/_OBJC_METACLASS_\$_SBAwayViewPluginController are undefined (linked -U on"
  echo "  purpose, SpringBoard provides them); the repo's lockplugin/build.sh accepts exactly this."
  echo "post-v6 = tools/isa_scan.py: words only an ARMv7 decoder accepts, literal-pool data excluded (details: verify/*.isa.txt)"
  echo "link-map = the ld64 map of the link that made the binary ($MAPS/<step>/<output>.map; its object table: verify/*.linkmap.txt):"
  echo "  no .o file and no archive member under \$IOS3_SDK was linked, and exactly one start-up object, from the toolchain's"
  echo "  startfiles directory (shown: crt1.o for an executable, dylib1.o for a dylib, bundle1.o for a bundle)."
  echo "start-up = tools/startcheck.py: start, dyld_stub_binding_helper and __dyld_func_lookup inside the binary, their linked"
  echo "  literals and the __DATA,__dyld section, and the notice (details: verify/*.startcheck.txt)"
  echo "notice  = tools/startcheck.py: the section __TEXT,__csu_notice of the binary holds, byte for byte, the source-availability"
  echo "  notice of its start-up object (toolchain/startfiles/notice.s): \"program\" = the text of crt1.o, required in an executable;"
  echo "  \"library\" = the text of dylib1.o and bundle1.o, required in a dylib and in a bundle. FAIL = no such section, or another text."
  echo "  The texts as read from the binaries above (verify/*.startcheck.txt):"
  for k in program library; do
    n="$( { grep -l "^  ok   notice: .* the text for a $k: \"" "$ARM_OUT"/verify/*.startcheck.txt 2>/dev/null || true; } | wc -l)"
    t="$( { sed -n "s/^  ok   notice: .* the text for a $k: \"\(.*\)\"\$/\1/p" "$ARM_OUT"/verify/*.startcheck.txt 2>/dev/null || true; } | sort -u)"
    echo "  $k, in $n of them: ${t:-(in none)}"
  done
  echo
  STATUS_AFTER="$(git -C "$REPO" status --short --ignored 2>/dev/null || true)"
  echo "repo after the build: $(git -C "$REPO" status --short 2>/dev/null | wc -l) entries in 'git status --short'$( [ "$STATUS_BEFORE" = "$STATUS_AFTER" ] || echo '  ** git status (incl. ignored) CHANGED during the build **' )"
  echo "time: ${SECONDS}s   build logs: $LOGS/arm-*.log   result: $( [ "$bad" = 0 ] && echo OK || echo 'VERIFICATION FAILED' )"
  echo "$bad" > "$ARM_OUT/verify/.bad"
} 2>&1 | tee "$REPORT"

[ "$(cat "$ARM_OUT/verify/.bad" 2>/dev/null || echo 1)" = 0 ] || exit 1
exit 0
