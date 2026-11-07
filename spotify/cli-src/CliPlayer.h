#pragma once
// Player thread: pulls decoded PCM from cspot's CentralAudioBuffer and feeds the AudioQueue sink.
// Adapted from feelfreelinux/cspot targets/cli/CliPlayer (1b07a9c) for the squeezelite-esp32
// cspot fork API (notifyAudioReachedPlayback takes the track identifier).
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "AudioQueueSink.h"
#include "BellTask.h"
#include "StatusWriter.h"

namespace bell {
class CentralAudioBuffer;
}
namespace cspot {
class SpircHandler;
}

class CliPlayer : public bell::Task {
 public:
  CliPlayer(std::unique_ptr<AudioQueueSink> sink, std::shared_ptr<cspot::SpircHandler> spircHandler,
            StatusWriter* status);
  void disconnect();

 private:
  std::shared_ptr<cspot::SpircHandler> handler;
  StatusWriter* status;  // may be null
  std::unique_ptr<AudioQueueSink> audioSink;
  std::shared_ptr<bell::CentralAudioBuffer> centralAudioBuffer;

  // trackHash -> track identifier, so the sink thread can tell spirc which track started.
  // Newest last; only the few most recent tracks can still have chunks in the 0.75 s buffer.
  static const size_t kMaxTrackIds = 4;
  std::mutex idMutex;
  std::vector<std::pair<size_t, std::string>> trackIds;

  std::atomic<bool> pauseRequested{false};
  std::atomic<bool> isPaused{true};
  std::atomic<bool> isRunning{true};
  std::atomic<bool> playlistEnd{false};
  std::mutex runningMutex;

  void runTask() override;
};
