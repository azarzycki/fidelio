#pragma once
// StatusWriter: publishes cspot's now-playing state for the dock display app.
// Writes <dir>/status.json and <dir>/status.plist atomically (tmp + rename) on every state
// change and once a second while playing; fetches the album cover (HTTPS, i.scdn.co) on its
// own thread and saves <dir>/cover.jpg.
#include <pthread.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "SpircHandler.h"

class StatusWriter {
 public:
  StatusWriter(const std::string& dir, const std::string& deviceName, int bitrateKbps);
  ~StatusWriter();

  // Called from the Spirc event handler (any thread). Cheap: records state and wakes the writer.
  void onEvent(const cspot::SpircHandler::Event& event);
  // Lifecycle states outside Spirc events.
  void setState(const char* state);  // "idle", "connected", "stopped"
  void setConnection(const std::string& ap, const std::string& country);
  // The country code arrives on the Mercury thread after startTask(); poll it at write time.
  void setCountryProvider(std::function<std::string()> provider);
  // Position helpers used by the player: the audio actually ended (playlist end).
  void audioEnded();

 private:
  static const size_t kMaxCoverBytes = 1024 * 1024;  // Spotify covers are tens of KB; a bound for Content-Length

  static void* threadEntry(void* self);
  void threadLoop();
  void writeFiles();                 // caller holds no lock
  bool fetchCover(const std::string& url, std::string& outName);
  uint32_t currentPositionMs();      // caller holds mutex
  static uint64_t nowMs();

  std::string dir, device;
  int bitrate;

  std::mutex mutex;
  pthread_t thread;
  pthread_cond_t wake;
  pthread_mutex_t wakeMutex;
  bool dirty = true;
  bool running = true;

  // state under mutex
  std::string state = "idle";
  std::string title, artist, album, imageUrl, coverFile, coverFetchedUrl;
  std::string ap, country;
  std::function<std::string()> countryProvider;
  uint32_t durationMs = 0;
  uint32_t baseMs = 0;         // position at baseTime
  uint64_t baseTime = 0;       // ms clock when baseMs was taken
  bool clockRunning = false;
  bool pendingStart = false;   // PLAYBACK_START seen, TRACK_INFO not yet
  uint32_t pendingStartMs = 0;
};
