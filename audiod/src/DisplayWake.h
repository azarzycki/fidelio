#pragma once
// B1.9 feature B: when playback starts from nothing (arbiter 'none' -> a source) wake the screen
// and bring the NowPlaying app to the front, through the private SpringBoardServices /
// GraphicsServices, resolved with dlopen/dlsym (never hard-linked: unavailable = log once, no-op).
// B1.16.4: when the launch is refused because the device is locked, the server can ask SpringBoard to
// unlock and open a URL (SBSOpenSensitiveURLAndUnlock). B1.16.6: the accepted request is the result;
// there is no launch retry (it kept answering 3 on the unlocked iPad).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "ios3_trace.h"

class DisplayWaker {
 public:
  // SBSLaunchApplicationWithIdentifier results the server acts on (other non-zero values are errors).
  static const int kLaunchOk = 0;
  static const int kLaunchLocked = 3;       // "device locked": iOS 5 refuses a launch over the lock screen
  static const int kUnavailable = -1;       // the private API is missing (logged once by the implementation)
  virtual ~DisplayWaker() {}
  // Undim the screen, reset the idle timer and launch the app. Returns the launch result (kLaunchOk,
  // kLaunchLocked, another SpringBoard error code, or kUnavailable).
  virtual int wake(const std::string& bundleId) = 0;
  // B1.16.10: wake the screen only (undim + reset the idle timer), without launching anything, for
  // wake_mode=screen: a locked device then shows its lit lock screen (with the lock plugin, if enabled).
  virtual void wakeScreen() {}
  // B1.16.4: whether SBSOpenSensitiveURLAndUnlock resolved (decided at startup, cheap to call).
  virtual bool canUnlock() { return false; }
  // B1.16.4: SBSOpenSensitiveURLAndUnlock(url, unlock = true). 1 = SpringBoard accepted the request
  // (B1.16.6, device-verified: without a passcode it unlocks and opens the URL's app), 0 = refused or
  // failed (e.g. a caller without the entitlements), kUnavailable = the symbol is missing.
  virtual int openURLAndUnlock(const std::string& url) { (void)url; return kUnavailable; }
  // B1.11: enable / disable a SpringBoard lock-screen bundle by name (SBEnableLockScreenBundle).
  // Returns false (logs once) if unavailable. Default: unsupported.
  virtual bool enableLockBundle(const std::string& name) { (void)name; return false; }
  virtual bool disableLockBundle(const std::string& name) { (void)name; return false; }
  // B1.16.11: SpringBoard's pid (0 = unknown), so the server can tell that SpringBoard restarted (a
  // respring forgets an enabled lock bundle). Default: unknown, i.e. no restart detection.
  virtual int springBoardPid() { return 0; }
  virtual std::string describe() = 0;
};

// Host / test double: counts calls and logs each one ("mock waker: ..."), so the host tests can read
// what audiod asked SpringBoard to do. B1.16.4: it can play a locked device, configured from the
// environment (read once, at construction):
//   AUDIOD_MOCK_LOCKED=1          the device starts locked: wake()'s launch returns kLaunchLocked
//   AUDIOD_MOCK_UNLOCK=MODE       what openURLAndUnlock does while locked:
//       ok      (default) returns 1 and the device is unlocked: a later wake()'s launch returns kLaunchOk
//       stuck   returns 1 but later launches keep answering kLaunchLocked. B1.16.6: that is what the
//               5.1.1 iPad does for seconds after an accepted unlock although it IS unlocked, so audiod
//               must treat both modes alike (the accepted request is the result)
//       refuse  returns 0 (the request failed); still locked
//       missing the symbol is absent: canUnlock() false, openURLAndUnlock returns kUnavailable
//   AUDIOD_MOCK_SB_RESTART_MS=N   B1.16.11: SpringBoard "restarts" N ms after start: springBoardPid()
//                                 is 100 before and 200 after (unset: always 100, no restart)
class MockDisplayWaker : public DisplayWaker {
 public:
  MockDisplayWaker() {
    const char* l = getenv("AUDIOD_MOCK_LOCKED");
    locked = l && strcmp(l, "0") != 0 && *l;
    const char* m = getenv("AUDIOD_MOCK_UNLOCK");
    mode = m && *m ? m : "ok";
    const char* r = getenv("AUDIOD_MOCK_SB_RESTART_MS");
    sbRestartMs = r && *r ? strtoull(r, nullptr, 10) : 0;
    startMs = nowMs();
  }
  int wake(const std::string& bundleId) override {
    last = bundleId;
    calls++;
    int rc = locked ? kLaunchLocked : kLaunchOk;
    IOS3_TRACE("mock waker: wake(%s) -> %d%s", bundleId.c_str(), rc, locked ? " (device locked)" : "");
    return rc;
  }
  void wakeScreen() override { screenWakes++; IOS3_TRACE("mock waker: wakeScreen()"); }
  bool canUnlock() override { return mode != "missing"; }
  int openURLAndUnlock(const std::string& url) override {
    unlockCalls++;
    int rc = mode == "missing" ? kUnavailable : mode == "refuse" ? 0 : 1;
    if (rc == 1 && mode == "ok") locked = false;
    IOS3_TRACE("mock waker: openURLAndUnlock(%s) -> %d (call #%d, mode %s)", url.c_str(), rc, unlockCalls, mode.c_str());
    return rc;
  }
  bool enableLockBundle(const std::string& name) override {
    lockName = name; lockEnables++;
    IOS3_TRACE("mock waker: enableLockBundle(%s)", name.c_str());
    return true;
  }
  bool disableLockBundle(const std::string& name) override {
    lockName = name; lockDisables++;
    IOS3_TRACE("mock waker: disableLockBundle(%s)", name.c_str());
    return true;
  }
  int springBoardPid() override { return sbRestartMs && nowMs() - startMs >= sbRestartMs ? 200 : 100; }
  std::string describe() override { return std::string("mock display waker (") + (locked ? "locked" : "unlocked") + ", unlock " + mode + ")"; }
  int calls = 0, screenWakes = 0, unlockCalls = 0, lockEnables = 0, lockDisables = 0;
  std::string last, lockName;

 private:
  static uint64_t nowMs() {
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return mach_absolute_time() / 1000000ull * tb.numer / tb.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
  }
  bool locked = false;
  std::string mode;
  uint64_t sbRestartMs = 0, startMs = 0;
};

#ifdef __APPLE__
DisplayWaker* createSpringBoardWaker();
#endif
