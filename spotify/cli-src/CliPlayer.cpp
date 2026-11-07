#include "CliPlayer.h"

#include <functional>
#include <string_view>
#include <variant>
#include <vector>

#include "BellUtils.h"  // for BELL_SLEEP_MS
#include "CentralAudioBuffer.h"
#include "Logger.h"
#include "SpircHandler.h"
#include "StreamInfo.h"
#include "TrackPlayer.h"

CliPlayer::CliPlayer(std::unique_ptr<AudioQueueSink> sink, std::shared_ptr<cspot::SpircHandler> handler,
                     StatusWriter* status)
    : bell::Task("player", 1024, 0, 0) {
  this->handler = handler;
  this->status = status;
  this->audioSink = std::move(sink);
  // Upstream passes 128 * 1024 here. The argument is a CHUNK COUNT and each chunk is
  // ~4.1 KB, zero-filled on construction: ~540 MB, which is what rebooted a 116 MB iPhone.
  // 32 chunks = 132 KB = 0.75 s of 44.1 kHz stereo; the AudioQueue sink buffers another ~0.8 s.
  this->centralAudioBuffer = std::make_shared<bell::CentralAudioBuffer>(32);

  this->handler->getTrackPlayer()->setDataCallback(
      [this](uint8_t* data, size_t bytes, std::string_view trackId) {
        size_t hash = std::hash<std::string_view>()(trackId);
        {
          std::scoped_lock lock(idMutex);
          if (trackIds.empty() || trackIds.back().first != hash) {
            if (trackIds.size() >= kMaxTrackIds) trackIds.erase(trackIds.begin());
            trackIds.emplace_back(hash, std::string(trackId));
          }
        }
        return this->centralAudioBuffer->writePCM(data, bytes, hash);
      });

  this->isPaused = false;

  this->handler->setEventHandler([this](std::unique_ptr<cspot::SpircHandler::Event> event) {
    if (this->status) this->status->onEvent(*event);
    switch (event->eventType) {
      case cspot::SpircHandler::EventType::PLAY_PAUSE:
        if (std::get<bool>(event->data)) {
          this->pauseRequested = true;
        } else {
          this->isPaused = false;
          this->pauseRequested = false;
        }
        break;
      case cspot::SpircHandler::EventType::FLUSH:
      case cspot::SpircHandler::EventType::DISC:
      case cspot::SpircHandler::EventType::SEEK:
        this->centralAudioBuffer->clearBuffer();
        this->audioSink->flush();
        break;
      case cspot::SpircHandler::EventType::PLAYBACK_START:
        this->isPaused = true;
        this->playlistEnd = false;
        this->centralAudioBuffer->clearBuffer();
        this->audioSink->flush();
        break;
      case cspot::SpircHandler::EventType::DEPLETED:
        this->playlistEnd = true;
        break;
      case cspot::SpircHandler::EventType::VOLUME: {
        int volume = std::get<int>(event->data);  // 0..65535 (Spotify scale)
        this->audioSink->volumeChanged((uint16_t)volume);
        break;
      }
      case cspot::SpircHandler::EventType::TRACK_INFO: {
        auto& info = std::get<cspot::TrackInfo>(event->data);
        CSPOT_LOG(info, "Now playing: %s - %s (%s)", info.artist.c_str(), info.name.c_str(), info.album.c_str());
        break;
      }
      default:
        break;
    }
  });
  startTask();
}

void CliPlayer::runTask() {
  std::scoped_lock lock(runningMutex);
  bell::CentralAudioBuffer::AudioChunk* chunk;
  size_t lastHash = 0;

  while (isRunning) {
    if (this->isPaused) {
      BELL_SLEEP_MS(10);
      continue;
    }
    chunk = this->centralAudioBuffer->readChunk();

    if (this->pauseRequested) {
      this->pauseRequested = false;
      this->isPaused = true;  // no BellDSP fade: keep CPU use minimal on the ARM11
    }

    if (!chunk || chunk->pcmSize == 0) {
      if (this->playlistEnd && this->audioSink->bufferedSeconds() < 0.05) {
        this->handler->notifyAudioEnded();
        this->playlistEnd = false;
        if (this->status) this->status->audioEnded();
      }
      BELL_SLEEP_MS(10);
      continue;
    }

    if (lastHash != chunk->trackHash) {
      lastHash = chunk->trackHash;
      std::string id;
      {
        std::scoped_lock l(idMutex);
        for (auto& e : trackIds)
          if (e.first == lastHash) id = e.second;
      }
      this->handler->notifyAudioReachedPlayback(id);
    }
    this->audioSink->feedPCMFrames(chunk->pcmData, chunk->pcmSize);
  }
}

void CliPlayer::disconnect() {
  isRunning = false;
  std::scoped_lock lock(runningMutex);
}
