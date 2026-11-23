#!/usr/bin/env bash
# Build the SpringBoard lock-screen plugin (an MH_BUNDLE, ARMv6, iPhone OS 3.1).
#   IOS3_SDK=<sysroot> PATH=<toolchain>/bin:$PATH ./build.sh [outdir] [ext]
# The sysroot: one made by toolchain/sysroot/build-sysroot.sh, or Apple's iPhoneOS3.1.2.sdk (tools/build-arm.sh).
#   ext = lockbundle (iOS 5.1.1 iPad, default) or bundle (iPhone OS 3.1.3).
# The principal class subclasses SpringBoard's private SBAwayViewPluginController; only that class's
# two ObjC symbols are left undefined (-U), so every OTHER symbol is still validated against the sysroot's
# libraries at link time and SpringBoard resolves the base class when it loads the bundle.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"
# B1.16.3: the bundle extension differs by OS — .lockbundle on iOS 5.1.1, .bundle on iPhone OS 3.1.3;
# both go in /System/Library/SpringBoardPlugins and are enabled by the base name "NowPlaying" (SpringBoard
# appends the extension). Same binary + Info.plist for both. Pass the extension as $2 (default lockbundle).
EXT="${2:-lockbundle}"
BND="$OUT/NowPlaying.$EXT"
rm -rf "$BND" "$OUT"/*.o
mkdir -p "$BND"
SUP1='_OBJC_CLASS_$_SBAwayViewPluginController'
SUP2='_OBJC_METACLASS_$_SBAwayViewPluginController'

# -Werror=objc-method-access: a message whose method no header declares stops the build. As a warning it
# compiles, with the result taken to be id and the arguments promoted: wrong code (toolchain/sysroot/README.md).
CFLAGS="-O2 -Wall -Wno-unused -Werror=objc-method-access -fno-objc-arc -fobjc-exceptions -I../.. -x objective-c"   # -I../..: common/DataDir.h
armv6-ios3-clang $CFLAGS -c NowPlayingLockPluginController.m -o "$OUT/NowPlayingLockPluginController.o"
# B1.16.7: the layout core is plain C, shared with the host test (nowplaying/hosttest/lock_layout_test.c).
armv6-ios3-clang -O2 -Wall -std=c99 -c LPLayout.c -o "$OUT/LPLayout.o"
armv6-ios3-clang -bundle -Wl,-U,"$SUP1" -Wl,-U,"$SUP2" "$OUT/NowPlayingLockPluginController.o" "$OUT/LPLayout.o" -o "$BND/NowPlaying" \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore
armv6-ios3-strip -x "$BND/NowPlaying"
armv6-ios3-ldid -S "$BND/NowPlaying"

# Verify: armv6-ios3-check, but the two SBAwayViewPluginController symbols are undefined on purpose
# (SpringBoard provides the base class at load); tolerate only those, fail on anything else.
if ! armv6-ios3-check "$BND/NowPlaying" > "$OUT/check.txt" 2>&1; then
  if grep ERROR "$OUT/check.txt" | grep -qv "SBAwayViewPluginController"; then
    cat "$OUT/check.txt"; echo "FAIL: unexpected link errors"; exit 1
  fi
fi
# ARMv6 ISA baseline: no instruction the ARM1176 cannot run. tools/isa_scan.py decodes every word as the
# device CPU and as ARMv7 (a grep of an armv6 disassembly cannot see movw/udiv/...: they print as <unknown>).
python3 -I ../../tools/isa_scan.py "$BND/NowPlaying" > "$OUT/isa.txt" 2>&1 || { cat "$OUT/isa.txt"; echo "FAIL: post-ARMv6 instructions (or the scan failed)"; exit 1; }

cp Info.plist "$BND/"
echo "built $BND (MH_BUNDLE armv6, fake-signed, base class resolved at load); undefined-at-link: $SUP1 (+metaclass)"
ls -l "$BND"
