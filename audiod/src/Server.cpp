#include "Server.h"

#include <math.h>


#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include "OtherAudio.h"
#include "ios3_trace.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

namespace audiod {

volatile sig_atomic_t Server::stopRequested = 0;
void Server::requestStop(int) { stopRequested = 1; }

uint64_t Server::nowMs() {
#ifdef __APPLE__
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  return (t / tb.denom) * tb.numer / 1000000ull + ((t % tb.denom) * tb.numer / tb.denom) / 1000000ull;
#else
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

// true as soon as one |sample| exceeds thr (early exit: music returns within a few samples)
static bool peakExceeds(const int16_t* s, size_t n, int thr) {
  for (size_t i = 0; i < n; i++) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > thr) return true; }
  return false;
}

Server::Server(const ServerOptions& o, const Tunables& tn, AudioOutput* output, HardwareVolume* hwv, DisplayWaker* w)
    : opts(o), t(tn), arbiter(tn), out(output), hw(hwv), waker(w) {
  startMs = nowMs();
  leadFrames = (size_t)opts.leadMs * 44100 / 1000;
  if (leadFrames > kRingFrames) leadFrames = kRingFrames;
  if (!opts.eventLog.empty()) eventFile = fopen(opts.eventLog.c_str(), "w");
  if (const char* e = getenv("AUDIOD_STALL_ACTIVE_MS")) stallActiveMs = (uint64_t)atoll(e);
  if (const char* e = getenv("AUDIOD_DOCK_FORWARD_MIN_MS")) dockMinMs = (uint64_t)atoll(e);
  // B1.9 A: absolute Spotify volume needs the hardware API; read the level once (never set at startup)
  if (hw) {
    hwAvailable = hw->available() && hw->get(hwLevel);
    if (!hwAvailable) hwLevel = -1;
  }
  if (opts.absoluteVolume) {
    if (absoluteActive())
      IOS3_TRACE("spotify volume: absolute (the slider drives the hardware; max %d %%, curve %s): %s, level now %.3f (slider %u)", opts.maxVolumePct,
                 opts.hwCurve == 1 ? "log" : opts.hwCurve == 2 ? "cubic" : "linear", hw->describe().c_str(), hwLevel, levelToSlider(hwLevel));
    else
      IOS3_TRACE("spotify volume: absolute requested but the hardware volume is unavailable (%s): falling back to software gain",
                 hw ? hw->describe().c_str() : "no backend");
  } else {
    IOS3_TRACE("spotify volume: software gain (B1.7 curve applied by the producer)");
  }
  IOS3_TRACE("airplay volume: %s (B1.13)%s", opts.airplayAbsolute ? "absolute" : "software",
             opts.airplayAbsolute && !hwReady() ? " requested but hardware unavailable -> software fallback" : "");
  IOS3_TRACE("dock volume (B1.16.12): %s", hw ? hw->describeDock().c_str() : "unavailable (no hardware volume backend)");
  if (opts.statusDir.empty())
    IOS3_TRACE("remote target (B1.16.16): no status dir, so no remote-target file (the dock remote falls back to active-source)");
  else
    IOS3_TRACE("remote target (B1.16.16): %s/remote-target (the active source, or the paused last one until it disconnects, stops or other audio plays; other audio: %s, polled every %llu ms while paused)",
               opts.statusDir.c_str(), otherAudioDescribe().c_str(), (unsigned long long)kOtherAudioPollMs);
  IOS3_TRACE("wake-on-play: %s%s%s", opts.wakeOnPlay && waker ? "on (" : "off", opts.wakeOnPlay && waker ? waker->describe().c_str() : "",
             opts.wakeOnPlay && waker ? (", mode " + std::string(opts.wakeLaunch ? "launch, app " + opts.wakeBundle : "screen only") + ")").c_str() : "");
  IOS3_TRACE("unlock-on-play: %s (url %s; %s)", opts.unlockOnPlay ? "on" : "off", opts.unlockUrl.c_str(),
             waker && waker->canUnlock() ? "SBSOpenSensitiveURLAndUnlock available" : "SBSOpenSensitiveURLAndUnlock NOT available: a locked device stays locked");
}

// ---------------------------------------------------------------- B1.9 A: absolute volume

float Server::sliderToLevel(uint16_t slider) const {
  if (slider == 0) return 0.f;
  float x = slider / 65535.f;
  if (opts.hwCurve == 1) return powf(10.f, (x - 1.f) * 60.f / 20.f);
  if (opts.hwCurve == 2) return x * x * x;
  return x;
}

uint16_t Server::levelToSlider(float level) const {
  if (!(level > 0.f)) return 0;
  if (level > 1.f) level = 1.f;
  float x;
  if (opts.hwCurve == 1) x = 1.f + 20.f * log10f(level) / 60.f;
  else if (opts.hwCurve == 2) x = cbrtf(level);
  else x = level;
  if (x < 0.f) x = 0.f;
  if (x > 1.f) x = 1.f;
  return (uint16_t)(x * 65535.f + 0.5f);
}

void Server::sendToClient(Client& c, uint32_t type, const void* payload, size_t len) {
  if (c.version < AUDIOD_PROTO_VERSION_2) return;  // a v1 producer never reads: nothing may pile up in its buffer
  uint8_t buf[64];
  AudiodMsgHeader h = { type, (uint32_t)len };
  memcpy(buf, &h, sizeof h);
  memcpy(buf + sizeof h, payload, len);
  int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  ssize_t n = send(c.fd, buf, sizeof h + len, flags);
  if (n != (ssize_t)(sizeof h + len) && !c.sendFailedLogged) {
    IOS3_TRACE("client #%d '%s': could not send message %u (%s); dropped", c.id, c.name.c_str(), type, n < 0 ? strerror(errno) : "short");
    c.sendFailedLogged = true;
  }
}

