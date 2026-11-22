# DockRemote (MobileSubstrate tweak, iPhone OS 3.1.3 phone)

Forwards the dock's Simple Remote **play/pause, next, previous** keys to `fidelio-audiod` (which routes
them to the active source) instead of letting them reach the built-in iPod app. Other keys (menu,
select, up, down) and the volume keys always fall through. Built for a Philips Fidelio DS9000 dock.

**Phone only.** On the 5.1.1 iPad, iapd imports no `GSSendEvent`; it sends the keys to the now-playing
app with `MRMediaRemoteSendCommand`. The iPad route is therefore NowPlaying acting as a media
controller, an app feature rather than this tweak.

## Status (B1.16.13): the measured decode, a key log for NowPlaying; swallow staged behind a flag

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
  | next (a tap) | 20 | one, `0x800` | dropped; NEXT sent |
  | previous (a tap) | 21 | one, `0x800` | dropped; PREV sent |
  | menu | 22 | `0x400`, `0x800` | passes |
  | select | 23 | `0x400`, `0x800` | passes |
  | up | 24 | `0x400`, `0x800` | passes |
  | down | 25 (unverified) | not seen | passes |
  | seek forward | 3 (unverified) | not seen | passes |
  | rewind | 4 (unverified) | one, `0x800` (seen once) | passes |

  - menu, select and up equal iapd's own key numbers 22, 23, 24. Down is 25 in iapd's numbering but never
    arrived, so 25 is an extrapolation.
  - Codes 3 and 4 are iapd's key numbers for next and previous. Code 4 came once, as one `0x800` event,
    when a previous press reached iapd twice. The reading that a held key becomes seek/rewind is
    unverified.
- **Presses (`PressTracker`).** A press is an optional `0x400` event and a `0x800` event of the same code.
  It is decided at its first event, once, and the decision covers all its events: it is taken whole or
  passed whole, and the verb goes out once, at the first event. A `0x800` with no `0x400` before it is a
  press of its own (next and previous always are).
- **Observe-only by default.** With no flags set, the hook passes every record straight through and writes
  nothing.
- **Load line.** Every iapd start appends one line to `/var/mobile/fidelio/remote-events.log` (and
  syslog): `DockRemote B1.16.13: hooked GSSendEvent with MSHookFunction from <library> ...` or `NOT hooked
  (...)`.
- **Observe:** `touch /var/mobile/fidelio/remote-tweak.observe`.
  - The hook appends one line per record to `remote-events.log`, and STILL passes it on: `gs type=
    subtype= flags= infoSize= port=`, the 52-byte header in hex, and up to 32 info bytes.
  - A record of the key shape (type 4001, infoSize 8) also gets `KEY code=<word at 56> flags=<flags>`.
  - A press taken over adds `TAKEN code= flags= -> audiod verb= (sent|send failed)`.
- **The key log, `/var/mobile/fidelio/remote-keys.log`** (B1.16.13), written while `remote-tweak.observe`
  OR `remote-tweak.swallow` exists. NowPlaying's Technical tab shows its last six lines.
  - One line per completed press, at its `0x800` event: `<unix seconds> <code> <name> <taken|passed>`,
    for example `1795369452 0 play-pause taken` or `1795369460 22 menu passed`.
  - Names: `play-pause`, `next`, `previous`, `menu`, `select`, `up`, `down`, `seek-forward`, `rewind`, and
    `code-N` for an unknown code.
  - Open, append, close per line, one `write()`. Created mode 0644 by iapd, which runs as `mobile`. When the
    file is larger than 16 KB it is truncated before the line is appended.
- **Swallow:** only once you also `touch /var/mobile/fidelio/remote-tweak.swallow`.
  - Taken over: play/pause, next and previous (the table above). The verb goes to audiod, and the iPod
    app sees nothing of the press.
  - Only while `audiod.dock_remote=on`, a Fidelio source owns audiod, and audiod is reachable
    (`DockRemoteCore.cpp`, asked once per press). Otherwise the press passes as before.
  - Everything else always passes through.
- **Kill switch (no rebuild):** `touch /var/mobile/fidelio/remote-tweak.disabled` makes the hook a complete
  no-op, checked on every record.

Safety: fields are read with `memcpy` at fixed offsets, the info dump is bounded (≤ 32 bytes, and none
for an implausible size), the hook is `@try`-guarded, and nothing blocks beyond DockRemoteCore's short
socket timeouts to audiod.

Host test: `hosttest/keys_test.cpp`, the suite `remote_keys` of `tools/run-host-tests.sh`. It runs
`DockRemoteKeys.cpp` on the host: each row of the table, the shapes that must pass, the decisions per
press, exactly one verb and one log line per press, and the 16 KB cap.

## Device facts used (phone, iPhone OS 3.1.3)

- iapd: thin armv6 `MH_EXECUTE`. Its GraphicsServices imports include `_GSSendEvent`, `_GSSendSystemEvent`
  and `_GSGetPurpleSystemEventPort`.
- Keys in iapd's own log (`SimpleRemoteEventHandler got evKeyDown ... key:N`): play/pause 0, next 3, prev 4,
  menu 22, select 23, up 24, down 25. In the GraphicsServices event, next and previous are codes 20 and 21
  (the table above).
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
2. With the phone undocked, restart iapd (`killall iapd`; launchd relaunches it) or reboot.
3. Check `/var/mobile/fidelio/remote-events.log` for the `hooked GSSendEvent ... from ...` line.
4. Dock the phone, `touch /var/mobile/fidelio/remote-tweak.observe`, press the buttons, and watch
   NowPlaying's Technical tab (or read `remote-keys.log`).
5. The swallow test: with `dock_remote=on` and Spotify playing, `touch
   /var/mobile/fidelio/remote-tweak.swallow`. play/pause, next and previous should drive Spotify and show
   "(taken)" in the Technical tab, and the iPod app should not start.

Remove: delete the two files (or `touch /var/mobile/fidelio/remote-tweak.disabled` to disable without
removing), then restart iapd undocked and reseat.
