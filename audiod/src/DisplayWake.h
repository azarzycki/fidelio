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
  virtual std::string describe() = 0;
};

// Host / test double: counts calls.
class MockDisplayWaker : public DisplayWaker {
 public:
  bool wake(const std::string& bundleId) override { last = bundleId; calls++; return true; }
  std::string describe() override { return "mock display waker"; }
  int calls = 0;
  std::string last;
};

#ifdef __APPLE__
DisplayWaker* createSpringBoardWaker();
#endif
