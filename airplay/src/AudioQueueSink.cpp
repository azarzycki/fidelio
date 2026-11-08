#include "AudioQueueSink.h"

#include <AudioToolbox/AudioServices.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

#include "ios3_trace.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

uint64_t AudioQueueSink::nowMs() {
#ifdef __APPLE__
  // Monotonic: the wall clock jumps by years when the phone picks up NTP after a boot.
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  return (t / tb.denom) * tb.numer / 1000000ull + ((t % tb.denom) * tb.numer / tb.denom) / 1000000ull;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

AudioQueueSink::AudioQueueSink() : opts() { init(); }
AudioQueueSink::AudioQueueSink(const Options& options) : opts(options) { init(); }

void AudioQueueSink::init() {
  ring.resize(kRingBytes);
  pthread_mutex_init(&lifecycleMutex, nullptr);
  pthread_mutex_init(&mutex, nullptr);
  pthread_cond_init(&spaceAvailable, nullptr);
  pthread_mutex_init(&queueMutex, nullptr);
  pthread_mutex_init(&readyMutex, nullptr);
  pthread_cond_init(&readyCond, nullptr);
  for (int i = 0; i < kNumBuffers; i++) { buffers[i] = nullptr; enqueued[i] = false; queuedBytes[i] = 0; }
  memset(&format, 0, sizeof format);
  softwareVolumeControl = false;  // we use the AudioQueue volume parameter

  // The AudioSession lives on its own run-loop thread (see the header). Wait up to 2 s for it so
  // the category/listeners are in place before the first AudioQueue is created.
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 * 1024);
  sessionThreadStarted = pthread_create(&sessionThread, &attr, sessionThreadEntry, this) == 0;
  pthread_attr_destroy(&attr);
  if (sessionThreadStarted) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct timespec ts = { tv.tv_sec + 2, tv.tv_usec * 1000 };
    pthread_mutex_lock(&readyMutex);
    while (!sessionReady && pthread_cond_timedwait(&readyCond, &readyMutex, &ts) == 0) {}
    pthread_mutex_unlock(&readyMutex);
    if (!sessionReady) IOS3_TRACE("AudioQueueSink: AudioSession thread did not come up in 2 s; continuing without route following");
  } else {
    IOS3_TRACE("AudioQueueSink: cannot start the AudioSession thread; continuing without route following");
  }

  setParams(44100, 2, 16);
}

AudioQueueSink::~AudioQueueSink() {
  pthread_mutex_lock(&lifecycleMutex);
  destroyQueue();
  pthread_mutex_unlock(&lifecycleMutex);
  if (sessionThreadStarted && sessionRunLoop) {
    CFRunLoopStop(sessionRunLoop);
    pthread_join(sessionThread, nullptr);
  }
  pthread_cond_destroy(&readyCond);
  pthread_mutex_destroy(&readyMutex);
  pthread_mutex_destroy(&queueMutex);
  pthread_cond_destroy(&spaceAvailable);
  pthread_mutex_destroy(&mutex);
  pthread_mutex_destroy(&lifecycleMutex);
}

// ---------------------------------------------------------------- AudioSession thread

void* AudioQueueSink::sessionThreadEntry(void* self) {
  static_cast<AudioQueueSink*>(self)->sessionThreadLoop();
  return nullptr;
}