void Server::sendWelcome(Client& c) {
  AudiodWelcome w;
  memset(&w, 0, sizeof w);
  w.version = AUDIOD_PROTO_VERSION_2;
  bool abs = absoluteActiveFor(c.name);
  w.flags = abs ? AUDIOD_VOL_ABSOLUTE : 0;
  w.slider = abs ? levelToSlider(hwLevel) : 0;
  sendToClient(c, AUDIOD_MSG_WELCOME, &w, sizeof w);
  if (abs) {
    arbiter.noteVolumeReport(c.id, nowMs());  // the connect-time level counts as a report
    noteReport(c, hwLevel, nowMs());          // B1.16.14: and its echo is recognised
  }
}

// B1.10: send the current hardware level to ONE source (on takeover, or the active source on a
// change). Reporting it to a BACKGROUND source is what let a volume-button press make a backgrounded
// Spotify steal the output (its controller replied to our volume notify with a track reload -> FLUSH),
// so a non-active source is told only when it next becomes active. The report arms the arbiter grace.
void Server::sendHwVolumeTo(Client& c) {
  if (c.version < AUDIOD_PROTO_VERSION_2 || !absoluteActiveFor(c.name)) return;
  AudiodHwVolume v = { levelToSlider(hwLevel), AUDIOD_VOL_ABSOLUTE };
  sendToClient(c, AUDIOD_MSG_HWVOLUME, &v, sizeof v);
  arbiter.noteVolumeReport(c.id, nowMs());
  noteReport(c, hwLevel, nowMs());  // B1.16.14
}

// B1.16.14: a sender may send a reported level straight back as a volume command (device-found: an
// AirPlay sender echoes each DACP dmcp.device-volume as SET_PARAMETER volume). Several reports can go
// out in quick succession and their echoes come back in order, later, possibly after newer reports or
// changes, so a few are kept, each with its time.
void Server::noteReport(Client& c, float level, uint64_t now) {
  if (!(level >= 0)) return;
  c.reports[c.reportNext % (sizeof c.reports / sizeof c.reports[0])] = Client::Report{level, now};
  c.reportNext++;
}

static const float kEchoTolerance = 1.0f / 255.0f + 0.00001f;  // one dock step (the dock's 0..255 scale)

long Server::echoAge(const Client& c, float level, uint64_t now) const {
  long best = -1;
  for (const Client::Report& r : c.reports) {
    if (r.ms == 0 || now - r.ms > kEchoWindowMs || fabsf(level - r.level) > kEchoTolerance) continue;
    long age = (long)(now - r.ms);
    if (best < 0 || age < best) best = age;
  }
  return best;
}

void Server::broadcastHwVolume() {
  // B1.10: only the ACTIVE source is told now; others are told when they next become active
  // (sendHwVolumeTo on takeover), so a background source can never act on a volume change.
  if (Client* c = clientById(arbiter.activeId())) sendHwVolumeTo(*c);
}

// A user moved the slider of a slider-driven source: set the hardware (clamped), log it, and if the
// clamp bit, tell every v2 client the level it actually got so the controller's slider snaps there.
void Server::applySlider(Client& c, uint16_t slider) {
  float want = sliderToLevel(slider);
  float ceiling = opts.maxVolumePct / 100.f;
  float level = want > ceiling ? ceiling : want;
  bool clamped = want > ceiling + 0.0005f;
  // B1.16.14: the echo of a level we reported to this source is not a user move: applying a late one
  // would undo a newer change (the dock's own buttons) and send that old level to the dock. A clamped
  // request is left to the clamp path (its report is what snaps the controller back to the ceiling).
  if (!clamped) {
    long age = echoAge(c, want, nowMs());
    if (age >= 0) {
      IOS3_TRACE("hwvolume: '%s' echoed %.3f (reported %ld ms ago): ignored", c.name.c_str(), want, age);
      return;
    }
  }
  if (hwLevel >= 0 && fabsf(level - hwLevel) < 0.002f && !clamped) return;  // already there (the controller echoing our report)
  bool ok = hw->set(level);
  if (ok) hwLevel = level;
  // B1.16.12: the dock hears of it only through iapd. Only after a set that worked, with the same level.
  const char* dock = ok ? forwardToDock(level, nowMs()) : nullptr;
  char reason[192];
  snprintf(reason, sizeof reason, "set %.3f by '%s' slider %u%s%s; dock=%s", level, c.name.c_str(), slider, clamped ? " clamped from " : "",
           clamped ? std::to_string(want).substr(0, 5).c_str() : "", dock ? dock : "?");
  if (!ok) snprintf(reason, sizeof reason, "set FAILED (%.3f) by '%s' slider %u", level, c.name.c_str(), slider);
  logEvent(Event{Event::HWVOLUME, nowMs(), c.name, ok ? std::to_string(level).substr(0, 5) : "?", reason});
  if (ok && clamped) broadcastHwVolume();
}

// Every pollIntervalMs: pick up a change made elsewhere (the phone's buttons, another client) and
// report it to the v2 producers so their controllers show the true level.
void Server::pollHardwareVolume(uint64_t now) {
  if (!hwReady()) return;
  if (now - lastHwPollMs < hw->pollIntervalMs()) return;
  lastHwPollMs = now;
  float l = -1;
  bool changed = hw->takeChange(l);
  if (!changed) {
    float cur = -1;
    if (hw->get(cur) && cur >= 0 && (hwLevel < 0 || fabsf(cur - hwLevel) >= 0.002f)) { l = cur; changed = true; }
  }
  if (!changed || !(l >= 0)) return;
  if (hwLevel >= 0 && fabsf(l - hwLevel) < 0.002f) return;
  hwLevel = l;
  if (dockPending) {  // B1.16.12: a held source level is older than this change: never send it now
    IOS3_TRACE("hwvolume: held dock level %.3f dropped (%d held): the level changed outside", dockPendingLevel, dockHeld);
    dockPending = false;
    dockHeld = 0;
  }
  logEvent(Event{Event::HWVOLUME, now, "external", std::to_string(l).substr(0, 5), "hardware level changed (buttons / another app): reporting slider " + std::to_string(levelToSlider(l))});
  broadcastHwVolume();
}

