#!/usr/bin/env bash
# Build DockRemote.dylib (ARMv6 MH_DYLIB, iPhone OS 3.1.3 phone only). MobileSubstrate's MSHookFunction
# and GraphicsServices' GSSendEvent are resolved at run time (dlsym), so every LINKED symbol must
# resolve against the SDK: no -undefined dynamic_lookup, and armv6-ios3-check must pass.
#   IOS3_SDK=... PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"; mkdir -p "$OUT"
REPO="$(cd ../.. && pwd)"
armv6-ios3-clang++ -std=c++17 -stdlib=libc++ -O2 -Wall -fno-objc-arc -fobjc-exceptions -I"$REPO" \
  -dynamiclib -o "$OUT/DockRemote.dylib" DockRemoteHook.mm DockRemoteCore.cpp \
  -framework Foundation
armv6-ios3-strip -x "$OUT/DockRemote.dylib"
armv6-ios3-ldid -S "$OUT/DockRemote.dylib"
armv6-ios3-check "$OUT/DockRemote.dylib"
cp DockRemote.plist "$OUT/"
ls -l "$OUT"