void AudioQueueSink::sessionThreadLoop() {
  sessionRunLoop = CFRunLoopGetCurrent();
  OSStatus st = AudioSessionInitialize(sessionRunLoop, kCFRunLoopDefaultMode, interruptionCallback, this);
  if (st == 0) {
    UInt32 category = kAudioSessionCategory_MediaPlayback;
    AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof category, &category);
    OSStatus lst = AudioSessionAddPropertyListener(kAudioSessionProperty_AudioRouteChange, routeChangeCallback, this);
    applyRouteOverride();
    OSStatus act = AudioSessionSetActive(true);
    sessionOk = true;
    IOS3_TRACE("AudioSession ready: route listener %ld, active %ld, route '%s', recovery=%s override=%s", (long)lst, (long)act,
               currentRoute().c_str(),
               opts.recovery == RECOVER_RECREATE ? "recreate" : opts.recovery == RECOVER_RESTART ? "restart" : "none",
               opts.route == ROUTE_SPEAKER ? "speaker" : opts.route == ROUTE_LINEOUT ? "lineout(no override)" : "auto");
  } else {
    // A command-line process without a bundle may be refused; audio still works, routes are not followed.
    IOS3_TRACE("AudioSessionInitialize -> %ld (no route following / interruption handling)", (long)st);
  }
  pthread_mutex_lock(&readyMutex);
  sessionReady = true;
  pthread_cond_broadcast(&readyCond);
  pthread_mutex_unlock(&readyMutex);
  if (st != 0) return;

  // Keep the run loop alive even when AudioSession has nothing scheduled.
  CFRunLoopTimerRef keepAlive = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + 1e9, 1e9, 0, 0, nullptr, nullptr);
  CFRunLoopAddTimer(sessionRunLoop, keepAlive, kCFRunLoopDefaultMode);
  CFRunLoopRun();  // until CFRunLoopStop from the destructor
  CFRunLoopRemoveTimer(sessionRunLoop, keepAlive, kCFRunLoopDefaultMode);
  CFRelease(keepAlive);
}

void AudioQueueSink::applyRouteOverride() {
  // iPhone OS 3 only knows "none" and "speaker": with no override the system picks the dock's
  // line-out when docked and the built-in speaker otherwise, which is exactly "follow the route".
  UInt32 ov = opts.route == ROUTE_SPEAKER ? kAudioSessionOverrideAudioRoute_Speaker : kAudioSessionOverrideAudioRoute_None;
  OSStatus st = AudioSessionSetProperty(kAudioSessionProperty_OverrideAudioRoute, sizeof ov, &ov);
  if (st != 0) IOS3_TRACE("OverrideAudioRoute -> %ld", (long)st);
}

std::string AudioQueueSink::currentRoute() {
  CFStringRef route = nullptr;
  UInt32 size = sizeof route;
  if (AudioSessionGetProperty(kAudioSessionProperty_AudioRoute, &size, &route) != 0 || !route) return "";
  char buf[64];
  std::string s = CFStringGetCString(route, buf, sizeof buf, kCFStringEncodingUTF8) ? buf : "?";
  CFRelease(route);
  return s;
}

void AudioQueueSink::interruptionCallback(void* ud, UInt32 state) {
  static_cast<AudioQueueSink*>(ud)->onInterruption(state);
}

void AudioQueueSink::routeChangeCallback(void* ud, AudioSessionPropertyID id, UInt32, const void* data) {
  if (id == kAudioSessionProperty_AudioRouteChange) static_cast<AudioQueueSink*>(ud)->onRouteChange(data);
}

// Flags only: the owner thread pauses/recreates/restarts in ensurePlaying() (within one tick in
// audiod, on the next feed in a producer's --direct-audio mode).
void AudioQueueSink::onInterruption(UInt32 state) {
  pthread_mutex_lock(&queueMutex);
  if (state == kAudioSessionBeginInterruption) {
    interrupted = true;
    pausedForInterruption = false;
    interruptedSinceMs = nowMs();
    bool was = running || wantPlaying;
    pthread_mutex_unlock(&queueMutex);
    IOS3_TRACE("audio interruption began (was %s)", was ? "playing" : "idle");
  } else if (state == kAudioSessionEndInterruption) {
    interruptionEndPending = true;
    pthread_mutex_unlock(&queueMutex);
    IOS3_TRACE("audio interruption ended (queued for the owner thread)");
  } else {
    pthread_mutex_unlock(&queueMutex);
  }
}

