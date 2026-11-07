# Code review: NowPlaying.app ("Fidelio") for iPhone OS 3.1.3 / armv6

Scope: everything in `src/` as of the initial commit (`83d2050`). Line numbers below refer to
that revision; the fixes are the commits on this branch named in each finding. Verification was
by reading, by checking the iPhoneOS 3.1.2 SDK headers for API availability/semantics, and by
building with the armv6-ios3 toolchain (`build.sh`: compile, link, strip, `ldid -S`,
`armv6-ios3-check` => PASS, 56 undefined symbols all resolved by the 3.1.2 dylibs). Nothing was
run on hardware; nothing here can be, so "verified" never means "observed on a device".

Every source file compiles clean with `-Wall -Wextra` (the build only uses `-Wall -Wno-unused`).

## Critical

None found. There is no code path I could make crash with any status.plist contents: every
value read from the dictionary is type-checked (`string:` / `integer:`), every user string goes
through `%@`, never as a format string, UILabel truncates, and `arrayWithObjects:` in the
Technical tab is only ever fed non-nil objects (a nil in the middle would silently truncate the
row and `objectAtIndex:1` would throw, so that invariant matters; it holds).

## High

### H1. The cover is kept and uploaded at full decoded size; its size is not bounded
`src/FidelioStatus.m:63`, `src/NowPlayingViewController.m:53-57`

`[[UIImage alloc] initWithContentsOfFile:]` is handed straight to the UIImageView. The first draw
decodes the JPEG to a w*h*4 bitmap that stays resident for as long as the image is displayed,
and it is also the layer's texture. Spotify's 640x640 is 1.6 MB; cover.jpg comes from the
network (Spotify CDN, or an AirPlay sender's embedded artwork, commonly 1400x1400 = 7.8 MB,
Apple Music 3000x3000 = 36 MB). On a 116 MB device that also runs the receiver, 36 MB is a
jetsam kill, and anything over 1024 px is also beyond the MBX GPU's texture limit. APP.md's
"≈300 KB" assumes a 300 px image that nothing guarantees.

Scenario: AirPlay from iTunes with 1400 px art -> 7.8 MB resident per cover plus the transient
decode; a 3000 px cover -> the app is killed while decoding.

Verified: arithmetic + UIImage/CGImage semantics; `UIGraphicsBeginImageContext`,
`drawInRect:`, `UIImage.size` present in the 3.1.2 UIKit headers (`UIGraphics.h:26-28`,
`UIImage.h:45,53`).

