# DockRemote (MobileSubstrate tweak, iPhone OS 3.1.3 phone)

Forwards the dock's Simple Remote **play/pause, next, previous** keys to `fidelio-audiod` (which routes
them to the active source) instead of letting them reach the built-in iPod app. Other keys (menu,
select, up, down) and the volume keys always fall through. Built for a Philips Fidelio DS9000 dock.

**Phone only.** On the 5.1.1 iPad, iapd imports no `GSSendEvent`; it sends the keys to the now-playing
app with `MRMediaRemoteSendCommand`. The iPad route is therefore NowPlaying acting as a media
controller, an app feature rather than this tweak.

## Status (B1.16.8): hooks GraphicsServices' `GSSendEvent`, ships OBSERVE-ONLY

- **Why the hook moved.** b051bfc bound to `-[SimpleRemote sendEventRecord:toApp:]`, but no key ever
  called it, asleep or awake; `remote-events.log` stayed empty.
  - iapd's C++ `SimpleRemoteEventHandler` builds a GraphicsServices event itself and sends it.
  - iapd imports `_GSSendEvent` and `_GSSendSystemEvent`, but not `GSEventAccessoryKeyStateChanged`.
  - In the 3.1.2 SDK's GraphicsServices (armv6, disassembled), an accessory key is a 60-byte
    `GSEventRecord` of **type 4001**: flags at offset 40, infoSize at 48 (= 8), the key (`uint16`) at 52
    and its state (`int`) at 56. It is sent with `GSSendSystemEvent`, which is `GSSendEvent(record, the
    purple system event port)`.
  - So `MSHookFunction(GSSendEvent)` sees every record iapd sends, through any of those entry points.
- **Observe-only by default.** With no flags set, the hook passes every record straight through.
- **Load line.** Every iapd start appends one line to `/var/mobile/fidelio/remote-events.log` (and
  syslog): `DockRemote B1.16.8: hooked GSSendEvent with MSHookFunction from <library> ...` or `NOT hooked
  (...)`.
- **Observe:** `touch /var/mobile/fidelio/remote-tweak.observe`.
  - The hook appends one line per record, and STILL passes it on: `gs type= subtype= flags= infoSize=
    port=`, the 52-byte header in hex, and up to 32 info bytes.
  - For a key record it adds `KEY key=<n> state=<s>`. That decode is used only when type is 4001 and
    infoSize is exactly 8, which proves the 3.1 layout.
  - Press each button with the phone docked and send the log. It shows the key numbers and which state
    value means "down".
- **Swallow:** only once you also `touch /var/mobile/fidelio/remote-tweak.swallow`.
  - The swallowed keys are play/pause 0, next 3 and prev 4. Their key-down record (state 1, an
    assumption the observe log must confirm first) is forwarded to audiod.
  - Both their down and up records are dropped, so the iPod app never sees them.
  - Only while `audiod.dock_remote=on`, a Fidelio source owns audiod, and audiod is reachable.
  - Everything else always passes through.
- **Kill switch (no rebuild):** `touch /var/mobile/fidelio/remote-tweak.disabled` makes the hook a complete
  no-op, checked on every record.

Safety: fields are read with `memcpy` at fixed offsets, the info dump is bounded (≤ 32 bytes, and none
for an implausible size), the hook is `@try`-guarded, and nothing blocks beyond DockRemoteCore's short
socket timeouts to audiod.

## Device facts used (phone, iPhone OS 3.1.3)

- iapd: thin armv6 `MH_EXECUTE`. Its GraphicsServices imports include `_GSSendEvent`, `_GSSendSystemEvent`
  and `_GSGetPurpleSystemEventPort`.
- Keys measured from iapd's own log: play/pause 0, next 3, prev 4, menu 22, select 23, up 24, down 25.
- Substrate: `/Library/MobileSubstrate/MobileSubstrate.dylib` is only the injection stub. `MSHook*` live in
  CydiaSubstrate (`/usr/lib/libsubstrate.dylib`). `MSHookFunction` is looked up where it is exported:
  first the images already loaded, then `libsubstrate.dylib`, `CydiaSubstrate.framework/CydiaSubstrate`
  and `MobileSubstrate.dylib`, stopping at the first that has the symbol (B1.16.5).

## Build

`IOS3_SDK=... PATH=<toolchain>/bin:$PATH ./build.sh [outdir]` produces `DockRemote.dylib` (ARMv6,
fake-signed) and copies `DockRemote.plist` (the iapd filter). MobileSubstrate and `GSSendEvent` are
resolved at run time.

## Install / remove: iapd restarts once, which drops the dock until a reseat

Install **undocked** (observe-only is safe to install):
1. Copy `DockRemote.dylib` and `DockRemote.plist` to `/Library/MobileSubstrate/DynamicLibraries/`.
2. With the phone undocked, restart iapd (`killall iapd`; launchd relaunches it) or reboot.
3. Check `/var/mobile/fidelio/remote-events.log` for the `hooked GSSendEvent ... from ...` line.
4. Dock the phone, `touch /var/mobile/fidelio/remote-tweak.observe`, press the buttons, send the log.

Remove: delete the two files (or `touch /var/mobile/fidelio/remote-tweak.disabled` to disable without
removing), then restart iapd undocked and reseat.
