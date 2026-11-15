# DockRemote (MobileSubstrate tweak, iPhone OS 3.1.3)

Forwards the dock's Simple Remote **play/pause, next, previous** keys to `fidelio-audiod` (which routes
them to the active source) instead of letting them start the built-in iPod app. Other keys (menu,
select, up, down) and volume keys always fall through. Built for use with a Philips Fidelio DS9000 dock.

**Status:** the symbol-independent core (`DockRemoteCore`) and the forwarding/safety logic are complete
and host-compiled. The hook itself (`DockRemoteHook.mm`) is **deliberately unwired** until the iapd
Simple-Remote symbol is supplied, because a crash inside `iapd` breaks the dock until the phone is
physically reseated. Do not install until the hook is wired.

## What is needed from the device to wire the hook

From the docked phone, paste as text (any one of these, nm first):

1. `nm -arch armv6 /System/Library/PrivateFrameworks/IAP.framework/Support/iapd`
   — the Objective-C method names and C++ symbols; I want `SimpleRemoteEventHandler`'s class/method
   that dispatches a context key, and `-[AccessoryManager sendEventRecord:toApp:]`.
2. `otool -oV /System/Library/PrivateFrameworks/IAP.framework/Support/iapd` — the ObjC class/method
   list, if `nm` is thin.
3. `strings -a` around `SimpleRemoteEventHandler got evKeyDown` and `sendEventRecord:toApp:`.

Prefer an Objective-C entry point (hookable by selector, no address) over an unexported C++ function.

## Behaviour (DockRemoteCore)

For each Simple Remote context key the wired hook calls `shouldForward(key)`, which returns true only
when: the kill switch `/var/mobile/fidelio/remote-tweak.disabled` is absent, `audiod.dock_remote=on`
in `settings.conf`, a Fidelio source owns audiod (`active-source` is not `none`), and audiod's socket
is reachable. On true, the hook forwards the mapped verb and **swallows** the key; otherwise it runs
the original handler untouched (press-and-hold preserved). Measured key map: play/pause=0, next=3,
prev=4 (menu=22, select=23, up=24, down=25 always fall through; volume keys are not Simple Remote).

## Install / remove (once wired) — iapd restarts once, which drops the dock until reseated

Install **undocked**:
1. Copy `DockRemote.dylib` and `DockRemote.plist` to `/Library/MobileSubstrate/DynamicLibraries/`.
2. With the phone undocked, restart iapd (`killall iapd`; launchd relaunches it) or reboot.
3. Dock the phone.

Remove: delete the two files (or `touch /var/mobile/fidelio/remote-tweak.disabled` to disable without
removing), then restart iapd undocked and reseat.
