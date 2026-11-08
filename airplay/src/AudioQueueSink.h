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
//
// Invariant (B1.1): while someone wants playback (audio has been fed and stop() not called), the
// queue is (re)started within ~1 s whatever sequence of interruption-begin / route-change /
// possibly MISSING interruption-end events arrives. `wantPlaying` is that intent, separate from
// the transient running/interrupted flags; a route change while "interrupted" ends the
// interruption (the end callback never came on the device); tick() (audiod's 20 ms loop) and
// feedPCMFrames() run a watchdog that clears an interruption older than kInterruptGraceMs and
// retries AudioQueueStart every kStartRetryMs while it fails (a genuine call keeps failing until
// it ends; the next success resumes). Seen on hardware: begin-interruption, two route changes,
// no end -> recreated-but-never-started queue, "shows playing, no sound".
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
  // Stop the queue (releases the hardware path until the next feedPCMFrames() restarts it) and
  // drop the playback intent: nothing restarts it until audio is fed again.
  void stop();
  // Watchdog step; call periodically (audiod: every 20 ms) so recovery does not depend on feeds.
  void tick();
  // Seconds of audio still buffered (ring + the real PCM inside the AudioQueue buffers).
  double bufferedSeconds();
  // Current system audio route name ("Speaker", "LineOut", "Headphone", ...), "" if unknown.
  std::string currentRoute();

 private:
  static const int kNumBuffers = 3;
  static const size_t kBufferBytes = 8 * 1024;   // ~46 ms at 44.1 kHz stereo 16-bit
  static const size_t kRingBytes = 128 * 1024;   // ~0.74 s
  static const size_t kPrimeBytes = 35280;       // 200 ms: after an underrun, pad silence until this much is back
  static const long kInterruptGraceMs = 1000;    // an interruption with no end callback is forgotten after this
  static const long kStartRetryMs = 500;         // retry AudioQueueStart this often while it fails
  static const long kRecoverRetryMs = 1000;      // retry re-acquiring a missing queue this often
  static const long kStallMs = 2000;             // no buffer completion for this long: the queue is stuck

  void init();
  static void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer);
  void fillBuffer(AudioQueueRef q, int index);  // enqueues buffers[index] unless the queue still owns it
  void primeBuffers(AudioQueueRef q);           // enqueue every buffer the queue does not own
  void start();
  void ensurePlaying(const char* from);  // the watchdog: wantPlaying && !running -> clear stale interruption, start
  static uint64_t nowMs();
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
  size_t queuedBytes[kNumBuffers];  // under queueMutex: real PCM (not silence padding) in that buffer
  AudioStreamBasicDescription format;
  bool running = false;       // AudioQueueStart has been called on `queue`
  bool open = false;          // `queue` exists with buffers
  bool interrupted = false;
  bool wasRunningBeforeInterruption = false;
  bool wantPlaying = false;          // under queueMutex: audio has been fed since the last stop()
  uint64_t interruptedSinceMs = 0;   // under queueMutex
  uint64_t lastStartAttemptMs = 0;   // watchdog bookkeeping (feed/tick callers only)
  uint64_t lastRecoverMs = 0;
  float lastVolume = 1.0f;
  bool stalled = false;        // under mutex: a feed timed out; cleared by the next completion
  bool startFailed = false;    // AudioQueueStart failed last time (log only changes)
  pthread_mutex_t queueMutex;  // guards queue/open/running/interrupted

  std::vector<uint8_t> ring;
  size_t ringHead = 0;  // read position
  size_t ringFill = 0;  // bytes available
  bool priming = true;  // under mutex: play silence until the ring holds kPrimeBytes (start, flush, underrun)
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
