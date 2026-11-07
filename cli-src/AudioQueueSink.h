#pragma once
// AudioQueue output sink for iPhone OS 3.x (AudioToolbox), for cspot's AudioSink interface.
// 16-bit interleaved stereo PCM in, played through the system AudioQueue. feedPCMFrames()
// blocks while the internal ring buffer is full, which paces the player thread.
//
// Route following: the sink owns a small CFRunLoop thread for the AudioSession (cspot's main
// thread never runs a run loop, so AudioSession callbacks would otherwise never be delivered).
// On kAudioSessionProperty_AudioRouteChange (dock/undock, 30-pin cable, headphones) and on the
// end of an interruption it re-activates the session and re-acquires the output: by default the
// AudioQueue is torn down and recreated on the new route (the ring buffer survives, so playback
// continues with ~0.8 s of audio in hand); "restart" only stops/starts the queue; "none" is the
// old behaviour, kept for A/B testing on the device.
#include <AudioToolbox/AudioQueue.h>
#include <AudioToolbox/AudioServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "AudioSink.h"

class AudioQueueSink : public AudioSink {
 public:
  enum RouteOverride { ROUTE_AUTO, ROUTE_SPEAKER, ROUTE_LINEOUT };
  enum RouteRecovery { RECOVER_RECREATE, RECOVER_RESTART, RECOVER_NONE };
  struct Options {
    RouteOverride route = ROUTE_AUTO;        // kAudioSessionProperty_OverrideAudioRoute (speaker only; lineout = no override)
    RouteRecovery recovery = RECOVER_RECREATE;
  };

  AudioQueueSink();
  explicit AudioQueueSink(const Options& options);
  ~AudioQueueSink() override;

  void feedPCMFrames(const uint8_t* buffer, size_t bytes) override;
  void volumeChanged(uint16_t volume) override;  // 0..65535
  bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override;

  // Drop everything that is still queued (used on seek/flush).
  void flush();
  // Seconds of audio still buffered (ring + AudioQueue buffers).
  double bufferedSeconds();
  // Current system audio route name ("Speaker", "LineOut", "Headphone", ...), "" if unknown.
  std::string currentRoute();

 private:
  static const int kNumBuffers = 3;
  static const size_t kBufferBytes = 8 * 1024;   // ~46 ms at 44.1 kHz stereo 16-bit
  static const size_t kRingBytes = 128 * 1024;   // ~0.74 s

  void init();
  static void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer);
  void fillBuffer(AudioQueueRef q, int index);  // enqueues buffers[index] unless the queue still owns it
  void primeBuffers(AudioQueueRef q);           // enqueue every buffer the queue does not own
  void start();
  void stop();
  bool createQueue();   // AudioQueueNewOutput + buffers for `format`; queueMutex must NOT be held
  void destroyQueue();  // stop + dispose; queueMutex must NOT be held
  void reacquireRoute(const char* why);

  // AudioSession (own run-loop thread)
  static void* sessionThreadEntry(void* self);
  void sessionThreadLoop();
  static void interruptionCallback(void* userData, UInt32 state);
  static void routeChangeCallback(void* userData, AudioSessionPropertyID id, UInt32 size, const void* data);
  void onInterruption(UInt32 state);
  void onRouteChange(const void* data);
  void applyRouteOverride();

  Options opts;
  AudioQueueRef queue = nullptr;
  AudioQueueBufferRef buffers[kNumBuffers];
  bool enqueued[kNumBuffers];    // under queueMutex: the queue owns the buffer
  AudioStreamBasicDescription format;
  bool running = false;       // AudioQueueStart has been called on `queue`
  bool open = false;          // `queue` exists with buffers
  bool interrupted = false;
  bool wasRunningBeforeInterruption = false;
  float lastVolume = 1.0f;
  pthread_mutex_t queueMutex;  // guards queue/open/running/interrupted

  std::vector<uint8_t> ring;
  size_t ringHead = 0;  // read position
  size_t ringFill = 0;  // bytes available
  pthread_mutex_t mutex;
  pthread_cond_t spaceAvailable;

  pthread_t sessionThread;
  bool sessionThreadStarted = false;
  CFRunLoopRef sessionRunLoop = nullptr;
  bool sessionReady = false;
  bool sessionOk = false;
  pthread_mutex_t readyMutex;
  pthread_cond_t readyCond;
};