// B1.16.12: tell the dock (IAPAudioSetVolume through the backend). Two synchronous MIG round trips to
// iapd (the "supported?" question, then the set) on the run-loop thread, so a slider drag is coalesced:
// one call per dockMinMs at most, the latest level of a drag going out when the window ends.
const char* Server::forwardToDock(float level, uint64_t now) {
  if (!hw->dockAvailable()) return HardwareVolume::dockResultName(HardwareVolume::DOCK_UNAVAILABLE);  // no IPC at all
  if (dockCalled && now - lastDockMs < dockMinMs) {
    dockPending = true;
    dockPendingLevel = level;
    dockHeld++;
    return "deferred";
  }
  return HardwareVolume::dockResultName(sendToDock(level, now));
}

HardwareVolume::DockResult Server::sendToDock(float level, uint64_t now) {
  HardwareVolume::DockResult r = hw->tellDock(level);
  lastDockMs = now;
  dockCalled = true;
  dockPending = false;
  dockHeld = 0;
  return r;
}

void Server::flushDockForward(uint64_t now) {
  if (!dockPending || now - lastDockMs < dockMinMs) return;
  float level = dockPendingLevel;
  int held = dockHeld;
  // B1.16.14: the level may have changed outside since the set (the dock's buttons) without the poll
  // having seen it yet: read it again, and never send a held level older than that change. The normal
  // external path (pollHardwareVolume) reports the new level.
  float cur = -1;
  if (hwLevel >= 0 && hw->get(cur) && cur >= 0 && fabsf(cur - hwLevel) >= 0.002f) {
    IOS3_TRACE("hwvolume: held dock level %.3f dropped (%d held): the level changed outside (now %.3f, not yet reported)", level, held, cur);
    dockPending = false;
    dockHeld = 0;
    return;
  }
  HardwareVolume::DockResult r = sendToDock(level, now);
  IOS3_TRACE("hwvolume: dock told %.3f, the latest of %d held set%s: dock=%s", level, held, held == 1 ? "" : "s", HardwareVolume::dockResultName(r));
}

// ---------------------------------------------------------------- B1.9 B: wake on play

// B1.11: enable the lock-screen bundle for a playback start from nothing (onStartFromNone decides
// when), and disable it when nothing is active any more. dlopen/dlsym; no-op if the flag is off or the
// API is unavailable.
void Server::enableLockPlugin(const std::string& source, uint64_t atMs, const char* why) {
  if (opts.lockPlugin.empty() || !waker || lockEnabled) return;
  if (waker->enableLockBundle(opts.lockPlugin)) {
    lockEnabled = true;
    lockEnabledName = opts.lockPlugin;
    sbPidAtEnable = waker->springBoardPid();  // B1.16.11: to notice a SpringBoard restart later
    sbNewSeenMs = 0;
    logEvent(Event{Event::WAKE, atMs, "none", source, "lock-plugin enabled " + opts.lockPlugin + (why ? std::string(" (") + why + ")" : "")});
  }
}

void Server::disableLockPlugin(const char* why) {
  if (!lockEnabled || !waker) return;
  waker->disableLockBundle(lockEnabledName);  // the name it was enabled under, even if the setting changed
  lockEnabled = false;
  sbPidAtEnable = 0;
  sbNewSeenMs = 0;
  logEvent(Event{Event::WAKE, nowMs(), "", "none", "lock-plugin disabled " + lockEnabledName + (why ? std::string(" (") + why + ")" : "")});
}

// B1.16.11: onStartFromNone decides about the plugin when playback starts. This covers what can change
// while a source keeps playing.
//  - The lock_plugin setting changed live: on -> enable now (unless audiod unlocked the device for this
//    start: the agreed rule); off -> disable now. Edge-triggered on the setting, so a failing SpringBoard
//    call is never retried in a loop.
//  - SpringBoard restarted (a respring forgets an enabled bundle while audiod still counts it as on):
//    SpringBoard's pid is polled every kSbCheckMs while the plugin is enabled, and once a new SpringBoard
//    has been up for kSbSettleMs the bundle is enabled again. Device-found on the iPad: after a respring
//    while playing, the plugin was not shown until the next start from nothing.
void Server::syncLockPlugin(uint64_t now) {
  if (!waker) return;
  std::string a = arbiter.activeName();
  int settingOn = opts.lockPlugin.empty() ? 0 : 1;
  if (settingOn != lockSettingSeen) {
    bool first = lockSettingSeen < 0;
    lockSettingSeen = settingOn;
    if (!first && !a.empty()) {
      if (!settingOn && lockEnabled) disableLockPlugin("turned off while playing");
      else if (settingOn && !lockEnabled && !unlockedThisStart) enableLockPlugin(a, now, "turned on while playing");
    }
  }
  if (!lockEnabled || a.empty() || now - lastSbCheckMs < kSbCheckMs) return;
  lastSbCheckMs = now;
  int pid = waker->springBoardPid();
  if (pid <= 0) return;  // unknown (host build) or SpringBoard is down at this moment
  if (sbPidAtEnable <= 0) { sbPidAtEnable = pid; return; }
  if (pid == sbPidAtEnable) { sbNewSeenMs = 0; return; }
  if (!sbNewSeenMs) { sbNewSeenMs = now; return; }  // a new SpringBoard: let it finish starting
  if (now - sbNewSeenMs < kSbSettleMs) return;
  bool ok = waker->enableLockBundle(lockEnabledName);
  logEvent(Event{Event::WAKE, now, "none", a, "lock-plugin re-enabled " + lockEnabledName + " after a SpringBoard restart" + (ok ? "" : " (FAILED)")});
  sbPidAtEnable = pid;
  sbNewSeenMs = 0;
}

