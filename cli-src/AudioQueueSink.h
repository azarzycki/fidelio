#pragma once
// AudioQueue output sink for iPhone OS 3.x (AudioToolbox), for cspot's AudioSink interface.
// 16-bit interleaved stereo PCM in, played through the system AudioQueue. feedPCMFrames()
// blocks while the internal ring buffer is full, which paces the player thread.
#include <AudioToolbox/AudioQueue.h>
#include <pthread.h>
#include <stdint.h>

#include <vector>

#include "AudioSink.h"

class AudioQueueSink : public AudioSink {
 public:
  AudioQueueSink();
  ~AudioQueueSink() override;

  void feedPCMFrames(const uint8_t* buffer, size_t bytes) override;
  void volumeChanged(uint16_t volume) override;  // 0..65535
  bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override;

  // Drop everything that is still queued (used on seek/flush).
  void flush();
  // Seconds of audio still buffered (ring + AudioQueue buffers).
  double bufferedSeconds();

 private:
  static const int kNumBuffers = 3;
  static const size_t kBufferBytes = 8 * 1024;   // ~46 ms at 44.1 kHz stereo 16-bit
  static const size_t kRingBytes = 128 * 1024;   // ~0.74 s

  static void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer);
  void fillBuffer(AudioQueueBufferRef buffer);
  void start();
  void stop();

  AudioQueueRef queue = nullptr;
  AudioQueueBufferRef buffers[kNumBuffers];
  AudioStreamBasicDescription format;
  bool running = false;
  bool open = false;

  std::vector<uint8_t> ring;
  size_t ringHead = 0;  // read position
  size_t ringFill = 0;  // bytes available
  pthread_mutex_t mutex;
  pthread_cond_t spaceAvailable;
};