void AudioQueueSink::onRouteChange(const void* data) {
  long reason = -1;
  char oldRoute[64] = "?";
  if (data) {
    CFDictionaryRef dict = static_cast<CFDictionaryRef>(data);
    CFNumberRef n = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, CFSTR(kAudioSession_AudioRouteChangeKey_Reason)));
    if (n) { SInt32 r = -1; CFNumberGetValue(n, kCFNumberSInt32Type, &r); reason = r; }
    CFStringRef o = static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR(kAudioSession_AudioRouteChangeKey_OldRoute)));
    if (o) CFStringGetCString(o, oldRoute, sizeof oldRoute, kCFStringEncodingUTF8);
  }
  static const char* reasons[] = { "unknown", "new device available", "old device unavailable", "category change", "override", "?",
                                   "wake from sleep", "no suitable route for category" };
  const char* why = (reason >= 0 && reason < 8) ? reasons[reason] : "?";
  IOS3_TRACE("audio route change: '%s' -> '%s' (reason %ld: %s); queued for the owner thread", oldRoute, currentRoute().c_str(), reason, why);
  pthread_mutex_lock(&queueMutex);
  routeChangePending = true;
  pthread_mutex_unlock(&queueMutex);
}

// ---------------------------------------------------------------- owner thread: lifecycle (lifecycleMutex held)

// Re-point the output at the current route and resume if playback is wanted.
void AudioQueueSink::reacquireRoute(const char* why) {
  OSStatus act = AudioSessionSetActive(true);
  pthread_mutex_lock(&queueMutex);
  bool want = wantPlaying;
  RouteRecovery mode = opts.recovery;
  pthread_mutex_unlock(&queueMutex);
  if (mode == RECOVER_NONE) {
    IOS3_TRACE("%s: recovery=none, AudioSessionSetActive -> %ld, leaving the queue as it is", why, (long)act);
    return;
  }
  if (mode == RECOVER_RECREATE) {
    destroyQueue();
    bool ok = createQueue();
    IOS3_TRACE("%s: AudioQueue recreated (%s), session active %ld, route '%s'%s", why, ok ? "ok" : "FAILED", (long)act,
               currentRoute().c_str(), want ? ", resuming" : "");
    if (ok && want) start();
    return;
  }
  // RECOVER_RESTART
  stopQueue();
  IOS3_TRACE("%s: AudioQueue stopped, session active %ld, route '%s'%s", why, (long)act, currentRoute().c_str(), want ? ", restarting" : "");
  if (want) start();
}

bool AudioQueueSink::createQueue() {
  pthread_mutex_lock(&queueMutex);
  if (queue) { pthread_mutex_unlock(&queueMutex); return true; }
  pthread_mutex_unlock(&queueMutex);
  AudioQueueRef q = nullptr;
  // NULL run loop: AudioQueue uses its own internal thread for the callbacks. Nothing else can
  // touch `queue` meanwhile: only the owner thread (us) creates and destroys it.
  IOS3_TRACE("AudioQueueNewOutput begin (%u Hz, %u ch, %u bit)", (unsigned)format.mSampleRate, (unsigned)format.mChannelsPerFrame,
             (unsigned)format.mBitsPerChannel);
  OSStatus st = AudioQueueNewOutput(&format, outputCallback, this, nullptr, nullptr, 0, &q);
  IOS3_TRACE("AudioQueueNewOutput -> %ld", (long)st);
  if (st != 0) return false;
  AudioQueueBufferRef bufs[kNumBuffers];
  for (int i = 0; i < kNumBuffers; i++) {
    st = AudioQueueAllocateBuffer(q, kBufferBytes, &bufs[i]);
    if (st != 0) {
      IOS3_TRACE("AudioQueueAllocateBuffer -> %ld", (long)st);
      AudioQueueDispose(q, true);
      return false;
    }
  }
  pthread_mutex_lock(&queueMutex);
  AudioQueueSetParameter(q, kAudioQueueParam_Volume, lastVolume);
  for (int i = 0; i < kNumBuffers; i++) { buffers[i] = bufs[i]; enqueued[i] = false; queuedBytes[i] = 0; }
  queue = q;
  open = true;
  running = false;
  lastCompletionMs = 0;
  generation++;
  pthread_mutex_unlock(&queueMutex);
  return true;
}

