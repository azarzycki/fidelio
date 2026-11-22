# NowPlaying.app ("Fidelio") — dock display for cspot-ios3

A native UIKit app for iPhone OS 3.1 (armv6) that shows what cspot-ios3 is playing. It is a display only: playback is driven from the Spotify app on your phone/laptop. Built with the armv6-ios3 toolchain (Objective-C, manual retain/release, no ARC, no blocks), links UIKit, Foundation, CoreGraphics, QuartzCore (+ CoreFoundation, libobjc, libSystem, libgcc_s pulled in by them). Fake-signed with `ldid -S`. `armv6-ios3-check`: PASS, 52 undefined symbols, all in the 3.1.2 SDK.

**Not run on hardware by me.** What is verified: it compiles and links, the Mach-O and Objective-C metadata are the 2009 shapes (non-fragile ABI sections, `__objc_classlist`, no PIE, no dyld-info), and the Info.plist keys are the iOS 3.1 ones.

## What the app reads (1.2, audiod)
`/var/mobile/fidelio/active-source` — written by fidelio-audiod (rewritten every 10 s): `spotify`, `airplay` or `none`; a file older than 30 s is treated as absent (audiod gone). The app shows **only** that source's status; with `none` it shows "Idle" even if a player still claims to be playing (a paused AirPlay sender streaming silence, a source that lost the arbitration). Without the file (audiod not running) it shows whichever `status-*.plist` changed most recently.

`/var/mobile/fidelio/status-spotify.plist` / `status-airplay.plist` — the per-source status (cspot-ios3 and fidelio-raop no longer share one `status.plist`); same keys as below, `cover` names `cover-spotify.jpg` / `cover-airplay.jpg`. The watchdog (`killall -9 cspot-ios3` when the status goes stale) watches `status-spotify.plist` regardless of what is shown.

Original contract, per source: `status.plist` — polled once per second with `[NSDictionary dictionaryWithContentsOfFile:]`. iOS 3 has no JSON parser in Foundation (NSJSONSerialization is iOS 5), so cspot-ios3 rev 4 writes **both** `status.json` (your contract, for scripts) and `status.plist` (XML plist, same keys/values) atomically, each as `.tmp` + `rename()`. The app reads only the plist. A partial read (file missing for a moment) keeps the last good dictionary, so the screen never flickers.

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
- **Technical**: plain black table, refreshed each second: state, codec+bitrate, TLS suite, access point, country, cspot RSS, app RSS (`task_info`), device free memory (`host_statistics` free pages), uptime (`KERN_BOOTTIME`), Wi-Fi IP (`getifaddrs` en0), battery (`UIDevice`, monitoring enabled), status-file age, device name, build. Since 1.4.2 a "Dock remote" section with the last key presses comes first (below).

Memory: the app allocates the two view hierarchies, one decoded cover (≈300 KB for a 300 px JPEG) and a 14-row table. No caches, no large buffers.

## One app for the phone and the iPad (1.4.0, B1.18)
The same ARMv6 binary runs natively on both: `UIDeviceFamily` is 1 and 2, set by hand because the 3.1 SDK has no such key. A screen whose shorter side exceeds 600 pt counts as a tablet, decided at run time from `[UIScreen mainScreen].bounds` (no `UI_USER_INTERFACE_IDIOM` in the 3.1 SDK, no OS-version test). The iPad no longer runs the app in the 320×480 compatibility frame with the 2× button.
- **Now Playing layout** (`NPLayout.c`, plain C, used by the app and by the host test `nowplaying/hosttest/layout_test.c`): the phone keeps the B1.16.1 frames exactly (the test pins them). On the tablet:
  - **portrait:** a 600 pt cover on top, larger type (title 40 pt), then progress, times and state, then 96×64 pt transport buttons (touch targets ≥ 60 pt);
  - **landscape:** the cover left, and on the right a column with the text at the cover's top edge and the controls ending at its bottom edge.

  Usable sizes tested: 320×431, 768×955/975 and 1024×699/719 (with and without the status bar, minus the 49 pt tab bar).
