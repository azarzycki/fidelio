# DockRemote (MobileSubstrate tweak, iPhone OS 3.1.3 phone)

Forwards the dock's Simple Remote **play/pause, next, previous** keys to `dock-audiod` (which routes
them to the active source, or to a paused source of ours that is still the remote's target) instead of
letting them reach the built-in iPod app. Other keys (menu,
select, up, down) and the volume keys always fall through. Built for a Philips Fidelio DS9000 dock.

**Phone only.** On the 5.1.1 iPad, iapd imports no `GSSendEvent`; it sends the keys to the now-playing
app with `MRMediaRemoteSendCommand`. The iPad route is therefore NowPlaying acting as a media
controller, an app feature rather than this tweak.

## B1.16.16: a paused source keeps the remote

Device trace of 2026-11-22 (B1.16.15, swallow on, Spotify playing): play/pause was taken and paused
Spotify; 1.5 s later audiod released Spotify (`quiet 1516 ms`) and wrote `none` to `active-source`; the
next play/pause, meant to resume, found no source active, passed, and the built-in iPod app started
playing. The takeover covered a playing source, not a paused one.

Since B1.16.16 the condition reads audiod's **remote target**, `/var/mobile/dockreceiver/remote-target` (one
line: `spotify`, `airplay` or `none`), instead of `active-source`:

- audiod writes it (B1.16.16 and later): the active source; with nothing active, the last active source,
  paused, until it disconnects from audiod, sends STOP (raop: the AirPlay session was torn down; cspot:
  the playlist ended), another source of ours becomes active (that one is then the target), or audio
  that is not ours plays (the Music app). No time limit. Replaced atomically (a temporary file renamed)
  at once on a change, and rewritten every 10 s.
- With nothing active, audiod sends every verb to the target, TOGGLE and PLAY as PLAY: play/pause
  resumes the paused source; next and previous go to it as well (`audiod/README.md`, B1.16.16).
- **No file** (an audiod before B1.16.16), or a file **older than 30 s** (left by an audiod that no longer
  maintains it): `active-source` decides, exactly as in B1.16.15.
- **audiod not running:** the socket check still gates every take. A crashed audiod leaves its last
  target in the file and its socket file on disk, but `connect()` fails, so the press passes. A stopped
  audiod writes `none` on its way out, and a starting one writes `none` before it accepts a client.
- The cost per press is what it was: one `stat` and one small read more (the target), no allocation.
- Load line: `DockRemote B1.16.16: hooked GSSendEvent ...` (since B1.17: `DockRemote B1.17: hooked ...`).

## Status (B1.16.15): the measured decode, a key log for NowPlaying; swallow behind a flag, first device test passed for previous and next

