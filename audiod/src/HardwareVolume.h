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
//
// B1.16.12: the dock. The system volume does not reach an accessory on its own (the packet log of
// 2026-11-22, docs/HANDOFF.md 2.5): iapd only tells the dock when IAPAudioSetVolume is called. tellDock()
// is that call, made by the Server after a SOURCE's set in absolute mode (never for an external change,
// which the dock already knows). The device backend resolves IAP.framework with dlopen/dlsym; the mock
// is driven by env AUDIOD_MOCK_IAP (unset = unavailable).
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

  // B1.16.12: what tellDock() did. UNAVAILABLE: no IAP framework or symbol (no call made); UNSUPPORTED:
  // no attached accessory takes the volume right now (IAPAudioIsVolumeControlSupported said no);
  // QUERY_FAILED: that question itself failed; OK / FAILED: IAPAudioSetVolume was called and returned
  // success / failure.
  enum DockResult { DOCK_UNAVAILABLE, DOCK_UNSUPPORTED, DOCK_QUERY_FAILED, DOCK_OK, DOCK_FAILED };
  virtual bool dockAvailable() { return false; }  // the IAP functions were resolved (no IPC)
  virtual DockResult tellDock(float level) { (void)level; return DOCK_UNAVAILABLE; }  // synchronous: MIG calls to iapd
  virtual std::string describeDock() { return "unavailable (this backend has none)"; }
  static const char* dockResultName(DockResult r) {
    switch (r) {
      case DOCK_UNSUPPORTED: return "unsupported";
      case DOCK_QUERY_FAILED: return "query-failed";
      case DOCK_OK: return "ok";
      case DOCK_FAILED: return "failed";
      default: return "unavailable";
    }
  }
};

// Host / test double. Available iff env AUDIOD_MOCK_HW_VOLUME is set (its value = initial level).
// AUDIOD_MOCK_HW_CHANGE_FILE=<path>: a file whose single float content, when it changes, is taken as an
// external (button) change — lets a test push a "phone button" event.
// B1.16.12: AUDIOD_MOCK_IAP=ok|unsupported|fail|query-fail makes the mock dock available in that state
// (unset = unavailable); every IAPAudioSetVolume it "makes" is logged as `mock iap: IAPAudioSetVolume(L) -> R`.
class MockHardwareVolume : public HardwareVolume {
 public:
  MockHardwareVolume();
  bool available() override { return avail; }
  bool get(float& level) override { if (!avail) return false; level = lvl; return true; }
  bool set(float level) override;
  bool takeChange(float& level) override;
  unsigned pollIntervalMs() override { return 100; }
  std::string describe() override { return avail ? "mock hardware volume (AUDIOD_MOCK_HW_VOLUME)" : "mock hardware volume: unavailable (AUDIOD_MOCK_HW_VOLUME unset)"; }
  bool dockAvailable() override { return !iapMode.empty(); }
  DockResult tellDock(float level) override;
  std::string describeDock() override;
  int sets = 0;

 private:
  bool avail = false;
  float lvl = 0.5f;
  std::string changeFile;
  std::string lastChangeContent;
  std::string iapMode;  // B1.16.12: AUDIOD_MOCK_IAP
};

#ifdef __APPLE__
// AVSystemController through dlopen + the ObjC runtime (see HardwareVolumeApple.cpp).
HardwareVolume* createAVSystemControllerVolume();
#endif