- **Rotation:** the tablet rotates to all four orientations (`UISupportedInterfaceOrientations~ipad`, every view controller agrees); the phone stays portrait. On iOS 4+ the tab bar controller is the window's `rootViewController` (iOS 5 routes rotation through it); on 3.1 its view is added as before.
- **Cover:** decoded to a 600 px square on the tablet (1.4 MB) and 256 px on the phone as before. `placeholder_600.png` is the tablet's empty tile. cspot fetches Spotify's 640 px cover (`--cover-size`, see CSPOT.md).
- **Technical and Settings:** on the tablet the tables sit in a centred column at most 640 pt wide (readable rows), Technical in 20 pt type with 44 pt rows. The Technical tab is now a plain view controller owning its table, which is what allows the column.
- **Status bar:** hidden on both devices (an appliance display); launch images `Default-Portrait~ipad.png` 768×1024 and `Default-Landscape~ipad.png` 1024×768 are full-screen for that reason. Icons: `Icon.png` 57 and `Icon-72.png` 72.
- After installing 1.4.0 run `uicache` as for 1.3.1: the device family, icons and launch images are read when the app list is rebuilt.

## The iPad's dock remote: NowPlaying as a media controller (1.4.1, B1.16.9, a staged prototype)

On the 5.1.1 iPad, iapd decodes the dock's Simple Remote keys and sends them with `MRMediaRemoteSendCommand`
to the **now-playing app**. UIKit delivers them there as remote-control events. A tweak can't intercept
them, unlike on the phone (DockRemote, B1.16.8), so NowPlaying has to receive them itself. The open
question is what it takes to be the now-playing app while the audio comes from the daemons. Hence a staged
prototype, chosen with a flag file read at launch: `/var/mobile/fidelio/remote-control.stage` holding 1, 2
or 3 (absent = 1). Kill and relaunch the app after changing it.

1. `beginReceivingRemoteControlEvents` (iOS 4+), with the window (`NPWindow`) kept as first responder. It
   claims first responder at launch, on activation and when each tab appears.
   - Every remote-control event is caught in `NPApplication`, in `-sendEvent:` and at the end of the
     responder chain, and handled once.
   - It is logged to `/var/mobile/fidelio/remote-control.log` with its subtype.
   - Play, pause, stop, toggle, next and previous are forwarded to audiod's CONTROL channel, the same
     code the on-screen buttons use (`NPSendControl`). Seek events are logged only.
2. Stage 1 plus `MRMediaRemoteSetCanBeNowPlayingApplication(true)`: MediaRemote via `dlopen`/`dlsym`,
   present on 5.1.1.
3. Stage 2 plus silence (an in-memory WAV, looped by `AVAudioPlayer`) in a **mixable** MediaPlayback
   session. It is mixable on purpose: audiod's own session is a non-mixable MediaPlayback session, which a
   non-mixable one here would interrupt. Below stage 3 the app does not touch audio at all.

The log's first line per launch names the stage and what each step did. On iPhone OS 3.1 none of this
runs: the remote-control API does not exist there, and the window never claims first responder. The
phone's NowPlaying behaves as before, apart from the version, now 1.4.1.

Test on the iPad: Spotify playing through audiod, NowPlaying in front, dock keys pressed. Does
`remote-control.log` fill, and with which subtypes? Try stage 1, then 2, then 3. Also check who gets the
keys when the iPod app was the last to play.

## The dock remote's key presses in the Technical tab (1.4.2, B1.16.13)

So that the owner can see at the dock which remote keys reached the phone (and whether a key needs a
firmer press), the Technical tab starts with a section **"Dock remote"**.

