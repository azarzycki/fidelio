#!/usr/bin/env bash
# tools/build-arm.sh [REPO]   (REPO defaults to the repo this script lives in)
#
# Cross-builds every ARMv6 / iPhone OS 3 artifact of the monorepo with the armv6-ios3 toolchain,
# collects them in ARM_OUT (default $BUILD_ROOT/arm-out) and verifies each binary:
#   size, sha256, armv6-ios3-check, tools/isa_scan.py (post-ARMv6 instructions), and for the entitled
#   binaries the entitlement keys (armv6-ios3-ldid -e) against common/fidelio-springboard.entitlements.plist.
#
#   fidelio-audiod, fidelio-lockctl  audiod/CMakeLists.txt + toolchain file (POST_BUILD strips and entitlement-signs)
#   fidelio-audiod-openurlswhenlocked  B1.16.4 device A/B: the same fidelio-audiod, re-signed with the plist's keys
#                                    plus com.apple.springboard.openurlswhenlocked (remove once the device test
#                                    shows whether unlock-on-play needs it)
#   fidelio-raop                     airplay/CMakeLists.txt, then strip -x + ldid -S
#   cspot-ios3                       spotify/cli-src against deps/cspot (upstream + repo patch), then strip -x + ldid -S
#   NowPlaying.app                   nowplaying/src/build.sh
#   Fidelio.lockbundle, Fidelio.bundle   nowplaying/lockplugin/build.sh (EXT lockbundle and bundle)
#   DockRemote.dylib                 tweak/DockRemote/build.sh
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
ISA_SCAN="$TOOLS_DIR/isa_scan.py"
ALT_KEY="com.apple.springboard.openurlswhenlocked"
export PYTHONDONTWRITEBYTECODE=1

case "$BUILD_ROOT$REPO$ARM_OUT" in *[[:space:]]*) die "paths with whitespace are not supported";; esac
[ -f "$IOS3_SDK/usr/lib/libSystem.B.dylib" ] || die "IOS3_SDK=$IOS3_SDK is not the iPhoneOS3.1.2 SDK"
[ -f "$TCFILE" ] || die "toolchain not found at $TC"
export PATH="$TC/bin:$PATH"
need_tools cmake ninja git protoc python3 llvm-objdump-18 armv6-ios3-clang armv6-ios3-strip armv6-ios3-ldid armv6-ios3-check
exec 9>"$BUILD_ROOT/.build-arm.lock"
flock -n 9 || die "another build-arm.sh is running"

SECONDS=0
mkdir -p "$ARM" "$LOGS"
note "repo: $(repo_describe "$REPO")"
STATUS_BEFORE="$(git -C "$REPO" status --short --ignored 2>/dev/null || true)"

# ------------------------------------------------------------------ CMake builds (C++17 on libc++) ---
# spotify/CSPOT.md + airplay/AIRPLAY.md: IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1 in the environment.
cmake_env() { env IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1 "$@"; }

note "mbedTLS $MBEDTLS_TAG for ARMv6"
( export IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1; build_mbedtls arm -DCMAKE_TOOLCHAIN_FILE="$TCFILE" )

note "fidelio-audiod + fidelio-lockctl"
stamp_dir "$ARM/audiod" "$REPO"
[ -f "$ARM/audiod/build.ninja" ] || logged "$LOGS/arm-audiod-configure.log" cmake_env \
  cmake -S "$REPO/audiod" -B "$ARM/audiod" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DCMAKE_BUILD_TYPE=Release
grep -q '^IOS3_LDID:FILEPATH=.*armv6-ios3-ldid$' "$ARM/audiod/CMakeCache.txt" || die "audiod CMake did not find armv6-ios3-ldid: the binaries would not be entitlement-signed"
logged "$LOGS/arm-audiod-build.log" cmake_env ninja -C "$ARM/audiod" fidelio-audiod fidelio-lockctl

