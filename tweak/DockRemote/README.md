# DockRemote (MobileSubstrate tweak, iPhone OS 3.1.3)

Forwards the dock's Simple Remote **play/pause, next, previous** keys to `dock-audiod`/`fidelio-audiod`
(which routes them to the active source) instead of letting them start the built-in iPod app. Other
keys (menu, select, up, down) and the volume keys always fall through. Built for use with a Philips
Fidelio DS9000 dock.

## Status: WIRED, ships in OBSERVE-ONLY mode

The hook target was confirmed from the device's iapd: the Simple Remote keys arrive at the
Objective-C method **`-[SimpleRemote sendEventRecord:toApp:]`** (class `SimpleRemote : PortStack`),
so it is hooked by selector with `MSHookMessageEx` — no fragile address patching. `DockRemote.dylib`
builds as an ARMv6 `MH_DYLIB` (check PASS, 0 post-ARMv6).

It is deliberately staged so the dock is never left broken:

- **Observe-only by default.** With no flags set, the hook logs nothing and ALWAYS calls the original,
  so the dock behaves exactly as before.
- **Observe:** `touch /var/mobile/fidelio/remote-tweak.observe` — the hook then appends each record it
  sees (type, subtype, infoSize, the first payload bytes in hex, the candidate key as the first info
  int, and the target app) to `/var/mobile/fidelio/remote-events.log`, and STILL calls the original.
  Press each remote button with the phone docked and send me that log so the exact key offset is
  confirmed before anything is swallowed.
- **Swallow:** only once you also `touch /var/mobile/fidelio/remote-tweak.swallow` does it forward a
  mapped key (play/pause 0, next 3, prev 4) to audiod and swallow it (the iPod app never sees it),
  and only when a dock-receiver source owns audiod, `audiod.dock_remote=on`, and audiod is reachable.
  Confirm the observe log first; if the key is not the first info int, tell me and I adjust the decode.
- **Kill switch (no rebuild):** `touch /var/mobile/fidelio/remote-tweak.disabled` makes the tweak a
  complete no-op, checked on every event.

Safety: the hook is `@try`-guarded throughout, every pointer and `infoSize` is checked, and it does no
blocking work on iapd's thread beyond a short non-blocking connect to audiod's socket.

## Device facts used (from the phone's iapd, 3.1.3)

- `-[SimpleRemote sendEventRecord:toApp:]` IMP 0xcb4c, returns `char` (BOOL); 1st arg is a
  `GSEventRecord*` with encoding `^{?=iI{CGPoint=ff}{CGPoint=ff}IQ^viiI[0C]}`.
- Format string: `[%@ sendEventRecord:toApp:] app:'%@' type:kGSEventAccessoryKeyStateChanged key:%d keyState:%@`.
- Measured keys: play/pause 0, next 3, prev 4, menu 22, select 23, up 24, down 25; targets were
  `com.apple.mobileipod` and `com.apple.springboard`.

## Build

`IOS3_SDK=... PATH=<toolchain>/bin:$PATH ./build.sh [outdir]` — produces `DockRemote.dylib` (ARMv6,
fake-signed) and copies `DockRemote.plist` (the iapd filter). MobileSubstrate is resolved at run time.

## Install / remove — iapd restarts once, which drops the dock until a reseat

Install **undocked** (observe-only is safe to install):
1. Copy `DockRemote.dylib` and `DockRemote.plist` to `/Library/MobileSubstrate/DynamicLibraries/`.
2. With the phone undocked, restart iapd (`killall iapd`; launchd relaunches it) or reboot.
3. Dock the phone, `touch /var/mobile/fidelio/remote-tweak.observe`, press the buttons, read the log.

Remove: delete the two files (or `touch /var/mobile/fidelio/remote-tweak.disabled` to disable without
removing), then restart iapd undocked and reseat.