Fixed: "Keep only a 256 px copy of the cover and refuse oversized JPEGs". The cover is decoded
once into a 256x256 aspect-filled bitmap (256 KB, the displayed size on the non-Retina screen),
the original is released, and files with a side over 1600 px (10 MB transient) are refused from
the header before any decode and show the placeholder. 1600 is a judgment call (it admits
iTunes' 1400 px art); the receiver should really downscale AirPlay artwork before writing it.

### H2. Memory warning while the Technical tab is visible leaks the whole Now Playing view
`src/NowPlayingViewController.m:19-30, 43-74`

On 3.x the default `-didReceiveMemoryWarning` "releases the view if it doesn't have a superview"
(`UIViewController.h:101`), which is exactly the Now Playing view whenever Technical is the
selected tab. The nine `retain`ed ivars (`coverView`, `placeholder`, seven labels/progress) keep
their +1; the next `-loadView` overwrites the pointers with new objects. Each warning in that
state leaks a placeholder bitmap (256 KB) plus the subviews; there is no `-viewDidUnload`
(available since 3.0, `UIViewController.h:88`).

Scenario: dock sits on the Technical tab; cspot's memory spikes during a cover fetch; UIKit sends
a memory warning; a few hundred KB leak each time, on the device with the least RAM Apple ever
shipped.

Verified: header comment; reasoning about ownership.

Fixed: "Release Now Playing subviews in viewDidUnload".

### H3. The tabs are titled "Most Viewed" and "More", not "Now Playing" and "Technical"
`src/NowPlayingViewController.m:13-14`, `src/TechnicalViewController.m:9-10`

Both items are created with `initWithTabBarSystemItem:` and then given a `title`. A system tab
bar item keeps its system-provided title and glyph; that is the documented point of a system
item (`UITabBarItem.h` keeps `isSystemItem`/`systemItem` flags and a fixed `_possibleTitles`
set), and setting `title` on one is ignored. APP.md says "the titles are overridden", but also
says the app was never run on hardware. The glyphs ("Most Viewed" = a bar chart, "More" = three
dots) are also unrelated to the content.

Verified: documented UIKit behaviour and the 3.1.2 header; not hardware-verified (noted as an
accepted risk below: if I am wrong, the custom icons still work and simply look different).

Fixed: "Use custom tab bar glyphs so the tabs are not titled Most Viewed / More".
`gen_images.py` now also draws two 30x30 alpha-mask glyphs (eighth note, three-row list),
deterministically (checked: identical bytes across two runs), and the items use
`initWithTitle:image:tag:`.

## Medium

### M1. A cover replaced within the same second is never reloaded
`src/FidelioStatus.m:58-61`

The reload key is (name, mtime). HFS+ stores mtime in whole seconds. If cspot renames a new
cover.jpg over the old one within the same second (two quick "next" presses; each fetch is a
~100 ms TLS round trip), the mtime is equal and the previous track's art stays on screen for the
whole track. Fixed by also comparing `NSFileSize` (same attrs dictionary, free):
"Detect cover replacement by size too and drop stale art on a failed load".

### M2. A cover that fails to decode keeps the previous track's art and is re-read every second
`src/FidelioStatus.m:62-74`

When `initWithContentsOfFile:` returns nil (0-byte file, truncated or non-JPEG bytes from the
network) the old UIImage is kept, so the previous album's cover is shown under the new track;
and because the key is not recorded, the condition is true again on every tick, so the file is
opened and header-parsed once a second until the next cover change. Same commit as M1: a failed
load now leaves `cover` nil (placeholder) and records the key, so it is retried only when the
file changes. A missing file with `cover` set behaves the same way instead of retrying each tick.

### M3. An unparseable status file is reported as "no status file" / silently as "(stale)"
`src/FidelioStatus.m:42-44`, `src/NowPlayingViewController.m:97`

`dictionaryWithContentsOfFile:` returns nil both when the file is absent and when it is not a
plist dictionary (writer bug; XML the parser rejects, e.g. a control character in Spotify
metadata). The code set `fileMissing` for both and showed "Waiting for cspot (no status file)",
or, if an earlier dict existed, kept showing that track as "Playing (stale)" after 15 s. The
comment also says it guards a "partial read", which cannot happen (rename is atomic).
Fixed: "Tell an unparseable status file apart from a missing one" (`fileExistsAtPath:`, new
`fileUnreadable` flag, distinct status line).

### M4. The app keeps polling at 1 Hz while the screen is locked
`src/FidelioAppDelegate.m:23`

iPhone OS 3 keeps the frontmost app running when the sleep button is pressed (or a call/alert
sits on top); only `applicationWillResignActive:` is sent. The timer then reads + parses the
plist, stats the cover, formats strings and relayouts labels once a second with nothing visible,
for as long as the dock sits locked. Fixed: "Stop the 1 Hz poll while the app is inactive
(screen locked)" -- timer invalidated on resign, recreated on become-active after one immediate
tick so the pre-lock frame is not shown. The delegate <-> NSTimer retain cycle is now stated in
a comment (acceptable: the delegate lives for the process).

### M5. `build.sh` does `rm -rf` on whatever directory the caller names
`src/build.sh:8`

`./build.sh ~/stuff` wipes `~/stuff` before building. Fixed to remove only the script's own
products: "Do not rm -rf the caller-supplied output directory in build.sh".

### M6. `build.sh` is committed without the executable bit
README and APP.md say `./src/build.sh`; a fresh clone gets "Permission denied" (that is how I hit
it). Fixed: "Make build.sh executable" (mode 100755 in every commit on this branch).

### M7. The Technical table does not fit the screen
`src/TechnicalViewController.m:25`

14 rows x 34 px = 476 px in a 431 px table (480 - 49 tab bar, status bar hidden): "Device" is
cut and "Build" is off screen unless scrolled, and the table is `reloadData`'d every second.
Fixed: 30 px rows = 420 px, "Fit all 14 Technical rows above the tab bar".

## Low (not fixed; recommendations)

- **Position clock uses the wall clock** (`FidelioStatus.m:95-99`). `readTime` is an NSDate;
  if the clock is set (NTP after Wi-Fi comes up, or by hand) while playing, the position jumps
  (forward: snaps to `duration`; backward: shows 0:00 and remaining > duration) until the next
  read, at most 1 s later. `CACurrentMediaTime()` (QuartzCore, already linked, `CABase.h:135`)
  would be immune. Left as is: the glitch is transient and self-healing.
- **A stale file in state `playing` keeps the clock running** to the end of the track and pins it
  there while "(stale)" is shown. Freezing the position once `fileAge > 15` would be more honest.
- **`mach_host_self()` twice per second** (`FidelioStatus.m:121,123`) without
  `mach_port_deallocate`; each call bumps the send-right uref count. The kernel pegs it at the
  maximum (copyout with overflow allowed), so it is not a leak that grows, and `host_page_size`
  never changes, but the idiom is wrong: fetch the port and the page size once.
- **`makeLabel:...` returns a +1 object** (`NowPlayingViewController.m:32`) under a name that
  the Cocoa convention (and the static analyzer) treats as autoreleased. Correct as used; rename
  to `newLabel:` or autorelease and let the ivar assignment retain.
- **`FIDELIO_APP_VERSION` duplicates `CFBundleVersion`** (`FidelioStatus.h:6`, `Info.plist:33`).
  Read `[[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"]` instead so the
  two cannot drift.
- **Info.plist**: `CFBundleIconFiles`, `UIDeviceFamily` and `UISupportedInterfaceOrientations`
  are 3.2+ keys; 3.1.3 ignores them and uses `CFBundleIconFile` / `UIInterfaceOrientation`,
  which are present and correct. Harmless, but they document nothing for this target. Consider
  adding `UIRequiresPersistentWiFi` = true: the device exists to show a Wi-Fi receiver, and the
  key keeps Wi-Fi up when the user locks the screen by hand (the idle timer is disabled, so
  auto-lock never happens, but the sleep button does).
- **`setStatusBarHidden:YES animated:NO`** (`FidelioAppDelegate.m:6`) is redundant with
  `UIStatusBarHidden` in Info.plist; harmless.
- **Timer runs only in the default run loop mode**, so both tabs freeze while the user drags the
  Technical table. For a dock display this is arguably the right behaviour (reloading a table
  under a finger is worse); noted so nobody "fixes" it by accident.
- **`cover` is used as a path component** (`FidelioStatus.m:57`). The writer is our own process
  and only ever writes `"cover.jpg"` or `""`; a hostile value could at most make the app try to
  open some other file as an image. Fine, but worth a comment in the contract.

## Accepted risks

- NSTimer retains the app delegate: retain cycle for an object that lives as long as the
  process; stated in the code.
- File I/O (1 KB plist, two stats) and the once-per-track JPEG decode (~50-100 ms on the
  412 MHz ARM11 for a 640 px image) happen on the main thread. No threads or GCD on this
  platform; a brief hitch at track change is acceptable for a display.
- The 1600 px decode cap (H1) is a guess at what this device can spare transiently; lower it if
  memory warnings show up, and make the receiver downscale AirPlay artwork.
- H3 (system tab item titles) is based on documented UIKit behaviour, not on a device run.
- The `cover` reload key is (name, mtime, size); a replacement within the same second with
  exactly the same byte size is still missed.
- `vm.free_count` ("Free memory") excludes inactive/speculative pages, so it understates what
  is reclaimable; it is labelled as free pages, so fine.

## Overall assessment

For a first cut written without hardware this is careful code: the memory management is
correct everywhere I looked (every alloc/copy/retain has its release, `rows` is swapped without
leaking, cells are reused, the cover is not re-decoded per tick because `coverView.image` is only
reassigned on change), the plist values are type-checked before use, no network string ever
reaches a format string, `getifaddrs` is freed on every path, and the layout numbers add up at
320x480 with the tab bar. The real problems were in what the code does not bound or does not
clean up: a network-sized JPEG kept at full resolution on a 116 MB device, the iOS-3 view purge
leaking the offscreen view, cover-change detection with a 1 s blind spot that leaves wrong art
on screen, and a visible tab-title mistake that only a device run would have shown. All of those
are fixed on this branch in nine small commits that still build and pass `armv6-ios3-check`.
What remains unverifiable from here is behaviour on the real phone: the tab titles, the memory
headroom for the 1600 px cap, and whether SpringBoard on 3.1.3 is happy with the bundle (the
Info.plist keys it needs are all present).