note "fidelio-raop"
stamp_dir "$ARM/airplay" "$REPO"
[ -f "$ARM/airplay/build.ninja" ] || logged "$LOGS/arm-airplay-configure.log" cmake_env \
  cmake -S "$REPO/airplay" -B "$ARM/airplay" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DMBEDTLS_PREFIX="$ARM/sysroot"
logged "$LOGS/arm-airplay-build.log" cmake_env ninja -C "$ARM/airplay"

note "cspot-ios3"
ensure_cspot_tree
ensure_nanopb_python
stamp_dir "$ARM/cspot" "$REPO $(cat "$DEPS/cspot.stamp")"
[ -f "$ARM/cspot/build.ninja" ] || PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/arm-cspot-configure.log" cmake_env \
  cmake -S "$REPO/spotify/cli-src" -B "$ARM/cspot" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCFILE" -DCMAKE_BUILD_TYPE=Release \
    -DCSPOT_DIR="$DEPS/cspot" -DBELL_EXTERNAL_MBEDTLS="$ARM/sysroot/lib/cmake/MbedTLS" -DMBEDTLS_RELEASE=RELEASE
PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/arm-cspot-build.log" cmake_env ninja -C "$ARM/cspot"

# ------------------------------------------------------------------ the repo's own build.sh scripts ---
# They document only IOS3_SDK + PATH, so the two libc++/compat variables are NOT set for them.
script_env() { env -u IOS3_CXX_RUNTIME -u IOS3_COMPAT_INCLUDE "$@"; }

note "NowPlaying.app"
logged "$LOGS/arm-nowplaying.log" script_env bash "$REPO/nowplaying/src/build.sh" "$ARM/nowplaying"
note "Fidelio.lockbundle + Fidelio.bundle"
logged "$LOGS/arm-lockplugin-lockbundle.log" script_env bash "$REPO/nowplaying/lockplugin/build.sh" "$ARM/lockplugin" lockbundle
logged "$LOGS/arm-lockplugin-bundle.log" script_env bash "$REPO/nowplaying/lockplugin/build.sh" "$ARM/lockplugin" bundle
note "DockRemote.dylib"
logged "$LOGS/arm-dockremote.log" script_env bash "$REPO/tweak/DockRemote/build.sh" "$ARM/dockremote"

# ------------------------------------------------------------------ collect ---------------------------
note "collecting into $ARM_OUT"
mkdir -p "$ARM_OUT"
rm -rf "$ARM_OUT"/{fidelio-audiod,fidelio-audiod-openurlswhenlocked,fidelio-lockctl,fidelio-raop,cspot-ios3,NowPlaying.app,Fidelio.lockbundle,Fidelio.bundle,DockRemote.dylib,DockRemote.plist,verify,report.txt,SHA256SUMS}
mkdir -p "$ARM_OUT/verify"
cp "$ARM/audiod/fidelio-audiod" "$ARM/audiod/fidelio-lockctl" "$ARM_OUT/"      # already stripped + entitlement-signed (POST_BUILD)
for b in "$ARM/airplay/fidelio-raop" "$ARM/cspot/cspot-ios3"; do               # CSPOT.md: strip -x, then ldid -S, then check
  cp "$b" "$ARM_OUT/"; armv6-ios3-strip -x "$ARM_OUT/$(basename "$b")"; armv6-ios3-ldid -S "$ARM_OUT/$(basename "$b")"
done
cp -R "$ARM/nowplaying/NowPlaying.app" "$ARM/lockplugin/Fidelio.lockbundle" "$ARM/lockplugin/Fidelio.bundle" "$ARM_OUT/"
cp "$ARM/dockremote/DockRemote.dylib" "$ARM/dockremote/DockRemote.plist" "$ARM_OUT/"
# B1.16.4 device A/B: the repo plist's keys + openurlswhenlocked (the device proof of the unlock carried it).
ENTS="$REPO/common/fidelio-springboard.entitlements.plist"
ALT_ENTS="$ARM_OUT/verify/openurlswhenlocked.entitlements.plist"
grep -q "<key>$ALT_KEY</key>" "$ENTS" && die "$ENTS already has $ALT_KEY: drop the A/B variant from this script"
sed "s|^</dict>|	<key>$ALT_KEY</key>\n	<true/>\n</dict>|" "$ENTS" > "$ALT_ENTS"
cp "$ARM_OUT/fidelio-audiod" "$ARM_OUT/fidelio-audiod-openurlswhenlocked"
armv6-ios3-ldid -S"$ALT_ENTS" "$ARM_OUT/fidelio-audiod-openurlswhenlocked"

