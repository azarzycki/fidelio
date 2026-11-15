#pragma once
// B1.16 part C3: the hook-INDEPENDENT core of the DockRemote MobileSubstrate tweak for iPhone OS
// 3.1.3's iapd. The hook (DockRemoteHook.mm; since B1.16.8 on GraphicsServices' GSSendEvent, where every
// accessory key passes as a type-4001 event) calls shouldForward() on each Simple Remote key; if it
// returns true the hook forwards the mapped transport verb to audiod and SWALLOWS the key (so the iPod
// app does not start), otherwise it lets the key fall through untouched.
//
// Safety: this runs inside iapd, and a crash there breaks the dock until a physical reseat. Everything
// here is non-blocking, allocation-light, pointer-checked, and reads at most two small files. The whole
// tweak is inert if /var/mobile/fidelio/remote-tweak.disabled exists (checked, cheaply, per event).
#include <stdint.h>

namespace dockremote {

// The Simple Remote context-key numbers measured on the device (iAP ContextButtonStatus bit numbers):
enum { KEY_PLAYPAUSE = 0, KEY_NEXT = 3, KEY_PREV = 4, KEY_MENU = 22, KEY_SELECT = 23, KEY_UP = 24, KEY_DOWN = 25 };

// Map a Simple Remote key to a transport verb, or 0 if this key must always fall through.
uint8_t verbForKey(int key);

// Decide, cheaply and without blocking, whether to forward this key to audiod instead of the iPod app:
//   - the kill-switch file must NOT exist,
//   - the "dock_remote" setting in settings.conf must be on,
//   - a Fidelio source must currently own audiod (active-source file is a connected source), and
//   - audiod's socket must be reachable.
// Any doubt -> false (fall through). verbForKey(key) must be non-zero.
bool shouldForward(int key);

// Forward the verb to audiod (non-blocking, short timeout). Logs via syslog. Returns true if sent.
bool forward(uint8_t verb);

}  // namespace dockremote