// B1.14: once a second, if settings.conf changed, apply the live options to the running config and
// rewrite the manifest so the app shows the new effective values.
void Server::applyLiveConfig() {
  // B1.15: the registry's write() callbacks have set opts/t directly. Mirror the tunables into the
  // arbiter and recompute values derived from an option (leadFrames from leadMs).
  arbiter.setTunables(t);
  leadFrames = (size_t)opts.leadMs * 44100 / 1000;
  if (leadFrames > kRingFrames) leadFrames = kRingFrames;
}

void Server::maybeReloadSettings(uint64_t now) {
  if (!settings || settingsConf.empty()) return;
  if (now - lastSettingsCheckMs < 1000) return;
  lastSettingsCheckMs = now;
  if (!settings->reloadIfChanged(settingsConf)) return;  // write() already applied file values to opts/t
  applyLiveConfig();                                      // push tunables into the arbiter, recompute derived
  settings->refreshValues();                             // manifest reflects the live config
  if (!settingsDir.empty()) settings->writeManifest(settingsDir);
  IOS3_TRACE("settings.conf changed: applied live (max_volume=%d crossfade=%u wake=%d wake_mode=%s unlock=%d preempt=%d spotify_abs=%d airplay_abs=%d)",
             opts.maxVolumePct, opts.crossfadeMs, (int)opts.wakeOnPlay, opts.wakeLaunch ? "launch" : "screen", (int)opts.unlockOnPlay,
             (int)opts.preemptPause, (int)opts.absoluteVolume, (int)opts.airplayAbsolute);
}

// B1.9 B + B1.16.4 + B1.11, for a playback start from nothing (not a switch, not a reconnect).
// Order: when an unlock may be attempted, the wake (and unlock) comes first and the lock plugin is
// enabled only if audiod does not unlock the device: never enabled and then immediately unlocked
// past. When no unlock can happen, the B1.11 order stays: plugin first, so a lock screen that lights
// up already shows the track. B1.16.10: wake_mode=screen never unlocks, so it always takes that order.
void Server::onStartFromNone(const Event& e) {
  if (e.kind != Event::TAKEOVER || !e.from.empty() || e.reason == "reconnect") return;
  bool unlockPossible = opts.wakeOnPlay && opts.wakeLaunch && waker && opts.unlockOnPlay && waker->canUnlock();
  if (!unlockPossible) enableLockPlugin(e.to, e.atMs);
  WakeResult w = maybeWake(e);
  unlockedThisStart = (w == WAKE_UNLOCKED);  // B1.16.11: syncLockPlugin keeps the plugin off for this start then
  if (unlockPossible && w != WAKE_UNLOCKED) enableLockPlugin(e.to, e.atMs);  // not unlocked by us: plugin as before
}

Server::WakeResult Server::maybeWake(const Event& e) {
  if (!opts.wakeOnPlay || !waker) return WAKE_NOT_RUN;
  if (lastWakeMs && e.atMs - lastWakeMs < opts.wakeMinIntervalMs) {
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "debounced (" + std::to_string(e.atMs - lastWakeMs) + " ms since the last wake)"});
    return WAKE_NOT_RUN;
  }
  lastWakeMs = e.atMs;
  if (!opts.wakeLaunch) {
    // B1.16.10 wake_mode=screen: light the screen only. On a locked device the lock screen (with the
    // lock plugin, enabled before this by onStartFromNone) shows the track; nothing is launched or unlocked.
    waker->wakeScreen();
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake (screen only)"});
    return WAKE_DONE;
  }
  int rc = waker->wake(opts.wakeBundle);
  if (rc == DisplayWaker::kLaunchOk) {
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake+launch " + opts.wakeBundle});
    return WAKE_DONE;
  }
  if (rc != DisplayWaker::kLaunchLocked) {
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake FAILED " + opts.wakeBundle + " (launch " + std::to_string(rc) + ")"});
    return WAKE_DONE;
  }
  // B1.16.4: the launch was refused because the device is locked.
  if (!opts.unlockOnPlay) {
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake: device locked, unlock-on-play off: staying on the lock screen"});
    return WAKE_DONE;
  }
  if (!waker->canUnlock()) {
    if (!unlockUnavailableLogged) {
      IOS3_TRACE("unlock-on-play: SBSOpenSensitiveURLAndUnlock is not available on this system: a locked device stays locked (logged once)");
      unlockUnavailableLogged = true;
    }
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake: device locked, unlock unavailable: staying on the lock screen"});
    return WAKE_DONE;
  }
  if (waker->openURLAndUnlock(opts.unlockUrl) != 1) {
    // One request per playback start, never a retry loop: a refusal is logged once and left alone.
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake: device locked, unlock request failed (" + opts.unlockUrl + "): staying on the lock screen"});
    return WAKE_DONE;
  }
  // B1.16.6: SpringBoard accepted the request, and that is the result. On the locked 5.1.1 iPad an
  // accepted nowplaying: request unlocked it and put NowPlaying in front within 2 s (SpringBoard's own
  // isLocked read 0 at 2, 6 and 12 s), while SBSLaunchApplicationWithIdentifier kept answering 3
  // ("device locked") for the whole 5 s that B1.16.4 retried it: that probe reported a false "not
  // confirmed" and enabled the lock plugin on an unlocked device. A refusal comes back as false (an
  // unentitled caller, for one), so true is SpringBoard's answer, not just delivery. Agreed rule: audiod
  // unlocked the device and the app is in front (it disables the idle timer), so the lock plugin stays
  // off for this start.
  logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "wake: device locked, unlock accepted (" + opts.unlockUrl + ")"});
  return WAKE_UNLOCKED;
}

Server::~Server() {
  for (auto& kv : clients) close(kv.first);
  if (listenFd >= 0) { close(listenFd); unlink(opts.sockPath.c_str()); }
  if (eventFile) fclose(eventFile);
}