void AudioQueueSink::destroyQueue() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  // Null the queue AND forget its buffers in the same critical section: a callback that is still
  // in flight compares queue == q first and drops its buffer; nothing can clobber a successor's
  // buffers[] later (the B1.1 race).
  queue = nullptr;
  open = false;
  running = false;
  pausedForInterruption = false;
  for (int i = 0; i < kNumBuffers; i++) { buffers[i] = nullptr; enqueued[i] = false; queuedBytes[i] = 0; }
  pthread_mutex_unlock(&queueMutex);
  if (!q) return;
  AudioQueueStop(q, true);  // synchronous; the buffers it hands back are dropped (queue mismatch)
  // A callback may still be between "found the buffer" and "enqueue refused": let it finish
  // before the buffers are freed.
  for (int spins = 0; spins < 500; spins++) {
    pthread_mutex_lock(&queueMutex);
    int inflight = callbacksInFlight;
    pthread_mutex_unlock(&queueMutex);
    if (inflight == 0) break;
    usleep(1000);
  }
  AudioQueueDispose(q, true);  // frees the buffers too
}

bool AudioQueueSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  pthread_mutex_lock(&lifecycleMutex);
  pthread_mutex_lock(&queueMutex);
  bool same = open && format.mSampleRate == sampleRate && format.mChannelsPerFrame == channelCount && format.mBitsPerChannel == bitDepth;
  pthread_mutex_unlock(&queueMutex);
  if (same) { pthread_mutex_unlock(&lifecycleMutex); return true; }

  destroyQueue();
  memset(&format, 0, sizeof format);
  format.mSampleRate = sampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  format.mChannelsPerFrame = channelCount;
  format.mBitsPerChannel = bitDepth;
  format.mBytesPerFrame = channelCount * (bitDepth / 8);
  format.mFramesPerPacket = 1;
  format.mBytesPerPacket = format.mBytesPerFrame;
  bool ok = createQueue();
  pthread_mutex_unlock(&lifecycleMutex);
  return ok;
}

void AudioQueueSink::start() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  if (!q || running || interrupted) { pthread_mutex_unlock(&queueMutex); return; }
  running = true;  // set before priming so a callback racing us behaves as "running"
  pthread_mutex_unlock(&queueMutex);
  // Prime every buffer the queue does not own (with silence if nothing is queued yet) and start.
  // `q` cannot be disposed meanwhile: only the owner thread does that, and we are it.
  primeBuffers(q);
  if (!startFailed) IOS3_TRACE("AudioQueueStart begin (route '%s')", currentRoute().c_str());
  OSStatus st = AudioQueueStart(q, nullptr);
  // Retried every kStartRetryMs while it fails: log changes only.
  if (st == 0 || !startFailed) IOS3_TRACE("AudioQueueStart -> %ld", (long)st);
  startFailed = st != 0;
  pthread_mutex_lock(&queueMutex);
  if (queue == q) {
    if (st != 0) running = false;
    else lastCompletionMs = nowMs();  // the completion watchdog counts from here
  }
  pthread_mutex_unlock(&queueMutex);
}

void AudioQueueSink::stopQueue() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  bool r = running;
  running = false;
  pausedForInterruption = false;
  pthread_mutex_unlock(&queueMutex);
  if (q && r) AudioQueueStop(q, true);
}

void AudioQueueSink::resetAndPrime() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = (queue && running) ? queue : nullptr;
  pthread_mutex_unlock(&queueMutex);
  if (!q) return;
  // Drops the queued buffers and keeps the queue running. The callbacks for the dropped buffers
  // run inside the reset, where enqueueing is refused (kAudioQueueErr_EnqueueDuringReset), so the
  // queue would be left with nothing to play: re-prime afterwards (a no-op for buffers it owns).
  AudioQueueReset(q);
  primeBuffers(q);
}

