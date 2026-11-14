#pragma once
// B1.9 feature B: when playback starts from nothing (arbiter 'none' -> a source) wake the screen
// and bring the NowPlaying app to the front, through the private SpringBoardServices /
// GraphicsServices, resolved with dlopen/dlsym (never hard-linked: unavailable = log once, no-op).
#include <stdint.h>

#include <string>

class DisplayWaker {
 public:
  virtual ~DisplayWaker() {}
  // Perform the wake + foreground. Returns false (and the implementation logs once) if unavailable.
  virtual bool wake(const std::string& bundleId) = 0;
  // B1.11: enable / disable a SpringBoard lock-screen bundle by name (SBEnableLockScreenBundle).
  // Returns false (logs once) if unavailable. Default: unsupported.
  virtual bool enableLockBundle(const std::string& name) { (void)name; return false; }
  virtual bool disableLockBundle(const std::string& name) { (void)name; return false; }
  virtual std::string describe() = 0;
};

// Host / test double: counts calls.
class MockDisplayWaker : public DisplayWaker {
 public:
  bool wake(const std::string& bundleId) override { last = bundleId; calls++; return true; }
  bool enableLockBundle(const std::string& name) override { lockName = name; lockEnables++; return true; }
  bool disableLockBundle(const std::string& name) override { lockName = name; lockDisables++; return true; }
  std::string describe() override { return "mock display waker"; }
  int calls = 0, lockEnables = 0, lockDisables = 0;
  std::string last, lockName;
};

#ifdef __APPLE__
DisplayWaker* createSpringBoardWaker();
#endif