bool Server::listen() {
  listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenFd < 0) return false;
  // Accepted sockets inherit this; set on the listener so a reconnecting producer never starts on
  // the default (small) buffer. ~1.5 s of PCM the kernel can hold while this loop is in a slow
  // CoreAudio call. The kernel may clamp it (kern.ipc.maxsockbuf): the effective size is logged.
  // Deliberately small (B1.5): with backpressure the active producer blocks on this buffer, and the
  // total downstream of raop's jitter buffer (sink 0.5 s + kernel buffers) must stay well below its
  // latency or the jitter buffer can never hold a cushion. 8 KB = 46 ms per side (Linux doubles it).
  int rcvbuf = 8 * 1024;
  setsockopt(listenFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, opts.sockPath.c_str(), sizeof a.sun_path - 1);
  unlink(opts.sockPath.c_str());  // a stale socket from a previous instance
  if (bind(listenFd, (sockaddr*)&a, sizeof a) != 0) { IOS3_TRACE("bind %s: %s", opts.sockPath.c_str(), strerror(errno)); return false; }
  chmod(opts.sockPath.c_str(), 0660);
  if (::listen(listenFd, 8) != 0) return false;
  IOS3_TRACE("listening on %s (gap %llu, takeover %llu, hold %llu, release %llu, quiet-release %llu, idle-stop %llu ms; silence threshold %d; lead %u ms)",
             opts.sockPath.c_str(), (unsigned long long)arbiter.tunables().producingGapMs, (unsigned long long)arbiter.tunables().takeoverMs,
             (unsigned long long)arbiter.tunables().holdMs, (unsigned long long)arbiter.tunables().releaseMs,
             (unsigned long long)arbiter.tunables().quietReleaseMs, (unsigned long long)arbiter.tunables().idleStopMs,
             arbiter.tunables().silenceThreshold, opts.leadMs);
  IOS3_TRACE("backpressure on the active source: %s; crossfade %u ms", opts.backpressure ? "on" : "off (b14 behaviour)", opts.crossfadeMs);
  writeActiveSourceFile("none");
  writeRemoteTargetFile(true);  // B1.16.16: "none" until a source plays (a leftover of an earlier audiod is replaced)
  return true;
}

void Server::acceptClient() {
  int fd = accept(listenFd, nullptr, nullptr);
  if (fd < 0) return;
  int rcvbuf = 8 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);  // in case the listener's did not carry over
  int eff = 0; socklen_t el = sizeof eff;
  getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &eff, &el);
  Client c;
  c.fd = fd;
  c.id = nextId++;
  c.ring.resize(kRingFrames * 2);
  int id = c.id;
  clients[fd] = std::move(c);
  IOS3_TRACE("client #%d connected (fd %d, SO_RCVBUF %d)", id, fd, eff);
}

void Server::dropClient(int fd, const char* why) {
  auto it = clients.find(fd);
  if (it == clients.end()) return;
  IOS3_TRACE("client #%d '%s' dropped: %s%s", it->second.id, it->second.name.c_str(), why,
             it->second.overflowFrames ? " (while active it ran ahead of the output; oldest frames were dropped)" : "");
  if (it->second.hello) arbiter.removeSource(it->second.id, nowMs());
  close(fd);
  clients.erase(it);
  afterTick();
}

void Server::readClient(Client& c) {
  uint8_t buf[16384];
  ssize_t n = recv(c.fd, buf, sizeof buf, 0);
  if (n <= 0) { dropClient(c.fd, n == 0 ? "closed" : strerror(errno)); return; }
  c.parser.feed(buf, (size_t)n);
  Message m;
  int fd = c.fd;
  while (clients.count(fd) && c.parser.next(m)) handle(c, m);
  if (clients.count(fd) && !c.parser.error().empty()) dropClient(fd, c.parser.error().c_str());
}

void Server::handle(Client& c, const Message& m) {
  uint64_t now = nowMs();
  if (!c.hello) {
    AudiodHello h;
    std::string why;
    if (!parseHello(m, h, why)) { dropClient(c.fd, ("bad HELLO: " + why).c_str()); return; }
    c.hello = true;
    c.name = helloName(h);
    c.version = h.version;
    if (c.name == "control") {  // B1.16: a transport controller, not a producer -> not an arbiter source
      c.controller = true;
      IOS3_TRACE("client #%d is a transport controller (protocol v%u)", c.id, h.version);
      return;
    }
    bool back = arbiter.addSource(c.id, c.name, h.priority, now);
    IOS3_TRACE("client #%d is source '%s' (priority %u, protocol v%u)%s", c.id, c.name.c_str(), h.priority, h.version, back ? ", reconnected: same stream" : "");
    sendWelcome(c);  // v2 producers learn whether the slider drives the hardware, and the current level
    afterTick();  // a reconnecting active source is active again right away
    return;
  }
  switch (m.type) {
    case AUDIOD_MSG_AUDIO: {
      if (m.payload.size() % 4 != 0) { dropClient(c.fd, "AUDIO length not a multiple of a frame"); return; }
      const int16_t* pcm = reinterpret_cast<const int16_t*>(m.payload.data());
      int thr = arbiter.tunables().silenceThreshold;
      bool loud = thr <= 0 || peakExceeds(pcm, m.payload.size() / 2, thr);
      ringAppend(c, pcm, m.payload.size() / 4);
      arbiter.onAudio(c.id, m.payload.size(), now, loud);
      afterTick();
      break;
    }
    case AUDIOD_MSG_VOLUME: {
      float linear;
      uint16_t slider, flags;
      if (!parseVolume(m, linear, slider, flags)) { dropClient(c.fd, "bad VOLUME length"); return; }
      if (flags & AUDIOD_VOL_SLIDER) {
        // B1.9 A: a slider-driven source. Absolute mode: unity gain at the output, the slider sets the
        // hardware (only on an APPLY = a user move; the startup echo is report-only). Software mode
        // (or no hardware API): the producer's curved gain, exactly B1.7.
        c.sliderDriven = true;
        c.slider = slider;
        if (absoluteActiveFor(c.name)) {
          c.volume = 1.0f;
          if (flags & AUDIOD_VOL_APPLY) applySlider(c, slider);
        } else {
          c.volume = linear;
        }
      } else {
        c.sliderDriven = false;
        c.volume = linear;
      }
      if (arbiter.activeId() == c.id) out->setVolume(c.volume);
      break;
    }
    case AUDIOD_MSG_FLUSH:
      if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: FLUSH from #%d (had %zu frames, head %zu)", c.id, c.ringFrames, c.ringHead);
      c.ringHead = 0; c.ringFrames = 0;
      arbiter.onFlush(c.id);
      if (arbiter.activeId() == c.id) out->flush();
      break;
    case AUDIOD_MSG_STOP:
      if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: STOP from #%d (%zu frames in ring)", c.id, c.ringFrames);
      arbiter.onStop(c.id, now);
      // B1.16.16: STOP means no more audio for a while (raop: the AirPlay session was torn down; cspot:
      // the playlist ended): nothing left to resume, so a paused source that stops is no longer the target.
      if (remoteTarget == c.name && arbiter.activeName() != c.name) setRemoteTarget("", "'" + c.name + "' sent STOP: nothing to resume");
      afterTick();
      break;
    case AUDIOD_MSG_CONTROL:
      handleControl(c, m);
      break;
    case AUDIOD_MSG_META:
      break;  // v2: unified now-playing status
    case AUDIOD_MSG_HELLO:
      dropClient(c.fd, "second HELLO");
      return;
  }
}

