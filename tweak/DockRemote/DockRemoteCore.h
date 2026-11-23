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

// The files the decision reads (the device's are under /var/mobile/fidelio; the host test uses its own).
struct Paths {
  const char* disable;  // remote-tweak.disabled, the kill switch
  const char* conf;     // settings.conf (audiod.dock_remote)
  const char* active;   // active-source (audiod: the source that owns the output, or "none")
  const char* target;   // remote-target (B1.16.16, audiod: the remote's target, or "none")
  const char* sock;     // audiod.sock
};
const Paths& devicePaths();

// Decide, cheaply and without blocking, whether to forward this key to audiod instead of the iPod app:
//   - the kill-switch file must NOT exist,
//   - the "dock_remote" setting in settings.conf must be on,
//   - B1.16.16: remote-target must name a source of ours (audiod: the active source, or the paused last
//     one until it disconnects, stops, or other audio plays). Without that file (an audiod before
//     B1.16.16), or with one older than 30 s (not maintained), active-source must name one, as before,
//   - and audiod's socket must be reachable (a file left by a dead audiod proves nothing).
// Any doubt -> false (fall through). verbForCode(code) (DockRemoteKeys.h) must be non-zero.
bool shouldForward(uint32_t code);
bool shouldForwardAt(const Paths& p, uint32_t code);  // the same with other paths (host test)
// remote-target: 1 names a source of ours, 0 "none" (or empty), -1 absent or older than 30 s.
int remoteTargetState(const Paths& p);

// Forward the verb to audiod (non-blocking, short timeout). Logs via syslog. Returns true if sent.
bool forward(uint8_t verb);

}  // namespace dockremote
