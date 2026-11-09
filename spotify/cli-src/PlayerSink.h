#pragma once
// The sink CliPlayer feeds: fidelio-audiod over its Unix socket (default) or the local AudioQueue
// (--direct-audio, standalone debugging). Both block the player thread to pace it.
#include <stddef.h>
#include <stdint.h>

#include <string>

#include "SocketSink.h"
#include "VolumeCurve.h"
#ifdef __APPLE__
#include "AudioQueueSink.h"
#endif

class PlayerSink {
 public:
  virtual ~PlayerSink() {}
  virtual void feedPCMFrames(const uint8_t* data, size_t bytes) = 0;  // 44.1 kHz / 16-bit / stereo
  // Connect volume 0..65535 -> the sink's linear gain through the perceptual curve (same for both sinks)
  void volumeChanged(uint16_t volume) { setGain(connectVolumeToGain(volume, curve)); }
  virtual void setGain(float linear) = 0;
  static VolumeCurve curve;
  virtual void flush() = 0;                                            // seek / track change
  virtual double bufferedSeconds() = 0;                                // for end-of-playlist detection
  virtual void audioEnded() {}                                         // playlist finished and drained
  virtual std::string describe() = 0;
};

class AudiodSink : public PlayerSink {
 public:
  AudiodSink(const std::string& sockPath, unsigned leadMs = 0) : sink("spotify", sockPath), path(sockPath) {
    if (leadMs) sink.setLeadMs(leadMs);
  }
  void feedPCMFrames(const uint8_t* d, size_t bytes) override { sink.write(reinterpret_cast<const int16_t*>(d), bytes / 4); }
  void setGain(float linear) override { sink.setVolume(linear); }
  void flush() override { sink.flush(); }
  double bufferedSeconds() override { return sink.bufferedSeconds(); }
  void audioEnded() override { sink.idle(); }
  std::string describe() override { return "fidelio-audiod at " + path; }

 private:
  SocketSink sink;
  std::string path;
};

#ifdef __APPLE__
class LocalQueueSink : public PlayerSink {
 public:
  explicit LocalQueueSink(const AudioQueueSink::Options& o) : q(o) {}
  void feedPCMFrames(const uint8_t* d, size_t bytes) override { q.feedPCMFrames(d, bytes); }
  void setGain(float linear) override { q.setGain(linear); }
  void flush() override { q.flush(); }
  double bufferedSeconds() override { return q.bufferedSeconds(); }
  void audioEnded() override { q.stop(); }
  std::string describe() override { return "local AudioQueue (route '" + q.currentRoute() + "')"; }

 private:
  AudioQueueSink q;
};
#endif

