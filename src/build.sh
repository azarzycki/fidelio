#!/usr/bin/env bash
# Build NowPlaying.app for iPhone OS 3.1 (armv6) with the armv6-ios3 toolchain.
#   IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"
APP="$OUT/NowPlaying.app"
rm -rf "$OUT"
mkdir -p "$APP"

CFLAGS="-O2 -Wall -Wno-unused -fno-objc-arc -fobjc-exceptions -x objective-c"
for f in main FidelioAppDelegate FidelioStatus NowPlayingViewController TechnicalViewController; do
  armv6-ios3-clang $CFLAGS -c "$f.m" -o "$OUT/$f.o"
done
armv6-ios3-clang "$OUT"/*.o -o "$APP/NowPlaying" \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore
armv6-ios3-strip -x "$APP/NowPlaying"
armv6-ios3-ldid -S "$APP/NowPlaying"
armv6-ios3-check "$APP/NowPlaying"

cp Info.plist "$APP/"
printf 'APPL????' > "$APP/PkgInfo"
python3 gen_images.py "$APP"
ls -l "$APP"
