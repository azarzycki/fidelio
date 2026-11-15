#pragma once
// B1.9 feature B: when playback starts from nothing (arbiter 'none' -> a source) wake the screen
// and bring the NowPlaying app to the front, through the private SpringBoardServices /
// GraphicsServices, resolved with dlopen/dlsym (never hard-linked: unavailable = log once, no-op).
// B1.16.4: when the launch is refused because the device is locked, the server can ask SpringBoard to
// unlock and open a URL (SBSOpenSensitiveURLAndUnlock), then retry the launch.
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
  // B1.16.4: the launch alone (the retry after an unlock request). Same result codes as wake().
  virtual int launch(const std::string& bundleId) = 0;
  // B1.16.4: whether SBSOpenSensitiveURLAndUnlock resolved (decided at startup, cheap to call).
  virtual bool canUnlock() { return false; }
  // B1.16.4: SBSOpenSensitiveURLAndUnlock(url, unlock = true). 1 = SpringBoard received the request
  // (NOT proof that it unlocked: only a later successful launch is), 0 = the request failed,
  // kUnavailable = the symbol is missing.
  virtual int openURLAndUnlock(const std::string& url) { (void)url; return kUnavailable; }
  // B1.11: enable / disable a SpringBoard lock-screen bundle by name (SBEnableLockScreenBundle).
  // Returns false (logs once) if unavailable. Default: unsupported.
  virtual bool enableLockBundle(const std::string& name) { (void)name; return false; }
  virtual bool disableLockBundle(const std::string& name) { (void)name; return false; }
  virtual std::string describe() = 0;
};

// Host / test double: counts calls and logs each one ("mock waker: ..."), so the host tests can read
// what audiod asked SpringBoard to do. B1.16.4: it can play a locked device, configured from the
// environment (read once, at construction):
//   AUDIOD_MOCK_LOCKED=1          the device starts locked: wake()/launch() return kLaunchLocked
//   AUDIOD_MOCK_UNLOCK=MODE       what openURLAndUnlock does while locked:
//       ok      (default) returns 1; the device is unlocked AUDIOD_MOCK_UNLOCK_MS (default 1500) later
//       stuck   returns 1 but the device never unlocks (launch keeps returning kLaunchLocked)
//       refuse  returns 0 (the request failed); still locked
//       missing the symbol is absent: canUnlock() false, openURLAndUnlock returns kUnavailable
class MockDisplayWaker : public DisplayWaker {
 public:
  MockDisplayWaker() {
    const char* l = getenv("AUDIOD_MOCK_LOCKED");
    locked = l && strcmp(l, "0") != 0 && *l;
    const char* m = getenv("AUDIOD_MOCK_UNLOCK");
    mode = m && *m ? m : "ok";
    const char* d = getenv("AUDIOD_MOCK_UNLOCK_MS");
    unlockDelayMs = d ? strtoull(d, nullptr, 10) : 1500;
  }
  int wake(const std::string& bundleId) override { last = bundleId; calls++; return result("wake", bundleId); }
  int launch(const std::string& bundleId) override { last = bundleId; launches++; return result("launch", bundleId); }
  bool canUnlock() override { return mode != "missing"; }
  int openURLAndUnlock(const std::string& url) override {
    unlockCalls++;
    int rc = mode == "missing" ? kUnavailable : mode == "refuse" ? 0 : 1;
    if (rc == 1 && locked && mode == "ok" && !unlockAtMs) unlockAtMs = nowMs() + unlockDelayMs;
    IOS3_TRACE("mock waker: openURLAndUnlock(%s) -> %d (call #%d, mode %s)", url.c_str(), rc, unlockCalls, mode.c_str());
    return rc;
  }
  bool enableLockBundle(const std::string& name) override { lockName = name; lockEnables++; return true; }
  bool disableLockBundle(const std::string& name) override { lockName = name; lockDisables++; return true; }
  std::string describe() override { return std::string("mock display waker (") + (locked ? "locked" : "unlocked") + ", unlock " + mode + ")"; }
  int calls = 0, launches = 0, unlockCalls = 0, lockEnables = 0, lockDisables = 0;
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
  int result(const char* what, const std::string& bundleId) {
    if (locked && unlockAtMs && nowMs() >= unlockAtMs) locked = false;
    int rc = locked ? kLaunchLocked : kLaunchOk;
    IOS3_TRACE("mock waker: %s(%s) -> %d%s", what, bundleId.c_str(), rc, locked ? " (device locked)" : "");
    return rc;
  }
  bool locked = false;
  std::string mode;
  uint64_t unlockDelayMs = 1500, unlockAtMs = 0;
};

#ifdef __APPLE__
DisplayWaker* createSpringBoardWaker();
#endif
