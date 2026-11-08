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
// Threading (B1.2): the AudioSession callbacks only SET FLAGS (interrupted, interruption ended,
// route changed). All AudioQueue lifecycle work (create/destroy/start/stop/reset) happens on the
// owner thread, i.e. inside feedPCMFrames() / tick() / flush() / stop() / setParams(), serialized
// by lifecycleMutex, which the AudioQueue callback never takes. queueMutex guards the shared
// queue/buffer state for the callback; destroyQueue() nulls `queue` and clears buffers[] in the
// same critical section, then waits for in-flight callbacks before AudioQueueDispose.
//
// Invariant (B1.1/B1.2): while someone wants playback (audio has been fed and stop() not called),
// the queue is running AND completing buffers within ~1 s whatever sequence of interruption-begin
// / route-change / possibly MISSING interruption-end events arrives, and whatever mediaserverd
// does: `wantPlaying` is that intent; a route change while "interrupted" ends the interruption;
// the watchdog in ensurePlaying() (every tick()/feed) clears an interruption older than
// kInterruptGraceMs, retries AudioQueueStart every kStartRetryMs while it fails, re-acquires a
// missing queue, and re-acquires a queue that is "running" but has completed no buffer for
// kNoCompletionMs (mediaserverd restart, silent wedge).
//
// Yielding (B1.4): a stuck interruption is only "forgotten" if no other audio is playing
// (kAudioSessionProperty_OtherAudioIsPlaying). While another app plays (the user pressed play in
// the Music app) the sink stays paused and the grace timer is held; it resumes once other audio
// has been off for the grace window, or on a real END callback. Any resume drops the ring.
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

  // Owner-thread entry points (any thread may call them; they serialize on lifecycleMutex).
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
  // Lifecycle changes so far (queue creations), for diagnostics and tests.
  unsigned long queueGeneration();
  // True once after the queue resumed from an interruption or a yield: the ring was dropped, and the
  // caller should drop whatever stale audio it buffered meanwhile too (audiod: the source ring).
  bool takeResumed();
  // The queue is running and not interrupted: audio fed now will be heard.
  bool isConsuming();

  static const long kInterruptGraceMs = 1000;    // an interruption with no end callback is forgotten after this
  static const long kStartRetryMs = 500;         // retry AudioQueueStart this often while it fails
  static const long kRecoverRetryMs = 1000;      // retry re-acquiring a missing/dead queue this often
  static const long kNoCompletionMs = 1000;      // "running" but no buffer completed for this long = dead queue
  static const long kFirstCompletionMs = 3000;   // ... except right after AudioQueueStart: the first completion may take longer
  static const long kMaxRecoverBackoffMs = 16000;  // consecutive no-completion recoveries back off 1, 2, 4 ... s up to this

 private:
  static const int kNumBuffers = 3;
  static const size_t kBufferBytes = 8 * 1024;   // ~46 ms at 44.1 kHz stereo 16-bit
  static const size_t kRingBytes = 128 * 1024;   // ~0.74 s
  static const size_t kPrimeBytes = 35280;       // 200 ms: after an underrun, pad silence until this much is back
  static const long kStallMs = 2000;             // direct mode: ring full and nothing completes for this long

  void init();
  static uint64_t nowMs();  // monotonic (mach_absolute_time on the device)

  // --- owner thread, lifecycleMutex held
  void ensurePlaying(const char* from);  // the watchdog and flag processor
  bool otherAudioIsPlaying();            // AudioSession query: another app owns the audio right now
  void resumeFromInterruption(const char* why);  // drop the stale ring, flag the resume for the caller
  void reacquireRoute(const char* why);  // destroy/create (or stop) + start if wanted
  bool createQueue();
  void destroyQueue();
  void start();
  void stopQueue();
  void resetAndPrime();

  // --- AudioQueue thread
  static void outputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer);
  void fillBuffer(AudioQueueRef q, int index);  // enqueues buffers[index] unless the queue still owns it
  void primeBuffers(AudioQueueRef q);           // enqueue every buffer the queue does not own

  // --- AudioSession thread (own run loop): flags only
  static void* sessionThreadEntry(void* self);
  void sessionThreadLoop();
  static void interruptionCallback(void* userData, UInt32 state);
  static void routeChangeCallback(void* userData, AudioSessionPropertyID id, UInt32 size, const void* data);
  void onInterruption(UInt32 state);
  void onRouteChange(const void* data);
  void applyRouteOverride();

  Options opts;
  pthread_mutex_t lifecycleMutex;  // serializes every lifecycle operation; never taken by the AudioQueue callback

  // --- under queueMutex
  pthread_mutex_t queueMutex;
  AudioQueueRef queue = nullptr;
  AudioQueueBufferRef buffers[kNumBuffers];
  bool enqueued[kNumBuffers];       // the queue owns the buffer
  size_t queuedBytes[kNumBuffers];  // real PCM (not silence padding) in that buffer
  AudioStreamBasicDescription format;
  bool open = false;                // `queue` exists with buffers
  bool running = false;             // AudioQueueStart succeeded and no Stop/Pause since
  bool interrupted = false;         // begin-interruption seen, no end yet
  bool pausedForInterruption = false;
  uint64_t interruptedSinceMs = 0;
  bool interruptionEndPending = false;  // set by the callback, consumed by ensurePlaying()
  bool routeChangePending = false;      // set by the callback, consumed by ensurePlaying()
  bool wantPlaying = false;             // audio has been fed since the last stop()
  bool resumedPending = false;          // set on resume, consumed by takeResumed()
  uint64_t lastCompletionMs = 0;        // last buffer the queue handed back
  uint64_t startedAtMs = 0;             // AudioQueueStart success; 0 = not running
  bool completedSinceStart = false;     // a buffer has come back since the last start
  int callbacksInFlight = 0;            // callbacks between "found the buffer" and "enqueued/dropped"
  float lastVolume = 1.0f;
  unsigned long generation = 0;         // createQueue() count

  // --- owner thread only (lifecycleMutex)
  bool startFailed = false;    // AudioQueueStart failed last time (log only changes)
  uint64_t lastStartAttemptMs = 0;
  uint64_t lastRecoverMs = 0;
  int noCompletionRecoveries = 0;  // consecutive "no completions" re-acquires (reset by a completion)
  bool yieldLogged = false;        // "yielding" traced once per yield

  // --- ring, under `mutex`
  std::vector<uint8_t> ring;
  size_t ringHead = 0;  // read position
  size_t ringFill = 0;  // bytes available
  bool priming = true;  // play silence until the ring holds kPrimeBytes (start, flush, underrun)
  bool stalled = false; // a feed timed out; cleared by the next completion
  pthread_mutex_t mutex;
  pthread_cond_t spaceAvailable;

  // --- session thread
  pthread_t sessionThread;
  bool sessionThreadStarted = false;
  CFRunLoopRef sessionRunLoop = nullptr;
  bool sessionReady = false;
  bool sessionOk = false;
  pthread_mutex_t readyMutex;
  pthread_cond_t readyCond;
};