void Server::logEvent(const Event& e) {
  static const char* names[] = { "takeover", "release", "idle-stop", "output-start", "wake", "hwvolume" };
  const char* k = names[e.kind];
  time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char wall[32];
  strftime(wall, sizeof wall, "%H:%M:%SZ", &tmv);
  IOS3_TRACE("%s +%llums %s: '%s' -> '%s' (%s)", wall, (unsigned long long)(e.atMs - startMs), k, e.from.empty() ? "none" : e.from.c_str(), e.to.empty() ? "none" : e.to.c_str(),
             e.reason.c_str());
  if (eventFile) {
    fprintf(eventFile, "%llu %s %s %s %s\n", (unsigned long long)(e.atMs - startMs), k, e.from.empty() ? "none" : e.from.c_str(),
            e.to.empty() ? "none" : e.to.c_str(), e.reason.c_str());
    fflush(eventFile);
  }
}

// Write "<line>\n" to a temporary file and rename it into place: a reader sees the old or the new
// content, never a partial one. False if any step failed (the caller then tries again next pass).
bool Server::writeAtomicLine(const std::string& path, const std::string& line) {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  bool ok = fprintf(f, "%s\n", line.c_str()) > 0;
  ok = fclose(f) == 0 && ok;
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
  return true;
}

void Server::writeActiveSourceFile(const std::string& name) {
  if (opts.statusDir.empty()) return;
  uint64_t now = nowMs();
  if (name == lastActiveWritten && now - lastActiveWriteMs < 10000) return;  // rewritten every 10 s so a reader can tell a dead audiod by the mtime
  lastActiveWriteMs = now;
  // B1.16.16: remembered as written only when the rename worked (before, a failed write was taken as
  // done and not retried until the 10 s rewrite).
  if (writeAtomicLine(opts.statusDir + "/active-source", name)) lastActiveWritten = name;
}

// B1.16.16: the dock remote's target, read by DockRemote (take or pass a key) and by NowPlaying (keep
// showing a paused source). Written at once when it changes, and again every 10 s like active-source.
void Server::writeRemoteTargetFile(bool force) {
  if (opts.statusDir.empty()) return;
  std::string name = remoteTarget.empty() ? "none" : remoteTarget;
  uint64_t now = nowMs();
  if (!force && name == remoteTargetWritten && now - remoteTargetWriteMs < 10000) return;
  remoteTargetWriteMs = now;
  if (writeAtomicLine(opts.statusDir + "/remote-target", name)) remoteTargetWritten = name;
}

void Server::setRemoteTarget(const std::string& name, const std::string& why) {
  if (name == remoteTarget) return;
  IOS3_TRACE("remote target: '%s' -> '%s' (%s)", remoteTarget.empty() ? "none" : remoteTarget.c_str(), name.empty() ? "none" : name.c_str(), why.c_str());
  remoteTarget = name;
  writeRemoteTargetFile(true);
}

// Called from afterTick, after every message and every run-loop pass.
void Server::updateRemoteTarget() {
  std::string a = arbiter.activeName();
  if (!a.empty()) {
    if (a != remoteTarget) setRemoteTarget(a, remoteTarget.empty() ? "active" : "'" + a + "' took over");
  } else if (!remoteTarget.empty()) {
    const Client* c = clientByName(remoteTarget);
    if (!c || c->controller) setRemoteTarget("", "'" + remoteTarget + "' disconnected");
  }
  writeRemoteTargetFile(false);  // the periodic rewrite
}

// While a paused source of ours is the target (nothing of ours active): audio that is not ours (the
// built-in Music app, another app) takes the remote back for good, until one of our sources plays again.
void Server::checkOtherAudio(uint64_t now) {
  if (remoteTarget.empty() || arbiter.activeId() >= 0) return;
  if (now - lastOtherAudioCheckMs < kOtherAudioPollMs) return;
  lastOtherAudioCheckMs = now;
  if (otherAudioPlaying()) setRemoteTarget("", "other audio is playing (not ours): the remote's keys go back to the system");
}

static const char* ctrlName(uint8_t v) {
  switch (v) { case AUDIOD_CTRL_PLAY: return "play"; case AUDIOD_CTRL_PAUSE: return "pause";
    case AUDIOD_CTRL_TOGGLE: return "toggle"; case AUDIOD_CTRL_NEXT: return "next"; case AUDIOD_CTRL_PREV: return "prev"; }
  return "?";
}

