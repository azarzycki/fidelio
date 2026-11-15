#!/usr/bin/env bash
# Build voltest (B1.16.12 diagnosis): an ARMv6 command-line probe of the system volume categories.
# One binary for both devices (the iPad's A4 runs armv6 code). No entitlements needed.
#   IOS3_SDK=... PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"; mkdir -p "$OUT"
# Compile and link separately: "-x objective-c" on a link line would also apply to the library the
# toolchain wrapper appends there.
armv6-ios3-clang -O2 -Wall -fno-objc-arc -fobjc-exceptions -x objective-c -c voltest.m -o "$OUT/voltest.o"
armv6-ios3-clang "$OUT/voltest.o" -o "$OUT/voltest" -framework Foundation
armv6-ios3-strip -x "$OUT/voltest"
armv6-ios3-ldid -S "$OUT/voltest"
armv6-ios3-check "$OUT/voltest"
ls -l "$OUT/voltest"
