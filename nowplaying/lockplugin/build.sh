#!/usr/bin/env bash
# Build Fidelio.lockbundle (an MH_BUNDLE, ARMv6, iPhone OS 3.1.2 SDK) for SpringBoard on iOS 5.1.1.
#   IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
# The principal class subclasses SpringBoard's private SBAwayViewPluginController; only that class's
# two ObjC symbols are left undefined (-U), so every OTHER symbol is still validated against the SDK
# dylibs at link time and SpringBoard resolves the base class when it loads the bundle.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"
BND="$OUT/Fidelio.lockbundle"
rm -rf "$BND" "$OUT"/*.o
mkdir -p "$BND"
SUP1='_OBJC_CLASS_$_SBAwayViewPluginController'
SUP2='_OBJC_METACLASS_$_SBAwayViewPluginController'

CFLAGS="-O2 -Wall -Wno-unused -fno-objc-arc -fobjc-exceptions -x objective-c"
armv6-ios3-clang $CFLAGS -c FidelioLockPluginController.m -o "$OUT/FidelioLockPluginController.o"
armv6-ios3-clang -bundle -Wl,-U,"$SUP1" -Wl,-U,"$SUP2" "$OUT/FidelioLockPluginController.o" -o "$BND/Fidelio" \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore
armv6-ios3-strip -x "$BND/Fidelio"
armv6-ios3-ldid -S "$BND/Fidelio"

# Verify: armv6-ios3-check, but the two SBAwayViewPluginController symbols are undefined on purpose
# (SpringBoard provides the base class at load); tolerate only those, fail on anything else.
if ! armv6-ios3-check "$BND/Fidelio" > "$OUT/check.txt" 2>&1; then
  if grep ERROR "$OUT/check.txt" | grep -qv "SBAwayViewPluginController"; then
    cat "$OUT/check.txt"; echo "FAIL: unexpected link errors"; exit 1
  fi
fi
# ARMv6 ISA baseline (only clz beyond ARMv5 is accepted, as for every Fidelio binary)
ISA=$(llvm-objdump-18 --triple=armv6-apple-darwin -d "$BND/Fidelio" 2>/dev/null \
  | grep -Eo '\b(movw|movt|udiv|sdiv|dmb|dsb|isb|rbit|sbfx|ubfx|bfi|bfc|pld|cbz|cbnz|tbb|tbh)\b' | sort -u || true)
[ -z "$ISA" ] || { echo "FAIL: post-ARMv6 instructions: $ISA"; exit 1; }

cp Info.plist "$BND/"
echo "built $BND (MH_BUNDLE armv6, fake-signed, base class resolved at load); undefined-at-link: $SUP1 (+metaclass)"
ls -l "$BND"
