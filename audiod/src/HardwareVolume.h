#pragma once
// B1.9: the device's real output volume, for "absolute" Spotify volume (the Connect slider IS the
// dock's level). On the phone this is the private AVSystemController (Celestial.framework), loaded
// with dlopen/dlsym and driven through the ObjC runtime: no hard link, so a missing framework or
// selector cannot kill the daemon, it only makes available() false and audiod falls back to the
// software gain (B1.7 behaviour). On the host a mock keeps the level in memory (env
// AUDIOD_MOCK_HW_VOLUME=<0..1> makes it "available"; unset = unavailable) so the plumbing, the
// clamp and the fallback can be tested.
//
// Levels are 0..1 floats as the hardware sees them. set() is only ever called in direct response
// to a slider command; the Server applies --max-volume before calling it.
#include <stdint.h>

#include <string>

class HardwareVolume {
 public:
  virtual ~HardwareVolume() {}
  virtual bool available() = 0;                 // the API is loaded and answered once
  virtual bool get(float& level) = 0;           // current level
  virtual bool set(float level) = 0;            // 0..1
  // Non-blocking: true once per external change (phone buttons / another client), with the new
  // level. Implementations that get change notifications report them here; a poll of get() every
  // pollIntervalMs() is the fallback.
  virtual bool takeChange(float& level) { (void)level; return false; }
  virtual unsigned pollIntervalMs() { return 1000; }
  virtual std::string describe() = 0;
};

// Host / test double. Available iff env AUDIOD_MOCK_HW_VOLUME is set (its value = initial level).
// AUDIOD_MOCK_HW_CHANGE_FILE=<path>: a file whose single float content, when it changes, is taken as an
// external (button) change — lets a test push a "phone button" event.
class MockHardwareVolume : public HardwareVolume {
 public:
  MockHardwareVolume();
  bool available() override { return avail; }
  bool get(float& level) override { if (!avail) return false; level = lvl; return true; }
  bool set(float level) override;
  bool takeChange(float& level) override;
  unsigned pollIntervalMs() override { return 100; }
  std::string describe() override { return avail ? "mock hardware volume (AUDIOD_MOCK_HW_VOLUME)" : "mock hardware volume: unavailable (AUDIOD_MOCK_HW_VOLUME unset)"; }
  int sets = 0;

 private:
  bool avail = false;
  float lvl = 0.5f;
  std::string changeFile;
  std::string lastChangeContent;
};

#ifdef __APPLE__
// AVSystemController through dlopen + the ObjC runtime (see HardwareVolumeApple.cpp).
HardwareVolume* createAVSystemControllerVolume();
#endif