# ------------------------------------------------------------------ verify ----------------------------
BINARIES="fidelio-audiod fidelio-audiod-openurlswhenlocked fidelio-lockctl fidelio-raop cspot-ios3 NowPlaying.app/NowPlaying Fidelio.lockbundle/Fidelio Fidelio.bundle/Fidelio DockRemote.dylib"
keys_of() { grep -o '<key>[^<]*</key>' "$1" | sed 's/<[^>]*>//g' | sort | tr '\n' ' ' || true; }
want_ents="$(keys_of "$ENTS")"
want_alt="$(keys_of "$ALT_ENTS")"
[ -n "$want_ents" ] || die "no <key> entries in $ENTS"
bad=0
REPORT="$ARM_OUT/report.txt"

{
  echo "repo:      $(repo_describe "$REPO")"
  echo "toolchain: $TC   SDK: $IOS3_SDK   host clang: $(clang-18 --version 2>/dev/null | head -1)"
  echo
  printf '%-34s %8s  %-64s  %-18s %8s  %s\n' binary size sha256 check post-v6 entitlements
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
    ents="-"
    case "$b" in fidelio-audiod|fidelio-lockctl|fidelio-audiod-openurlswhenlocked)
      armv6-ios3-ldid -e "$f" > "$ARM_OUT/verify/$tag.entitlements.xml" 2>/dev/null || true
      got="$(keys_of "$ARM_OUT/verify/$tag.entitlements.xml")"
      want="$want_ents"; [ "$b" = fidelio-audiod-openurlswhenlocked ] && want="$want_alt"
      ents="$(echo $got | wc -w) keys: $(echo $got | sed 's/com\.apple\.springboard\.//g')"
      [ -n "$got" ] && [ "$got" = "$want" ] || { ents="$ents  != expected"; bad=1; } ;;
    esac
    printf '%-34s %8s  %-64s  %-18s %8s  %s\n' "$b" "$size" "$sha" "$check" "$isa" "$ents"
    echo "$sha  $b" >> "$ARM_OUT/SHA256SUMS"
  done
  echo
  echo "check   = armv6-ios3-check (raw output: $ARM_OUT/verify/*.check.txt)."
  echo "  FAIL(2 by-design): only _OBJC_CLASS_\$_/_OBJC_METACLASS_\$_SBAwayViewPluginController are undefined (linked -U on"
  echo "  purpose, SpringBoard provides them); the repo's lockplugin/build.sh accepts exactly this."
  echo "post-v6 = tools/isa_scan.py: words only an ARMv7 decoder accepts, literal-pool data excluded (details: verify/*.isa.txt)"
  echo
  STATUS_AFTER="$(git -C "$REPO" status --short --ignored 2>/dev/null || true)"
  echo "repo after the build: $(git -C "$REPO" status --short 2>/dev/null | wc -l) entries in 'git status --short'$( [ "$STATUS_BEFORE" = "$STATUS_AFTER" ] || echo '  ** git status (incl. ignored) CHANGED during the build **' )"
  echo "time: ${SECONDS}s   build logs: $LOGS/arm-*.log   result: $( [ "$bad" = 0 ] && echo OK || echo 'VERIFICATION FAILED' )"
  echo "$bad" > "$ARM_OUT/verify/.bad"
} 2>&1 | tee "$REPORT"

[ "$(cat "$ARM_OUT/verify/.bad" 2>/dev/null || echo 1)" = 0 ] || exit 1
exit 0