void Server::handleControl(Client& c, const Message& m) {
  if (m.payload.size() < sizeof(AudiodControl)) { IOS3_TRACE("control: short payload from #%d, ignored", c.id); return; }
  AudiodControl ctl; memcpy(&ctl, m.payload.data(), sizeof ctl);
  Client* target = clientById(arbiter.activeId());  // the active source acts on the verb
  bool resume = false, paused = false;
  if (!target && !remoteTarget.empty()) {
    // B1.16.16: nothing active, but a paused source of ours is the remote's target: every verb goes to
    // it. TOGGLE (the remote's play/pause) and PLAY resume it, sent as PLAY: it is paused, and PLAY is a
    // no-op for a source that is in fact still playing (a silent gap), where TOGGLE would pause it.
    target = clientByName(remoteTarget);
    paused = target != nullptr;
    if (paused && (ctl.verb == AUDIOD_CTRL_PLAY || ctl.verb == AUDIOD_CTRL_TOGGLE)) { ctl.verb = AUDIOD_CTRL_PLAY; resume = true; }
  }
  if (!target && (ctl.verb == AUDIOD_CTRL_PLAY || ctl.verb == AUDIOD_CTRL_TOGGLE)) {
    // Nothing is playing: PLAY/TOGGLE resumes the most-recently-active source if it is still connected
    // (e.g. a Spotify that B1.12 paused on preemption). Send it PLAY.
    target = lastActiveName.empty() ? nullptr : clientByName(lastActiveName);
    resume = target != nullptr;
    if (resume) ctl.verb = AUDIOD_CTRL_PLAY;
  }
  if (!target) { IOS3_TRACE("control: %s -> no active source%s, ignored", ctrlName(ctl.verb),
                            lastActiveName.empty() ? "" : (" (last '" + lastActiveName + "' gone)").c_str()); return; }
  if (target->version < AUDIOD_PROTO_VERSION_2) { IOS3_TRACE("control: %s -> '%s' ignored (v1 source cannot be controlled)", ctrlName(ctl.verb), target->name.c_str()); return; }
  sendToClient(*target, AUDIOD_MSG_CONTROL, &ctl, sizeof ctl);
  IOS3_TRACE("control: %s -> '%s'%s%s", ctrlName(ctl.verb), target->name.c_str(), resume ? " (resume)" : "", paused ? " (paused remote target)" : "");
}

void Server::afterTick() {
  for (const Event& e : arbiter.drainEvents()) {
    logEvent(e);
    onStartFromNone(e);  // B1.9 B wake, B1.16.4 unlock, B1.11 lock plugin: none -> source only
    schedulePreempt(e);  // B1.12: a source -> source switch -> tell the loser to pause
    if (e.kind == Event::TAKEOVER) {
      out->crossfade(fadeFrames());  // the old source's tail ramps out under the new one's ramp-in; its lead is dropped
      lastSilenceMs = 0;
      // Start with the new source's lead (the latest leadFrames it sent); anything older is stale.
      if (Client* c = clientById(arbiter.activeId())) {
        if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: takeover #%d with %zu frames (head %zu)", c->id, c->ringFrames, c->ringHead);
        if (c->ringFrames > leadFrames) { c->ringHead = (c->ringHead + c->ringFrames - leadFrames) % kRingFrames; c->ringFrames = leadFrames; }
        out->setVolume(c->volume);
        sendHwVolumeTo(*c);  // B1.10: it is active now -> give it the current hardware level
      }
    }
    if (e.kind == Event::RELEASE) {
      // "quiet" (bytes stopped) / stop / disconnect: drop what is left. "silent" (digital silence
      // still arriving): let the buffered tail play out; a fade or a pause between tracks is not a cut.
      if (e.reason.compare(0, 6, "silent") != 0) out->crossfade(fadeFrames());  // a fade-out, not a cut
      lastSilenceMs = e.atMs;
    }
    if (e.kind == Event::IDLE_STOP) { out->flush(); out->idle(); }
  }
  std::string a = arbiter.activeName();
  if (!a.empty()) lastActiveName = a;  // B1.16: remember it for a later PLAY with nothing active
  if (a.empty()) {  // nothing active: turn the lock bundle off (B1.11); the next start decides afresh
    disableLockPlugin();
    unlockedThisStart = false;
  }
  writeActiveSourceFile(a.empty() ? "none" : a);
  updateRemoteTarget();  // B1.16.16
  pump();
}

size_t Server::fadeFrames() const { return (size_t)opts.crossfadeMs * 44100 / 1000; }

Server::Client* Server::clientById(int id) {
  for (auto& kv : clients) if (kv.second.id == id) return &kv.second;
  return nullptr;
}

Server::Client* Server::clientByName(const std::string& name) {
  for (auto& kv : clients) if (kv.second.hello && kv.second.name == name) return &kv.second;
  return nullptr;
}

// B1.12: schedule a PREEMPTED for the source that just lost the output. The delay filters a flap
// (a takeover that immediately reverses) so the active source is never left paused.
void Server::schedulePreempt(const Event& e) {
  if (!opts.preemptPause) return;
  if (e.kind != Event::TAKEOVER || e.from.empty() || e.reason == "reconnect") return;  // only source -> source
  if (Client* loser = clientByName(e.from)) {
    if (loser->version < AUDIOD_PROTO_VERSION_2) return;  // v1 (raop uses DACP, which it drives itself when v2)
    loser->preemptDueMs = e.atMs + opts.preemptDelayMs;
    loser->preemptBy = e.to;
  }
}

void Server::firePreempts(uint64_t now) {
  for (auto& kv : clients) {
    Client& c = kv.second;
    if (!c.preemptDueMs || now < c.preemptDueMs) continue;
    c.preemptDueMs = 0;
    if (arbiter.activeId() == c.id) continue;  // it flapped back to active: do not pause it
    AudiodPreempted p;
    memset(&p, 0, sizeof p);
    strncpy(p.by, c.preemptBy.c_str(), AUDIOD_NAME_MAX - 1);
    sendToClient(c, AUDIOD_MSG_PREEMPTED, &p, sizeof p);
    logEvent(Event{Event::WAKE, now, c.name, c.preemptBy, "preempted (told to pause)"});
  }
}