- **Source:** `/var/mobile/fidelio/remote-keys.log`, written by the DockRemote tweak inside iapd (phone
  only) while `remote-tweak.observe` or `remote-tweak.swallow` is set: one line per completed press,
  `<unix seconds> <code> <name> <taken|passed>` (`tweak/DockRemote/README.md`).
- **Shown:** the last six presses, newest first, each as the local time `HH:MM:SS` and the key in words
  (`play/pause`, `next`, `previous`, `menu`, `select`, `up`, `down`, `seek forward`, `rewind`, `code N`),
  with "(taken)" when the tweak swallowed the press and sent it to audiod.
- **The newest press** is bold and white; for 3 s after it was logged it is green. The older ones are grey.
- **No file, or no complete line in it:** one entry, "No key seen yet." with "The dock remote tweak logs
  keys only while it is observing." under it. That is what the iPad always shows: it has no tweak.
- **Polling:** twice a second, only while the tab is visible. Each poll is one `stat()`; the file (its last
  8 KB at most) is read only when its size, date or inode changed. The poll starts when the tab appears
  and stops when another tab is selected (within a second, from the app's 1 Hz tick) or the app resigns
  active.
- The parsing, the words and the clock are plain C (`NPKeyLog.c`), host-tested by
  `nowplaying/hosttest/keylog_test.c` (the suite `keylog`).
- The rest of the tab is unchanged; it follows as the second section.

## Install
```sh
scp -r NowPlaying.app root@phone:/Applications/
ssh root@phone 'chown -R mobile:mobile /Applications/NowPlaying.app; chmod +x /Applications/NowPlaying.app/NowPlaying; killall SpringBoard'
```
If the icon does not appear after the respring: `uicache` (Cydia's) or `su mobile -c uicache`, then respring again. The binary is already fake-signed; re-sign with `ldid -S NowPlaying.app/NowPlaying` only if you rebuild it. The app needs no entitlements; it only reads `/var/mobile/fidelio` (owned by `mobile`, same user it runs as).

**URL scheme (1.3.1, B1.16.4).** The app registers the scheme `nowplaying:`; fidelio-audiod opens it with `SBSOpenSensitiveURLAndUnlock` so that unlocking a locked device for playback lands directly in the app. Opening the URL only brings the app forward; its content is ignored. iOS reads an app's URL schemes when it rebuilds its app list, not on a respring alone, so after installing or updating 1.3.1 run `uicache` (Cydia's, as `mobile`: `su mobile -c uicache`), then respring. Check: `uiopen nowplaying:` over SSH, where the `uiopen` tool is installed, should open the app. Without the registration SpringBoard has no app for the URL, and whether it still unlocks is untested. If audiod logs `unlock not confirmed`, check the registration first. `--unlock-url prefs:` (the URL of the device proof) is a fallback for that test; it opens Settings first, then audiod's launch retry brings NowPlaying forward.

## Build
```sh
# IOS3_SDK: a sysroot (toolchain/sysroot/build-sysroot.sh, or ../build/sysroot/root after tools/build-arm.sh), or Apple's iPhoneOS3.1.2.sdk
export IOS3_SDK=/path/to/sysroot PATH=<toolchain>/bin:$PATH
./build.sh            # -> build/NowPlaying.app (compiles, links, strips, ldid -S, armv6-ios3-check, images)
```
`gen_images.py` draws `Icon.png` (57×57), `placeholder.png` (256×256) and `Default.png` (320×480) in pure Python; no PIL needed.

## Things to watch on the device
- If SpringBoard shows the icon but the app bounces back: run `/Applications/NowPlaying.app/NowPlaying` from SSH as `mobile` to see the exception (a missing selector on 3.1 would show up there). Everything used is in the 3.0/3.1 SDK headers, but that is compile-time proof only.
- `UIProgressView` tint cannot be changed on 3.1 (it is the system blue bar); acceptable on black.
- Tab bar icons use the system items "Most Viewed" / "More" (the only built-in glyphs on 3.1 that fit); the titles are overridden to "Now Playing" / "Technical".
