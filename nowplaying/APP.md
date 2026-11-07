# NowPlaying.app ("Fidelio") — dock display for cspot-ios3

A native UIKit app for iPhone OS 3.1 (armv6) that shows what cspot-ios3 is playing. It is a display only: playback is driven from the Spotify app on your phone/laptop. Built with the armv6-ios3 toolchain (Objective-C, manual retain/release, no ARC, no blocks), links UIKit, Foundation, CoreGraphics, QuartzCore (+ CoreFoundation, libobjc, libSystem, libgcc_s pulled in by them). Fake-signed with `ldid -S`. `armv6-ios3-check`: PASS, 52 undefined symbols, all in the 3.1.2 SDK.

**Not run on hardware by me.** What is verified: it compiles and links, the Mach-O and Objective-C metadata are the 2009 shapes (non-fragile ABI sections, `__objc_classlist`, no PIE, no dyld-info), and the Info.plist keys are the iOS 3.1 ones.

## What the app reads
`/var/mobile/fidelio/status.plist` — polled once per second with `[NSDictionary dictionaryWithContentsOfFile:]`. iOS 3 has no JSON parser in Foundation (NSJSONSerialization is iOS 5), so cspot-ios3 rev 4 writes **both** `status.json` (your contract, for scripts) and `status.plist` (XML plist, same keys/values) atomically, each as `.tmp` + `rename()`. The app reads only the plist. A partial read (file missing for a moment) keeps the last good dictionary, so the screen never flickers.

`/var/mobile/fidelio/cover.jpg` — reloaded when the `cover` field or the file's modification date changes.

## Field contract (both files, identical keys; never omitted, "" or 0 when unknown)
| key | type | source |
|---|---|---|
| `state` | string | `idle` (started, no login yet) · `connected` (authenticated, nothing playing) · `playing` · `paused` · `stopped` (Spotify disconnected, or the queue ran out) |
| `title`, `artist`, `album` | UTF-8 string | Spirc `TRACK_INFO` (sent when a track's audio actually reaches the sink) |
| `duration_ms` | int | `TRACK_INFO` |
| `position_ms` | int | Spirc position (PLAYBACK_START / SEEK / PLAY_PAUSE) + cspot's own `mach_absolute_time` clock while playing; frozen while paused; reset on track change. Written every second while playing. The app additionally advances it by the time since it read the file, so it moves smoothly. Accuracy: the AudioQueue sink holds ~0.8 s of audio, so the displayed position runs up to ~1 s ahead of the speaker. |
| `bitrate_kbps` | int | the `-b` value (96 default) |
| `codec` | string | always `OGG/Vorbis` |
| `cover` | string | `cover.jpg` once the album image has been fetched, `""` otherwise |
| `device` | string | the `-n` name |
| `ap` | string | `host:port` of the Spotify access point in use (`Session::lastApAddress`) |
| `country` | string | the 2-letter country the Mercury session reports (arrives a moment after connect) |
| `tls` | string | version and cipher suite of the most recent successful mbedTLS handshake in the cspot process (apresolve / token / CDN / cover fetch), e.g. `TLSv1.3 TLS1-3-CHACHA20-POLY1305-SHA256`; `""` before the first one |
| `rss_kb` | int | cspot's resident size (`task_info TASK_BASIC_INFO`) |
| `updated` | int | Unix seconds when the file was written |

Write triggers: every Spirc event, state changes, and once per second while `playing`. Options: `--status-dir DIR` (default `/var/mobile/fidelio`), `--no-status`.

## Cover art — verdict: feasible, implemented
The fork's `TrackInfo.imageUrl` is already `https://i.scdn.co/image/<hex file id>` built from `album.cover_group.image[0]` (the proto keeps only `file_id`, no size enum; in practice `image[0]` is Spotify's DEFAULT ≈300 px image). cspot-ios3 fetches it with bell's own `HTTPClient::get` (HTTPS via mbedTLS, same path as the CDN audio) on the status writer's thread, writes `cover.jpg.tmp`, renames it to `cover.jpg`, then sets `"cover":"cover.jpg"`. On any failure the field stays `""` and the app shows the placeholder. The fetch is one extra TLS connection per track change (~40 KB heap while it runs). Unverified on hardware: whether `image[0]` is really the 300 px variant for every album; the app scales whatever it gets with aspect-fill into a 256 px rounded square.

## Screens
- **Now Playing**: 256 px cover (rounded 10 px) or the placeholder tile, title (bold 26), artist (20), album (16, dimmed), progress bar with `m:ss` elapsed / `-m:ss` remaining, status line (Playing / Paused / Stopped / Connected - waiting for Spotify / Waiting for cspot (no status file); "(stale)" appended when the status file is older than 15 s). Black background, status bar hidden, idle timer disabled so the dock screen does not lock.
- **Technical**: plain black table, refreshed each second: state, codec+bitrate, TLS suite, access point, country, cspot RSS, app RSS (`task_info`), device free memory (`host_statistics` free pages), uptime (`KERN_BOOTTIME`), Wi-Fi IP (`getifaddrs` en0), battery (`UIDevice`, monitoring enabled), status-file age, device name, build.

Memory: the app allocates the two view hierarchies, one decoded cover (≈300 KB for a 300 px JPEG) and a 14-row table. No caches, no large buffers.

## Install
```sh
scp -r NowPlaying.app root@phone:/Applications/
ssh root@phone 'chown -R mobile:mobile /Applications/NowPlaying.app; chmod +x /Applications/NowPlaying.app/NowPlaying; killall SpringBoard'
```
If the icon does not appear after the respring: `uicache` (Cydia's) or `su mobile -c uicache`, then respring again. The binary is already fake-signed; re-sign with `ldid -S NowPlaying.app/NowPlaying` only if you rebuild it. The app needs no entitlements; it only reads `/var/mobile/fidelio` (owned by `mobile`, same user it runs as).

## Build
```sh
export IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH
./build.sh            # -> build/NowPlaying.app (compiles, links, strips, ldid -S, armv6-ios3-check, images)
```
`gen_images.py` draws `Icon.png` (57×57), `placeholder.png` (256×256) and `Default.png` (320×480) in pure Python; no PIL needed.

## Things to watch on the device
- If SpringBoard shows the icon but the app bounces back: run `/Applications/NowPlaying.app/NowPlaying` from SSH as `mobile` to see the exception (a missing selector on 3.1 would show up there). Everything used is in the 3.0/3.1 SDK headers, but that is compile-time proof only.
- `UIProgressView` tint cannot be changed on 3.1 (it is the system blue bar); acceptable on black.
- Tab bar icons use the system items "Most Viewed" / "More" (the only built-in glyphs on 3.1 that fit); the titles are overridden to "Now Playing" / "Technical".