// The invariant: playback wanted -> the queue runs and completes buffers, within ~1 s, whatever
// callbacks did or did not arrive. All flag processing happens here, on the owner thread.
void AudioQueueSink::ensurePlaying(const char* from) {
  uint64_t now = nowMs();
  pthread_mutex_lock(&queueMutex);
  bool want = wantPlaying, have = open, isRunning = running, isInterrupted = interrupted, paused = pausedForInterruption;
  bool routeChange = routeChangePending, endPending = interruptionEndPending;
  routeChangePending = false;
  interruptionEndPending = false;
  uint64_t since = interruptedSinceMs, lastDone = lastCompletionMs;
  bool anyEnqueued = false;
  for (int i = 0; i < kNumBuffers; i++) anyEnqueued = anyEnqueued || enqueued[i];
  AudioQueueRef q = queue;
  pthread_mutex_unlock(&queueMutex);

  // 1. an interruption began: pause once (the system has silenced us anyway) and wait for its end
  if (isInterrupted && isRunning && !paused && q) {
    AudioQueuePause(q);
    pthread_mutex_lock(&queueMutex);
    if (queue == q) { running = false; pausedForInterruption = true; }
    pthread_mutex_unlock(&queueMutex);
    isRunning = false;
  }
  // 2. the route changed: the system has settled on a route, so whatever interrupted us is over
  //    (its end callback never came on the device); re-acquire the output on the new route
  if (routeChange) {
    if (isInterrupted) IOS3_TRACE("route change while interrupted: treating the interruption as ended (no end callback came)");
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    pthread_mutex_unlock(&queueMutex);
    reacquireRoute("route change");
    return;
  }
  // 3. the interruption ended: if the queue is already running (the watchdog restarted it), only
  //    re-activate the session; otherwise fall through to a plain start (no recreate)
  if (endPending) {
    OSStatus act = AudioSessionSetActive(true);
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    pthread_mutex_unlock(&queueMutex);
    isInterrupted = false;
    IOS3_TRACE("interruption end processed: AudioSessionSetActive -> %ld%s", (long)act, isRunning ? ", already playing" : "");
    if (isRunning) return;
  }
  if (!want) return;
  // 4. no queue at all: re-acquire (rate-limited)
  if (!have) {
    if (now - lastRecoverMs < (uint64_t)kRecoverRetryMs) return;
    lastRecoverMs = now;
    IOS3_TRACE("%s: playback wanted but there is no AudioQueue: re-acquiring", from);
    reacquireRoute("no queue");
    return;
  }
  // 5. running: the real watchdog. A queue that owns buffers but has completed none for
  //    kNoCompletionMs is dead (mediaserverd restart / silent wedge): re-acquire it.
  if (isRunning) {
    if (anyEnqueued && lastDone && now - lastDone > (uint64_t)kNoCompletionMs && now - lastRecoverMs >= (uint64_t)kRecoverRetryMs) {
      lastRecoverMs = now;
      IOS3_TRACE("%s: queue is running but has completed no buffer for %llu ms: re-acquiring", from, (unsigned long long)(now - lastDone));
      reacquireRoute("no completions");
    }
    return;
  }
  // 6. not running: a stuck interruption is forgotten after the grace period
  if (isInterrupted) {
    if (now - since < (uint64_t)kInterruptGraceMs) return;  // a real interruption: give its end callback a chance
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    pthread_mutex_unlock(&queueMutex);
    OSStatus act = AudioSessionSetActive(true);
    IOS3_TRACE("%s: interruption began %llu ms ago and never ended: clearing it (AudioSessionSetActive -> %ld) and restarting", from,
               (unsigned long long)(now - since), (long)act);
  }
  // 7. start, retrying every kStartRetryMs while it fails (a genuine interruption keeps it failing)
  if (startFailed && now - lastStartAttemptMs < (uint64_t)kStartRetryMs) return;
  lastStartAttemptMs = now;
  if (startFailed) AudioSessionSetActive(true);  // a failed start usually means the session was taken away
  start();
}