- **On the phone** since 2026-11-22 (loaded 18:35:49 phone time), with `remote-tweak.observe` and
  `remote-tweak.swallow` set. The first swallow test (18:42, Spotify active): two previous presses (code
  4) and two next presses (code 3) were taken and reached Spotify through audiod (`TAKEN ... (sent)`,
  then audiod's `control: prev -> 'spotify'` and `control: next -> 'spotify'`); up, down and select
  passed; audiod logged no audio interruption, so the iPod app did not take the audio. Not yet tested:
  play/pause under the takeover, AirPlay as the active source, and the keys with no source active. What
  the owner heard is not yet reported. `docs/HANDOFF.md` §2.5 has the details.

- **The hook point (B1.16.8).** b051bfc bound to `-[SimpleRemote sendEventRecord:toApp:]`, but no key ever
  called it, asleep or awake; `remote-events.log` stayed empty.
  - iapd's C++ `SimpleRemoteEventHandler` builds a GraphicsServices event itself and sends it.
  - iapd imports `_GSSendEvent` and `_GSSendSystemEvent`, but not `GSEventAccessoryKeyStateChanged`.
  - In the 3.1.2 SDK's GraphicsServices (armv6, disassembled), an accessory key is a 60-byte
    `GSEventRecord` of **type 4001**: flags at offset 40, infoSize at 48 (= 8), 8 bytes of info at 52.
    It is sent with `GSSendSystemEvent`, which is `GSSendEvent(record, the purple system event port)`.
  - So `MSHookFunction(GSSendEvent)` sees every record iapd sends, through any of those entry points.
- **The decode (B1.16.13, `DockRemoteKeys.cpp`),** measured on the phone on 2026-11-22 with 4ed6a5a
  observing: two key passes compared with iapd's own log of the same presses. The B1.16.8 reading (the key
  as `uint16` at 52, a 0/1 state at 56) was wrong.
  - The 32-bit word at 56 is a **command code**. The word at 52 was 0 in every event, and the header
    words were the same for every key.
  - The flags word tells the two events of a press apart: `0x400` first (down), `0x800` second (up). Both
    carry the same code.
  - A record counts as a key only when type is 4001, infoSize is 8, the word at 52 is 0 and the flags
    are exactly `0x400` or `0x800`. Anything else passes through untouched.
  - A type-101 event (flags 0, info all zero) comes before every press and every dock volume step. It
    always passes through.

  | Key | Code | Events | With swallow (conditions met) |
  |---|---|---|---|
  | play/pause | 0 | `0x400`, `0x800` | both dropped; TOGGLE sent at the `0x400` |
  | next (right) | 20 | one, `0x800` | dropped; NEXT sent |
  | next (right) | 3 | one, `0x800` | dropped; NEXT sent |
  | previous (left) | 21 | one, `0x800` | dropped; PREV sent |
  | previous (left) | 4 | one, `0x800` | dropped; PREV sent |
  | menu | 22 | `0x400`, `0x800` | passes |
  | select | 23 | `0x400`, `0x800` | passes |
  | up | 24 | `0x400`, `0x800` | passes |
  | down | 25 | `0x400`, `0x800` | passes |

  - menu, select, up and down equal iapd's own key numbers 22 … 25 (down verified on 2026-11-22: three
    presses).
  - The right key comes in two forms, both measured: code 20 on the first press of a run and code 3 on
    the quick repeats after it (2026-11-22, 18:40:57–59: 20, 3, 3), one `0x800` event each. 3 is iapd's
    own key number for next. B1.16.15 was built with 3 assumed by symmetry; it has been seen since.
  - The left key comes in two forms, both measured: code 21 (once, the first observation of the day) and
    code 4 (every press since), one `0x800` event each. 4 is iapd's own key number for previous. The
    single 21 fits the right key's pattern (first press 20/21, repeats 3/4), but it is one observation;
    that a hold gives code 4 is not confirmed.
  - Should 3 or 4 ever come as two events (`0x400`, `0x800`), the press is handled like play/pause: one
    verb at the `0x400`, both events dropped.
  - Some up and select presses were never sent by the dock (no packet in iapd's log); the up key arrives
    unreliably. The dock also thins out rapid repeats and passes a hold on as separate presses about a
    second apart. The tweak cannot see what the dock does not send; every packet the dock did send was
    decoded and logged (162 of 162 on the evening of 2026-11-22).
- **Presses (`PressTracker`).** A press is an optional `0x400` event and a `0x800` event of the same code.
  It is decided at its first event, once, and the decision covers all its events: it is taken whole or
  passed whole, and the verb goes out once, at the first event. A `0x800` with no `0x400` before it is a
  press of its own (next and previous so far always are).
- **Observe-only by default.** With no flags set, the hook passes every record straight through and writes
  nothing.
- **Load line.** Every iapd start appends one line to `/var/mobile/dockreceiver/remote-events.log` (and
  syslog): `DockRemote B1.17: hooked GSSendEvent with MSHookFunction from <library> ...` or `NOT hooked
  (...)`.
- **Observe:** `touch /var/mobile/dockreceiver/remote-tweak.observe`.
  - The hook appends one line per record to `remote-events.log`, and STILL passes it on: `gs type=
    subtype= flags= infoSize= port=`, the 52-byte header in hex, and up to 32 info bytes.
  - A record of the key shape (type 4001, infoSize 8) also gets `KEY code=<word at 56> flags=<flags>`.
  - A press taken over adds `TAKEN code= flags= -> audiod verb= (sent|send failed)`.
- **The key log, `/var/mobile/dockreceiver/remote-keys.log`** (B1.16.13), written while `remote-tweak.observe`
  OR `remote-tweak.swallow` exists. NowPlaying's Technical tab shows its last six lines.
  - One line per completed press, at its `0x800` event: `<unix seconds> <code> <name> <taken|passed>`,
    for example `1795369452 0 play-pause taken` or `1795369460 22 menu passed`.
  - Names: `play-pause`, `next` (codes 20 and 3), `previous` (21 and 4), `menu`, `select`, `up`, `down`,
    and `code-N` for an unknown code. The code in the line tells the two forms of next and previous
    apart. (B1.16.13 named 3 `seek-forward` and 4 `rewind`.)
  - Open, append, close per line, one `write()`. Created mode 0644 by iapd, which runs as `mobile`. When the
    file is larger than 16 KB it is truncated before the line is appended.
- **Swallow:** only once you also `touch /var/mobile/dockreceiver/remote-tweak.swallow`.
  - Taken over: play/pause, next and previous, with all their codes (the table above). The verb goes to
    audiod, and the iPod app sees nothing of the press.
  - Only while `audiod.dock_remote=on`, a source of ours is audiod's remote target (B1.16.16, above;
    before: a source of ours owns audiod), and audiod is reachable (`DockRemoteCore.cpp`, asked once per
    press). Otherwise the press passes as before.
  - Everything else always passes through.
