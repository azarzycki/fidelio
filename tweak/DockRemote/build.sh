#!/usr/bin/env bash
# Build DockRemote.dylib (ARMv6 MH_DYLIB, iPhone OS 3.1). MobileSubstrate is resolved at run time.
#   IOS3_SDK=... PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
# NOTE: the hook is not wired yet (see README); this proves the dylib builds and links.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"; mkdir -p "$OUT"
REPO="$(cd ../.. && pwd)"
armv6-ios3-clang++ -std=c++17 -stdlib=libc++ -O2 -Wall -fno-objc-arc -fobjc-exceptions -I"$REPO" \
  -dynamiclib -o "$OUT/DockRemote.dylib" DockRemoteHook.mm DockRemoteCore.cpp \
  -framework Foundation -Wl,-undefined,dynamic_lookup
armv6-ios3-strip -x "$OUT/DockRemote.dylib"
armv6-ios3-ldid -S "$OUT/DockRemote.dylib"
armv6-ios3-check "$OUT/DockRemote.dylib" || true
cp DockRemote.plist "$OUT/"
ls -l "$OUT"
