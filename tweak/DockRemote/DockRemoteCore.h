#pragma once
// B1.16 part C3: the hook-INDEPENDENT core of the DockRemote MobileSubstrate tweak for iPhone OS
// 3.1.3's iapd. The hook (DockRemoteHook.mm; since B1.16.8 on GraphicsServices' GSSendEvent, where every
// accessory key passes as a type-4001 event) decodes the key and follows the press with DockRemoteKeys.h
// (B1.16.13). At the first event of a press whose code has a verb it asks shouldForward(); if that
// returns true the hook forwards the verb to audiod and SWALLOWS the whole press (so the iPod app does
// not start), otherwise it lets the press fall through untouched.
//
// Safety: this runs inside iapd, and a crash there breaks the dock until a physical reseat. Everything
// here is non-blocking, allocation-light, pointer-checked, and reads at most two small files. The whole
// tweak is inert if /var/mobile/fidelio/remote-tweak.disabled exists (checked, cheaply, per event).
#include <stdint.h>

namespace dockremote {

// Decide, cheaply and without blocking, whether to forward this key to audiod instead of the iPod app:
//   - the kill-switch file must NOT exist,
//   - the "dock_remote" setting in settings.conf must be on,
//   - a Fidelio source must currently own audiod (active-source file is a connected source), and
//   - audiod's socket must be reachable.
// Any doubt -> false (fall through). verbForCode(code) (DockRemoteKeys.h) must be non-zero.
bool shouldForward(uint32_t code);

// Forward the verb to audiod (non-blocking, short timeout). Logs via syslog. Returns true if sent.
bool forward(uint8_t verb);

}  // namespace dockremote