void Server::ringAppend(Client& c, const int16_t* frames, size_t n) {
  bool active = arbiter.activeId() == c.id;  // an inactive source's ring overwriting itself is normal
  if (n > kRingFrames) { frames += (n - kRingFrames) * 2; if (active) c.overflowFrames += n - kRingFrames; n = kRingFrames; }
  if (c.ringFrames + n > kRingFrames) {  // drop the oldest (active: the producer ran ahead of the pump)
    size_t drop = c.ringFrames + n - kRingFrames;
    c.ringHead = (c.ringHead + drop) % kRingFrames;
    c.ringFrames -= drop;
    if (active) c.overflowFrames += drop;
  }
  size_t tail = (c.ringHead + c.ringFrames) % kRingFrames;
  size_t first = kRingFrames - tail;
  if (first > n) first = n;
  memcpy(&c.ring[tail * 2], frames, first * 4);
  if (n > first) memcpy(&c.ring[0], frames + first * 2, (n - first) * 4);
  c.ringFrames += n;
}

void Server::pump() {
  if (arbiter.activeId() < 0 || !arbiter.outputRunning()) return;
  Client* c = clientById(arbiter.activeId());
  if (!c) return;
  while (c->ringFrames > 0 && out->bufferedSeconds() < opts.sinkTargetSec) {
    size_t n = c->ringFrames < 882 ? c->ringFrames : 882;  // 20 ms slices
    if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: pump #%d %zu frames (head %zu, %zu in ring)", c->id, n, c->ringHead, c->ringFrames);
    size_t first = kRingFrames - c->ringHead;
    if (first > n) first = n;
    out->write(&c->ring[c->ringHead * 2], first);
    if (n > first) out->write(&c->ring[0], n - first);
    c->ringHead = (c->ringHead + n) % kRingFrames;
    c->ringFrames -= n;
  }
}

void Server::run() {
  static int16_t silence[441 * 2 * 4];  // 40 ms
  memset(silence, 0, sizeof silence);
  while (!stopRequested) {
    std::vector<pollfd> pfds;
    pfds.push_back({listenFd, POLLIN, 0});
    // Backpressure: do not read the active source while its staging ring already holds audio the
    // output has no room for; its producer then blocks on the kernel buffers and is paced by the
    // output's real consumption. Not while the output is paused (interruption / yield): then it is
    // drained and overwritten like an inactive source, so it never hard-blocks.
    int activeId = arbiter.activeId();
    bool gateActive = opts.backpressure && activeId >= 0 && arbiter.outputRunning() && out->consuming();
    // B1.10 test hook: once a source becomes active, refuse to read it for stallActiveMs (reproduce
    // the device backpressure stall); the producer must ride it out, not disconnect.
    uint64_t nowg = nowMs();
    if (stallActiveMs && activeId >= 0 && activeId != stalledActiveId) { stalledActiveId = activeId; activeSinceMs = nowg; IOS3_TRACE("STALL HOOK: gating reads of active #%d for %llu ms", activeId, (unsigned long long)stallActiveMs); }
    bool forceStall = stallActiveMs && activeId >= 0 && activeId == stalledActiveId && nowg - activeSinceMs < stallActiveMs;
    for (auto& kv : clients) {
      short ev = POLLIN;
      if (gateActive && kv.second.id == activeId && kv.second.ringFrames >= kStagingFrames) ev = 0;
      if (forceStall && kv.second.id == activeId) ev = 0;
      pfds.push_back({kv.first, ev, 0});
    }
    int rc = poll(pfds.data(), pfds.size(), opts.tickMs);
    if (rc < 0) { if (errno == EINTR) continue; IOS3_TRACE("poll: %s", strerror(errno)); return; }
    for (size_t i = 1; i < pfds.size(); i++) {
      if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      auto it = clients.find(pfds[i].fd);
      if (it != clients.end()) readClient(it->second);
    }
    if (pfds[0].revents & POLLIN) acceptClient();
    uint64_t now = nowMs();
    arbiter.tick(now);
    afterTick();
    out->tick();
    pollHardwareVolume(now);  // B1.9 A: buttons / external changes -> the producers
    flushDockForward(now);    // B1.16.12: the latest held dock level, once the window has passed
    firePreempts(now);        // B1.12: tell a source that lost the output to pause
    syncLockPlugin(now);      // B1.16.11: live lock_plugin changes, re-enable after a SpringBoard restart
    checkOtherAudio(now);     // B1.16.16: other audio while a paused source is the remote's target
    maybeReloadSettings(now);  // B1.14: pick up a settings.conf change
    if (out->resumed()) {
      // The output was paused (interruption / yielding to another app) and just resumed: the active
      // source's ring holds audio from before the pause. Drop it; the producer keeps sending current audio.
      if (Client* c = clientById(arbiter.activeId())) { c->ringHead = 0; c->ringFrames = 0; }
      IOS3_TRACE("output resumed: stale buffered audio of the active source dropped");
    }
    // Keep the output primed with silence between sources so the queue (and the route) stays up
    // until the idle stop; paced by wall clock in 40 ms slices.
    if (opts.feedSilence && arbiter.activeId() < 0 && arbiter.outputRunning()) {
      if (lastSilenceMs == 0) lastSilenceMs = now;
      while (now - lastSilenceMs >= 40) {
        out->write(silence, 441 * 4);
        lastSilenceMs += 40;
      }
    }
  }
  IOS3_TRACE("shutdown requested: releasing the output");
  out->flush();
  out->idle();
  writeActiveSourceFile("none");
  remoteTarget.clear();
  writeRemoteTargetFile(true);  // B1.16.16: a stopped audiod leaves "none" (a crash leaves the last value; the socket check covers it)
}

}  // namespace audiod