- **Kill switch (no rebuild):** `touch /var/mobile/dockreceiver/remote-tweak.disabled` makes the hook a complete
  no-op, checked on every record.

Safety: fields are read with `memcpy` at fixed offsets, the info dump is bounded (≤ 32 bytes, and none
for an implausible size), the hook is `@try`-guarded, and nothing blocks beyond DockRemoteCore's short
socket timeouts to audiod.

Host test: `hosttest/keys_test.cpp`, the suite `remote_keys` of `tools/run-host-tests.sh`. It runs
`DockRemoteKeys.cpp` on the host: each row of the table, the shapes that must pass, the decisions per
press, exactly one verb and one log line per press, and the 16 KB cap.

B1.16.16: `hosttest/core_test.cpp` (suite `remote_core`) runs `DockRemoteCore.cpp`'s condition against
files in a temporary directory: remote-target naming a source or `none`, absent or stale (the fall-back
to active-source), the socket reachable or not, the kill switch and the setting, and the trace's press
sequence through the PressTracker (without remote-target the resume passes, as on the phone; with it
both presses are taken). `hosttest/probe.cpp` (`dockremote_probe`) is the same condition as a command;
audiod's suite `remote_tgt` runs it against the files a host audiod writes.

## Device facts used (phone, iPhone OS 3.1.3)

- iapd: thin armv6 `MH_EXECUTE`. Its GraphicsServices imports include `_GSSendEvent`, `_GSSendSystemEvent`
  and `_GSGetPurpleSystemEventPort`.
- Keys in iapd's own log (`SimpleRemoteEventHandler got evKeyDown ... key:N`): play/pause 0, next 3, prev 4,
  menu 22, select 23, up 24, down 25. In the GraphicsServices event, next is code 20 or 3 and previous
  code 21 or 4 (the table above).
- All type-4001 events go to one receiving port whichever app is in front; only the built-in iPod app
  acts on menu, select, up and down. After the first 15 seconds of an attach the phone sends the dock
  nothing, so nothing on the phone makes the dock filter keys (2026-11-22).
- Substrate: `/Library/MobileSubstrate/MobileSubstrate.dylib` is only the injection stub. `MSHook*` live in
  CydiaSubstrate (`/usr/lib/libsubstrate.dylib`). `MSHookFunction` is looked up where it is exported:
  first the images already loaded, then `libsubstrate.dylib`, `CydiaSubstrate.framework/CydiaSubstrate`
  and `MobileSubstrate.dylib`, stopping at the first that has the symbol (B1.16.5).

## Build

`IOS3_SDK=<sysroot> PATH=<toolchain>/bin:$PATH ./build.sh [outdir]` produces `DockRemote.dylib` (ARMv6,
fake-signed) and copies `DockRemote.plist` (the iapd filter). MobileSubstrate and `GSSendEvent` are
resolved at run time.

## Install / remove: iapd restarts once, which drops the dock until a reseat

Install **undocked** (observe-only is safe to install):
1. Copy `DockRemote.dylib` and `DockRemote.plist` to `/Library/MobileSubstrate/DynamicLibraries/`.
   To **replace** an installed `DockRemote.dylib`, never copy over it: the running iapd has it mapped,
   and on the phone a file that has run is killed when new content is written into the same inode
   (measured with audiod, 2026-11-22; `docs/HANDOFF.md` §8). Copy it to a temporary name that does not
   end in `.dylib` (MobileSubstrate would load it), give it the old file's owner and mode, check its
   sha256, then `mv -f` it over `DockRemote.dylib`.
2. With the phone undocked, restart iapd (`killall iapd`; launchd relaunches it) or reboot.
3. Check `/var/mobile/dockreceiver/remote-events.log` for the `hooked GSSendEvent ... from ...` line.
4. Dock the phone, `touch /var/mobile/dockreceiver/remote-tweak.observe`, press the buttons, and watch
   NowPlaying's Technical tab (or read `remote-keys.log`).
5. The swallow test: with `dock_remote=on` and Spotify playing, `touch
   /var/mobile/dockreceiver/remote-tweak.swallow`. play/pause, next and previous should drive Spotify and show
   "(taken)" in the Technical tab, and the iPod app should not start.
6. B1.16.16 (needs audiod B1.16.16): play/pause to pause, wait more than 2 s (audiod releases the
   source; `remote-target` still says `spotify`), play/pause again: Spotify resumes, both presses show
   "taken", and the iPod app does not start. Then start the Music app by hand: `remote-target` becomes
   `none` and the keys pass to it again. Passed on the phone on 2026-11-22 (`docs/HANDOFF.md` §2.5).

Remove: delete the two files (or `touch /var/mobile/dockreceiver/remote-tweak.disabled` to disable without
removing), then restart iapd undocked and reseat.
