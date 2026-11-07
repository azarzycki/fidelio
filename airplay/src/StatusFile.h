#pragma once
// Status files for the dock display app: the same contract cspot-ios3 rev 4 writes
// (status.json + status.plist, atomic), plus "source": "airplay". Also writes cover.jpg.
#include <pthread.h>
#include <stdint.h>

#include <mutex>
#include <string>
#include <vector>

class StatusFile {
 public:
  StatusFile(const std::string& dir, const std::string& device);  // dir "" = disabled
  ~StatusFile();

  void setState(const std::string& state);  // idle|connected|playing|paused|stopped
  void setTrack(const std::string& title, const std::string& artist, const std::string& album);
  void setProgress(uint32_t positionMs, uint32_t durationMs);  // restarts the position clock
  void setPlaying(bool playing);                               // runs / freezes the position clock
  void setCodec(const std::string& codec);
  void setPeer(const std::string& peer);
  void setCover(const std::vector<uint8_t>& jpeg);  // writes cover.jpg atomically
  void clearTrack();
  void poke();  // write now

 private:
  static void* threadEntry(void* self);
  void loop();
  void writeFiles();
  void wake();
  uint32_t positionNow();
  static uint64_t nowMs();

  std::string dir, device;
  bool enabled;
  std::mutex mutex;
  pthread_t thread;
  pthread_mutex_t wakeMutex;
  pthread_cond_t cond;
  bool dirty = true, running = true;

  std::string state = "idle", title, artist, album, codec, peer, cover;
  uint32_t durationMs = 0, baseMs = 0;
  uint64_t baseTime = 0;
  bool clockRunning = false;
};
