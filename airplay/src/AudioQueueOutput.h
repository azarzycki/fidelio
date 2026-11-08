#pragma once
// AudioOutput backed by the AudioQueue sink shared with cspot-ios3 (same 44.1 kHz/16/stereo sink,
// 128 KB ring + 3 x 8 KB queue buffers; feedPCMFrames blocks while the ring is full, which paces us).
#include <memory>

#include "AudioOutput.h"
#include "AudioQueueSink.h"

class AudioQueueOutput : public AudioOutput {
 public:
  AudioQueueOutput() : sink(new AudioQueueSink()) {}
  explicit AudioQueueOutput(const AudioQueueSink::Options& o) : sink(new AudioQueueSink(o)) {}
  void write(const int16_t* frames, size_t numFrames) override {
    sink->feedPCMFrames(reinterpret_cast<const uint8_t*>(frames), numFrames * 4);
  }
  void flush() override { sink->flush(); }
  void setVolume(float linear) override {
    if (linear < 0) linear = 0;
    if (linear > 1) linear = 1;
    sink->volumeChanged((uint16_t)(linear * 65535.0f));
  }
  double bufferedSeconds() override { return sink->bufferedSeconds(); }
  void idle() override { sink->stop(); }  // releases the hardware path; the next write() restarts the queue
  void tick() override { sink->tick(); }  // recovery watchdog (interruption with no end, missing queue)
  bool resumed() override { return sink->takeResumed(); }
  AudioQueueSink* queueSink() { return sink.get(); }

 private:
  std::unique_ptr<AudioQueueSink> sink;
};
