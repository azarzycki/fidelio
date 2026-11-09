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
  // Connect volume 0..65535 -> the sink's linear gain through the perceptual curve (same for both sinks).
  // B1.9: the raw slider travels too, so audiod can drive the hardware volume in absolute mode.
  void volumeChanged(uint16_t volume) { setVolume(connectVolumeToGain(volume, curve), volume); }
  virtual void setGain(float linear) = 0;
  virtual void setVolume(float linear, uint16_t slider) { (void)slider; setGain(linear); }
  static VolumeCurve curve;
  // B1.9: non-blocking housekeeping (audiod sink: read WELCOME / HWVOLUME, reconnect); call it often.
  virtual void poll() {}
  // B1.9: audiod reports that it drives the hardware volume, and its current level as a slider value.
  virtual bool hardwareVolumeAbsolute() { return false; }
  virtual uint16_t hardwareSlider() { return 0; }
  // Called from poll()/feed when audiod reports the hardware level (absolute mode) -- the CLI pushes it
  // to the Connect controller as the device volume.
  void (*onHardwareVolume)(void* user, bool absolute, uint16_t slider) = nullptr;
  void* onHardwareVolumeUser = nullptr;
  virtual void flush() = 0;                                            // seek / track change
  virtual double bufferedSeconds() = 0;                                // for end-of-playlist detection
  virtual void audioEnded() {}                                         // playlist finished and drained
  virtual std::string describe() = 0;
};

class AudiodSink : public PlayerSink {
 public:
  AudiodSink(const std::string& sockPath, unsigned leadMs = 0) : sink("spotify", sockPath), path(sockPath) {
    if (leadMs) sink.setLeadMs(leadMs);
    sink.enableServerMessages();  // protocol v2: WELCOME / HWVOLUME (the hardware volume, B1.9)
    sink.setHardwareVolumeCallback(&AudiodSink::hwTrampoline, this);
    sink.poll();  // connect now so WELCOME (absolute? level?) is known before Spotify asks for our volume
  }
  void feedPCMFrames(const uint8_t* d, size_t bytes) override { sink.write(reinterpret_cast<const int16_t*>(d), bytes / 4); }
  void setGain(float linear) override { sink.setVolume(linear); }
  void setVolume(float linear, uint16_t slider) override { sink.setVolumeSlider(linear, slider, true); }
  void poll() override { sink.poll(); }
  bool hardwareVolumeAbsolute() override { return sink.hardwareVolumeAbsolute(); }
  uint16_t hardwareSlider() override { return sink.hardwareSlider(); }
  void flush() override { sink.flush(); }
  double bufferedSeconds() override { return sink.bufferedSeconds(); }
  void audioEnded() override { sink.idle(); }
  std::string describe() override { return "fidelio-audiod at " + path; }

 private:
  static void hwTrampoline(void* user, bool absolute, uint16_t slider) {
    AudiodSink* self = static_cast<AudiodSink*>(user);
    if (self->onHardwareVolume) self->onHardwareVolume(self->onHardwareVolumeUser, absolute, slider);
  }
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

