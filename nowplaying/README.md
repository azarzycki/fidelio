# NowPlaying (the dock display)

**NowPlaying — a native now-playing dock display for the original iPhone (iOS 3.1.3, ARMv6) and the iPad 1.** A small UIKit app that shows what the Spotify Connect receiver ([`../spotify/`](../spotify/)) or the AirPlay receiver ([`../airplay/`](../airplay/)) is playing: album art, track/artist/album, a progress bar, and a second tab of live technical telemetry. It's a *display* — playback is driven from any Spotify client; the app just reads a status file the receiver writes.

Built with the same ARMv6 / iPhone-OS-3.1 cross-toolchain (Objective-C, manual retain/release — no ARC, no blocks), links UIKit / Foundation / CoreGraphics / QuartzCore, fake-signed with `ldid -S`. Runs at ~2 MB private resident.

## Screens
- **Now Playing** — 256 px album cover (or a drawn placeholder), bold title, artist, album, a progress bar with elapsed / −remaining time, and a state line. Black background, status bar hidden, idle-timer disabled so the dock screen never sleeps.
- **Technical** — a live table: state, codec + bitrate, negotiated TLS suite, Spotify access point + country, the receiver's RSS, the app's RSS, device free memory, uptime, Wi-Fi IP, battery, status-file age, device name, build.

## What's here
- `src/` — the full sources: `main.m`, `NPAppDelegate.{h,m}`, `NowPlayingViewController.{h,m}`, `TechnicalViewController.{h,m}`, `NPStatus.{h,m}`, `Info.plist`, `gen_images.py` (draws Icon/placeholder/Default in pure Python, no PIL), and `build.sh`.
- `APP.md` — the status-file contract it reads, the install steps, and the cover-art implementation notes.

## Data contract
Polls `/var/mobile/dockreceiver/status.plist` (XML plist — iOS 3 has no JSON parser) once per second, and reloads `/var/mobile/dockreceiver/cover.jpg` when it changes. The companion receiver writes both `status.json` and `status.plist` atomically. Keys: `state, title, artist, album, duration_ms, position_ms, bitrate_kbps, codec, cover, device, ap, country, tls, rss_kb, updated` (and `source` once the AirPlay input lands). Full table in `APP.md`.

## Build
```sh
# IOS3_SDK: a sysroot (toolchain/sysroot/build-sysroot.sh, or ../build/sysroot/root after tools/build-arm.sh), or Apple's iPhoneOS3.1.2.sdk
export IOS3_SDK=/path/to/sysroot PATH=<toolchain>/bin:$PATH
./src/build.sh      # -> build/NowPlaying.app  (compile, link, strip, ldid -S, armv6-ios3-check, images)
```

## Install
```sh
scp -r NowPlaying.app root@phone:/Applications/
ssh root@phone 'chown -R mobile:mobile /Applications/NowPlaying.app; chmod +x /Applications/NowPlaying.app/NowPlaying; killall SpringBoard'
```
If the icon doesn't appear after the respring, run `uicache` and respring again.

## License
Original work, MIT — see [`LICENSE`](LICENSE). Part of the dock-receiver monorepo (see the top-level [`README.md`](../README.md)).