// ---------------------------------------------------------------- owner-thread entry points

void AudioQueueSink::tick() {
  pthread_mutex_lock(&lifecycleMutex);
  ensurePlaying("tick");
  pthread_mutex_unlock(&lifecycleMutex);
}

void AudioQueueSink::stop() {
  pthread_mutex_lock(&lifecycleMutex);
  pthread_mutex_lock(&queueMutex);
  wantPlaying = false;
  pthread_mutex_unlock(&queueMutex);
  stopQueue();
  pthread_mutex_unlock(&lifecycleMutex);
}

void AudioQueueSink::flush() {
  pthread_mutex_lock(&lifecycleMutex);
  pthread_mutex_lock(&mutex);
  ringHead = 0;
  ringFill = 0;
  priming = true;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
  resetAndPrime();
  pthread_mutex_unlock(&lifecycleMutex);
}

double AudioQueueSink::bufferedSeconds() {
  pthread_mutex_lock(&mutex);
  size_t fill = ringFill;
  pthread_mutex_unlock(&mutex);
  size_t bytes = fill;
  pthread_mutex_lock(&queueMutex);
  for (int i = 0; i < kNumBuffers; i++) bytes += queuedBytes[i];
  pthread_mutex_unlock(&queueMutex);
  return format.mBytesPerFrame ? (double)bytes / (format.mBytesPerFrame * format.mSampleRate) : 0;
}

unsigned long AudioQueueSink::queueGeneration() {
  pthread_mutex_lock(&queueMutex);
  unsigned long g = generation;
  pthread_mutex_unlock(&queueMutex);
  return g;
}

void AudioQueueSink::volumeChanged(uint16_t volume) {
  pthread_mutex_lock(&queueMutex);
  lastVolume = volume / 65535.0f;
  if (queue) AudioQueueSetParameter(queue, kAudioQueueParam_Volume, lastVolume);  // `queue` is live while non-null under this lock
  pthread_mutex_unlock(&queueMutex);
}

void AudioQueueSink::feedPCMFrames(const uint8_t* data, size_t bytes) {
  pthread_mutex_lock(&lifecycleMutex);
  pthread_mutex_lock(&queueMutex);
  wantPlaying = true;
  pthread_mutex_unlock(&queueMutex);
  ensurePlaying("feed");
  pthread_mutex_lock(&queueMutex);
  bool haveQueue = open;
  pthread_mutex_unlock(&queueMutex);
  pthread_mutex_unlock(&lifecycleMutex);
  if (!haveQueue) return;  // ensurePlaying() is re-acquiring it; this chunk is dropped

  pthread_mutex_lock(&mutex);
  while (bytes > 0) {
    while (ringFill == kRingBytes) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      struct timespec ts = { tv.tv_sec + kStallMs / 1000, tv.tv_usec * 1000 + (kStallMs % 1000) * 1000000L };
      if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      int rc = pthread_cond_timedwait(&spaceAvailable, &mutex, &ts);
      if (rc == ETIMEDOUT && ringFill == kRingBytes) {
        // Direct mode only (audiod never fills the ring: its pump stops at 0.5 s and tick() runs
        // the completion watchdog). The queue stopped completing buffers: never block the player
        // thread forever; drop this chunk and re-acquire the output once per stall.
        bool first = !stalled;
        stalled = true;
        pthread_mutex_unlock(&mutex);
        if (first) {
          IOS3_TRACE("no buffer completion for %ld ms, re-acquiring the audio output", kStallMs);
          pthread_mutex_lock(&lifecycleMutex);
          reacquireRoute("stall");
          pthread_mutex_unlock(&lifecycleMutex);
        }
        return;
      }
    }
    size_t space = kRingBytes - ringFill;
    size_t n = bytes < space ? bytes : space;
    size_t tail = (ringHead + ringFill) % kRingBytes;
    size_t first = kRingBytes - tail;
    if (first > n) first = n;
    memcpy(&ring[tail], data, first);
    if (n > first) memcpy(&ring[0], data + first, n - first);
    ringFill += n;
    data += n;
    bytes -= n;
  }
  pthread_mutex_unlock(&mutex);
}

