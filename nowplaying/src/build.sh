#!/usr/bin/env bash
# Build NowPlaying.app for iPhone OS 3.1 (armv6) with the armv6-ios3 toolchain.
#   IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH ./build.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-build}"
APP="$OUT/NowPlaying.app"
rm -rf "$APP" "$OUT"/*.o   # only our own products: never rm -rf a caller-supplied directory
mkdir -p "$APP"

CFLAGS="-O2 -Wall -Wno-unused -fno-objc-arc -fobjc-exceptions -x objective-c"
for f in main FidelioAppDelegate FidelioStatus NowPlayingViewController TechnicalViewController SettingsViewController EnumPickerViewController StringEditorViewController NPEventProbe NPRemoteControl; do
  armv6-ios3-clang $CFLAGS -c "$f.m" -o "$OUT/$f.o"
done
armv6-ios3-clang -O2 -Wall -c NPLayout.c -o "$OUT/NPLayout.o"   # B1.18: the layout core (plain C, also host-tested)
armv6-ios3-clang "$OUT"/*.o -o "$APP/NowPlaying" \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore \
  -framework AVFoundation -framework AudioToolbox   # B1.16.9: stage 3's silent mixable player
armv6-ios3-strip -x "$APP/NowPlaying"
armv6-ios3-ldid -S "$APP/NowPlaying"
armv6-ios3-check "$APP/NowPlaying"

cp Info.plist "$APP/"
printf 'APPL????' > "$APP/PkgInfo"
python3 gen_images.py "$APP"
# B1.16.1: every tab needs its glyph in the bundle, or the tab shows blank. Fail the build if one is missing.
for g in tab_nowplaying tab_technical tab_settings Icon Icon-72 placeholder placeholder_600 Default Default-Portrait~ipad Default-Landscape~ipad; do
  [ -f "$APP/$g.png" ] || { echo "BUILD ERROR: $APP/$g.png missing (gen_images.py must produce it)"; exit 1; }
done
ls -l "$APP"