// ---------------------------------------------------------------- AudioQueue thread

// Runs on the AudioQueue thread (and inside AudioQueueReset/Stop for the dropped buffers).
void AudioQueueSink::outputCallback(void* userData, AudioQueueRef q, AudioQueueBufferRef buffer) {
  AudioQueueSink* self = static_cast<AudioQueueSink*>(userData);
  int index = -1;
  pthread_mutex_lock(&self->queueMutex);
  if (self->queue == q) {
    for (int i = 0; i < kNumBuffers; i++) {
      if (self->buffers[i] == buffer) { self->enqueued[i] = false; self->queuedBytes[i] = 0; index = i; break; }
    }
    if (index >= 0) { self->lastCompletionMs = nowMs(); self->callbacksInFlight++; }
  }
  pthread_mutex_unlock(&self->queueMutex);
  pthread_mutex_lock(&self->mutex);
  self->stalled = false;
  pthread_mutex_unlock(&self->mutex);
  if (index < 0) return;  // a dead queue's buffer is just dropped
  self->fillBuffer(q, index);
  pthread_mutex_lock(&self->queueMutex);
  self->callbacksInFlight--;
  pthread_mutex_unlock(&self->queueMutex);
}

void AudioQueueSink::primeBuffers(AudioQueueRef q) {
  for (int i = 0; i < kNumBuffers; i++) fillBuffer(q, i);
}

void AudioQueueSink::fillBuffer(AudioQueueRef q, int index) {
  pthread_mutex_lock(&queueMutex);
  bool owned = queue == q && buffers[index] && !enqueued[index];
  AudioQueueBufferRef buffer = owned ? buffers[index] : nullptr;
  pthread_mutex_unlock(&queueMutex);
  if (!buffer) return;
  uint8_t* out = static_cast<uint8_t*>(buffer->mAudioData);
  size_t want = buffer->mAudioDataBytesCapacity;

  pthread_mutex_lock(&mutex);
  if (priming && ringFill >= kPrimeBytes) priming = false;
  size_t take = priming ? 0 : (ringFill < want ? ringFill : want);
  take -= take % format.mBytesPerFrame;  // keep whole frames
  size_t first = kRingBytes - ringHead;
  if (first > take) first = take;
  memcpy(out, &ring[ringHead], first);
  if (take > first) memcpy(out + first, &ring[0], take - first);
  ringHead = (ringHead + take) % kRingBytes;
  ringFill -= take;
  // Underrun: pad with silence so the queue never starves and stops, then keep padding until the
  // ring holds kPrimeBytes again. A producer that only keeps up with real time (audiod fed over
  // the socket) would otherwise get a hole in every other 46 ms buffer instead of one short gap.
  if (take < want) priming = true;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);

  if (take < want) memset(out + take, 0, want - take);
  buffer->mAudioDataByteSize = want;

  pthread_mutex_lock(&queueMutex);
  if (queue == q) {
    OSStatus st = AudioQueueEnqueueBuffer(q, buffer, 0, nullptr);
    if (st == 0) { enqueued[index] = true; queuedBytes[index] = take; }
    // kAudioQueueErr_EnqueueDuringReset (-66632) is expected inside AudioQueueReset/Stop: the
    // buffer is ours again and the next primeBuffers() enqueues it. Its data is lost either way.
    else if (st != kAudioQueueErr_EnqueueDuringReset) IOS3_TRACE("AudioQueueEnqueueBuffer -> %ld", (long)st);
  }
  pthread_mutex_unlock(&queueMutex);
}
