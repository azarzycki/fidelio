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
  pthread_mutex_init(&lifecycleMutex, nullptr);
  pthread_mutex_init(&mutex, nullptr);
  pthread_cond_init(&spaceAvailable, nullptr);
  pthread_mutex_init(&queueMutex, nullptr);
  pthread_mutex_init(&readyMutex, nullptr);
  pthread_cond_init(&readyCond, nullptr);
  for (int i = 0; i < kNumBuffers; i++) { buffers[i] = nullptr; enqueued[i] = false; queuedBytes[i] = 0; }
  memset(&format, 0, sizeof format);
  softwareVolumeControl = false;  // we use the AudioQueue volume parameter

  // B1.8: bare mode makes no AudioSession call at all; `sessionAvailable` stays false and every
  // call site below checks it. auto/on start the session thread, which retries forever.
  if (opts.session == SESSION_OFF) {
    IOS3_TRACE("AudioQueueSink: audio-session=off: bare AudioQueue, zero AudioSession calls (no route following, no interruption handling%s)",
               opts.route == ROUTE_SPEAKER ? "; --audio-route speaker has no effect" : "");
  } else {
    startSessionThread();
  }

  setParams(44100, 2, 16);
}

AudioQueueSink::~AudioQueueSink() {
  pthread_mutex_lock(&lifecycleMutex);
  destroyQueue();
  pthread_mutex_unlock(&lifecycleMutex);
  if (sessionThreadStarted) {
    pthread_mutex_lock(&readyMutex);
    sessionQuit = true;  // ends the retry wait or the run-loop poll
    pthread_cond_broadcast(&readyCond);
    pthread_mutex_unlock(&readyMutex);
    if (sessionRunLoop) CFRunLoopStop(sessionRunLoop);
    pthread_join(sessionThread, nullptr);
  }
  pthread_cond_destroy(&readyCond);
  pthread_mutex_destroy(&readyMutex);
  pthread_mutex_destroy(&queueMutex);
  pthread_cond_destroy(&spaceAvailable);
  pthread_mutex_destroy(&mutex);
  pthread_mutex_destroy(&lifecycleMutex);
}

// SESSION_ON / SESSION_AUTO: the AudioSession lives on its own run-loop thread (see the header).
// Wait up to kSessionFirstWaitMs for its first AudioSessionInitialize so that, when the session is
// available, the category/listeners are in place before the first AudioQueue is created (the
// pre-B1.8 behaviour, unchanged). If it is not, the owner proceeds: `auto` plays bare meanwhile,
// `on` waits for the session; the thread keeps retrying and a later success upgrades in place.
void AudioQueueSink::startSessionThread() {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 * 1024);
  sessionThreadStarted = pthread_create(&sessionThread, &attr, sessionThreadEntry, this) == 0;
  pthread_attr_destroy(&attr);
  if (!sessionThreadStarted) {
    IOS3_TRACE("AudioQueueSink: cannot start the AudioSession thread (audio-session=%s): %s", sessionModeName(opts.session),
               opts.session == SESSION_ON ? "no audio (on = the session is required)" : "bare AudioQueue (no AudioSession call made)");
    return;
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct timespec ts = { tv.tv_sec + kSessionFirstWaitMs / 1000, tv.tv_usec * 1000 };
  pthread_mutex_lock(&readyMutex);
  while (!sessionReady && pthread_cond_timedwait(&readyCond, &readyMutex, &ts) == 0) {}
  bool ready = sessionReady, ok = sessionOk;
  if (!ok) ownerProceeded = true;  // the thread's eventual success must upgrade the running queue
  pthread_mutex_unlock(&readyMutex);
  if (ok) {
    IOS3_TRACE("AudioQueueSink: AudioSession in use (audio-session=%s)", sessionModeName(opts.session));
    return;
  }
  const char* then = opts.session == SESSION_ON
                         ? "no audio until it succeeds (on = the session is required); retrying with backoff"
                         : "playing through a bare AudioQueue meanwhile (no AudioSession call from the player); retrying with backoff and "
                           "upgrading in place when it succeeds";
  if (!ready) IOS3_TRACE("AudioQueueSink: the first AudioSessionInitialize has not returned after %ld ms (audio-session=%s): %s", kSessionFirstWaitMs,
                         sessionModeName(opts.session), then);
  else IOS3_TRACE("AudioQueueSink: AudioSessionInitialize failed (audio-session=%s): %s", sessionModeName(opts.session), then);
}

bool AudioQueueSink::sessionLive() {
  pthread_mutex_lock(&queueMutex);
  bool live = sessionAvailable;
  pthread_mutex_unlock(&queueMutex);
  return live;
}

int AudioQueueSink::sessionAttempts() {
  pthread_mutex_lock(&readyMutex);
  int n = sessionTries;
  pthread_mutex_unlock(&readyMutex);
  return n;
}

// ---------------------------------------------------------------- AudioSession thread

void* AudioQueueSink::sessionThreadEntry(void* self) {
  static_cast<AudioQueueSink*>(self)->sessionThreadLoop();
  return nullptr;
}

static void fourcc(OSStatus st, char out[5]) {
  for (int i = 0; i < 4; i++) {
    int c = (int)((st >> (24 - 8 * i)) & 0xff);
    out[i] = (c >= 32 && c < 127) ? (char)c : '.';
  }
  out[4] = 0;
}

void AudioQueueSink::sessionThreadLoop() {
  sessionRunLoop = CFRunLoopGetCurrent();
  // 1. AudioSessionInitialize, retried forever with backoff. At cold boot a RunAtLoad daemon
  //    starts before the audio session server is up: the first call hangs past the owner's wait
  //    and returns 'ini?' (1768843583, kAudioSessionInitializationError: the registration RPC
  //    timed out). Seconds later the same call succeeds, so every failure is transient.
  OSStatus st;
  uint64_t backoff = (uint64_t)kSessionRetryMs;
  int attempt = 0;
  for (;;) {
    attempt++;
    pthread_mutex_lock(&readyMutex);
    sessionTries = attempt;
    pthread_mutex_unlock(&readyMutex);
    uint64_t t0 = nowMs();
    st = AudioSessionInitialize(sessionRunLoop, kCFRunLoopDefaultMode, interruptionCallback, this);
    uint64_t took = nowMs() - t0;
    if (st == kAudioSessionAlreadyInitialized) {  // 'init': a previous attempt did register us after all
      IOS3_TRACE("AudioSessionInitialize attempt #%d -> 'init' (already initialized): treating as success", attempt);
      st = 0;
    }
    if (st == 0) break;
    char code[5];
    fourcc(st, code);
    IOS3_TRACE("AudioSession not ready: AudioSessionInitialize attempt #%d -> %ld ('%s') after %llu ms; retrying in %llu ms (%s)", attempt,
               (long)st, code, (unsigned long long)took, (unsigned long long)backoff,
               opts.session == SESSION_ON ? "no audio until it succeeds" : "playing bare meanwhile");
    pthread_mutex_lock(&queueMutex);
    sessionInitFailedPending = true;  // the owner re-creates a bare queue whose start the half-initialized session may have spoiled
    pthread_mutex_unlock(&queueMutex);
    pthread_mutex_lock(&readyMutex);
    sessionReady = true;  // the owner stops waiting after the first failure
    pthread_cond_broadcast(&readyCond);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct timespec ts = { tv.tv_sec + (time_t)(backoff / 1000), tv.tv_usec * 1000 + (long)(backoff % 1000) * 1000000L };
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (!sessionQuit && pthread_cond_timedwait(&readyCond, &readyMutex, &ts) == 0) {}
    bool quit = sessionQuit;
    pthread_mutex_unlock(&readyMutex);
    if (quit) return;
    backoff *= 2;
    if (backoff > (uint64_t)kSessionRetryMaxMs) backoff = kSessionRetryMaxMs;
  }

  // 2. configure the session, then publish it: a late success (the owner already runs a bare
  //    queue) is an in-place upgrade that ensurePlaying() performs on the owner thread.
  UInt32 category = kAudioSessionCategory_MediaPlayback;
  AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof category, &category);
  OSStatus lst = AudioSessionAddPropertyListener(kAudioSessionProperty_AudioRouteChange, routeChangeCallback, this);
  applyRouteOverride();
  OSStatus act = AudioSessionSetActive(true);
  pthread_mutex_lock(&readyMutex);
  bool late = ownerProceeded;
  sessionReady = true;
  sessionOk = true;
  pthread_cond_broadcast(&readyCond);
  pthread_mutex_unlock(&readyMutex);
  pthread_mutex_lock(&queueMutex);
  sessionAvailable = true;
  if (late) sessionUpgradePending = true;
  pthread_mutex_unlock(&queueMutex);
  if (attempt > 1 || late)
    IOS3_TRACE("AudioSession acquired on retry %d: route '%s'; route+interruption listeners attached (listener %ld, active %ld)%s", attempt,
               currentRoute().c_str(), (long)lst, (long)act, late ? "; upgrading the running output in place" : "");
  IOS3_TRACE("AudioSession ready after %d attempt%s%s: route listener %ld, active %ld, route '%s', recovery=%s override=%s", attempt,
             attempt == 1 ? "" : "s", late ? " (late)" : "", (long)lst, (long)act,
             currentRoute().c_str(), opts.recovery == RECOVER_RECREATE ? "recreate" : opts.recovery == RECOVER_RESTART ? "restart" : "none",
             opts.route == ROUTE_SPEAKER ? "speaker" : opts.route == ROUTE_LINEOUT ? "lineout(no override)" : "auto");

  // 3. serve the callbacks until the destructor asks us to quit. Keep the run loop alive even when
  //    AudioSession has nothing scheduled; poll the quit flag once a second (a CFRunLoopStop that
  //    races the start of the loop would otherwise be lost).
  CFRunLoopTimerRef keepAlive = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + 1e9, 1e9, 0, 0, nullptr, nullptr);
  CFRunLoopAddTimer(sessionRunLoop, keepAlive, kCFRunLoopDefaultMode);
  for (;;) {
    pthread_mutex_lock(&readyMutex);
    bool quit = sessionQuit;
    pthread_mutex_unlock(&readyMutex);
    if (quit) break;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
  }
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

bool AudioQueueSink::sessionInUse() { return sessionLive(); }

std::string AudioQueueSink::currentRoute() {
  if (!sessionLive()) return "default";  // bare mode: nothing to ask; the system picks the route
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
  if (!sessionAvailable) { pthread_mutex_unlock(&queueMutex); return; }  // not published yet (still configuring)
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
  if (!sessionLive()) return;
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
  OSStatus act = sessionLive() ? AudioSessionSetActive(true) : 0;  // no session (yet): no session call, just the queue
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
  if (opts.session == SESSION_ON && !sessionAvailable) {  // strict: never play without the session
    pthread_mutex_unlock(&queueMutex);
    if (!waitLogged) { IOS3_TRACE("audio-session=on: playback wanted but the AudioSession is not up yet: waiting for it (no bare fallback)"); waitLogged = true; }
    return;
  }
  waitLogged = false;
  if (!q || running || interrupted) { pthread_mutex_unlock(&queueMutex); return; }
  running = true;  // set before priming so a callback racing us behaves as "running"
  pthread_mutex_unlock(&queueMutex);
  // Prime every buffer the queue does not own (with silence if nothing is queued yet) and start.
  // `q` cannot be disposed meanwhile: only the owner thread does that, and we are it.
  bool live = sessionLive();
  primeBuffers(q);
  // Every attempt is logged (B1.8): a daemon that cannot start must say so on each retry.
  if (!startFailed) IOS3_TRACE("AudioQueueStart begin (route '%s', %s)", currentRoute().c_str(), live ? "AudioSession" : "bare");
  else IOS3_TRACE("AudioQueueStart retry #%d after %d failure%s (route '%s', %s)", startFailures + 1, startFailures, startFailures == 1 ? "" : "s",
                  currentRoute().c_str(), live ? "AudioSession re-activated" : "bare");
  OSStatus st = AudioQueueStart(q, nullptr);
  if (st == 0) {
    IOS3_TRACE("AudioQueueStart -> 0%s", startFailed ? " (recovered)" : "");
    startFailures = 0;
  } else {
    startFailures++;
    IOS3_TRACE("AudioQueueStart -> %ld (failure #%d; next retry in %llu ms%s)", (long)st, startFailures, (unsigned long long)startBackoffMs(),
               startFailures >= kStartRecreateAfter ? ", recreating the queue first" : "");
  }
  startFailed = st != 0;
  pthread_mutex_lock(&queueMutex);
  if (queue == q) {
    if (st != 0) running = false;
    else { startedAtMs = nowMs(); lastCompletionMs = startedAtMs; completedSinceStart = false; }  // the completion watchdog counts from here
  }
  pthread_mutex_unlock(&queueMutex);
}

// Backoff before the next AudioQueueStart after `startFailures` consecutive failures: 250, 500, 1000, 2000 ms.
uint64_t AudioQueueSink::startBackoffMs() const {
  int n = startFailures > 0 ? startFailures - 1 : 0;
  uint64_t b = (uint64_t)kStartRetryMs << (n > 6 ? 6 : n);
  return b > (uint64_t)kStartRetryMaxMs ? (uint64_t)kStartRetryMaxMs : b;
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
  bool firstPending = !completedSinceStart;
  bool haveSession = sessionAvailable, upgrade = sessionUpgradePending, initFailed = sessionInitFailedPending;
  sessionUpgradePending = false;
  sessionInitFailedPending = false;
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
  // 1b. the AudioSession came up after the output was already running bare (B1.8): re-acquire the
  //     output on the real route exactly like a route change. The ring survives, so playback goes
  //     on with the audio in hand; nothing is dropped from the stream.
  if (upgrade) {
    IOS3_TRACE("%s: AudioSession is up (attempt #%d): %s, route '%s', buffered audio kept%s", from, sessionAttempts(),
               have ? "upgrading the bare output in place" : "acquiring the output on it", currentRoute().c_str(), want ? ", resuming" : "");
    reacquireRoute("session upgrade");
    return;
  }
  // 1c. an AudioSessionInitialize attempt just failed (B1.8). If the bare queue is not playing (on
  //     the device the AudioQueueStart issued while that RPC was in flight failed with the session's
  //     own error), throw it away and create a clean one now instead of waiting for the backoff.
  if (initFailed && want && have && !isRunning && !isInterrupted && opts.session != SESSION_ON) {
    IOS3_TRACE("%s: AudioSession attempt failed while the output was not playing: re-creating a clean bare AudioQueue", from);
    startFailed = false;  // a fresh queue, a fresh start
    startFailures = 0;
    reacquireRoute("session init failed");
    return;
  }
  // 2. the route changed: the system has settled on a route, so whatever interrupted us is over
  //    (its end callback never came on the device); re-acquire the output on the new route
  if (routeChange) {
    if (isInterrupted) {
      IOS3_TRACE("route change while interrupted: treating the interruption as ended (no end callback came)");
      pthread_mutex_lock(&queueMutex);
      interrupted = false;
      pthread_mutex_unlock(&queueMutex);
      yieldLogged = false;
      if (want) resumeFromInterruption("route change");
    }
    reacquireRoute("route change");
    return;
  }
  // 3. the interruption ended: if the queue is already running (the watchdog restarted it), only
  //    re-activate the session; otherwise fall through to a plain start (no recreate)
  if (endPending) {
    OSStatus act = haveSession ? AudioSessionSetActive(true) : 0;
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    pthread_mutex_unlock(&queueMutex);
    IOS3_TRACE("interruption end processed: AudioSessionSetActive -> %ld%s", (long)act, isRunning ? ", already playing" : "");
    yieldLogged = false;
    if (isRunning) return;
    if (isInterrupted && want) resumeFromInterruption("interruption end");
    isInterrupted = false;
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
  // 5. running: the real watchdog. A running queue that has completed no buffer for a while is
  //    dead, whether it owns buffers or not (a buffer stranded by a refused enqueue counts as
  //    dead too): re-acquire it. The first completion after AudioQueueStart gets a longer window
  //    (dock line-out / a fresh mediaserverd can be slow), and consecutive recoveries back off so a
  //    queue that starts fine but never renders (a long call with no END callback) does not keep
  //    audiod in a tight recreate loop. Also re-prime: a no-op for buffers the queue owns, it
  //    re-enqueues within one tick any buffer the queue handed back that nobody re-enqueued.
  if (isRunning) {
    if (q) primeBuffers(q);
    if (lastDone > lastRecoverMs && noCompletionRecoveries) noCompletionRecoveries = 0;  // it rendered after the last recovery: healthy again
    uint64_t window = firstPending ? (uint64_t)kFirstCompletionMs : (uint64_t)kNoCompletionMs;
    uint64_t backoff = (uint64_t)kRecoverRetryMs << (noCompletionRecoveries > 4 ? 4 : noCompletionRecoveries);
    if (backoff > (uint64_t)kMaxRecoverBackoffMs) backoff = kMaxRecoverBackoffMs;
    // lastDone is stamped on the queue thread and can be a few ms AHEAD of this tick's `now`:
    // never let the subtraction wrap (seen on the device as a 2^64 ms "no completion").
    uint64_t sinceDone = now > lastDone ? now - lastDone : 0;
    if (lastDone && sinceDone > window && now - lastRecoverMs >= backoff) {
      lastRecoverMs = now;
      noCompletionRecoveries++;
      IOS3_TRACE("%s: queue is running but has completed no buffer for %llu ms%s: re-acquiring (recovery #%d, next after %llu ms)", from,
                 (unsigned long long)sinceDone, firstPending ? " since AudioQueueStart" : "", noCompletionRecoveries,
                 (unsigned long long)(backoff * 2 > (uint64_t)kMaxRecoverBackoffMs ? (uint64_t)kMaxRecoverBackoffMs : backoff * 2));
      reacquireRoute("no completions");
    }
    return;
  }
  // 6. not running: a stuck interruption is forgotten after the grace period -- unless another app
  //    is actually playing (the user pressed play in the Music app): then we yield for as long as it
  //    plays, holding the timer, and resume once it has been quiet for the grace window (or on END).
  if (isInterrupted) {
    if (otherAudioIsPlaying()) {  // checked every tick while interrupted: the grace window only runs while no other audio plays
      pthread_mutex_lock(&queueMutex);
      interruptedSinceMs = now;  // hold: the grace window restarts when the other audio stops
      pthread_mutex_unlock(&queueMutex);
      if (!yieldLogged) { IOS3_TRACE("%s: interrupted and other audio is playing: yielding until it stops", from); yieldLogged = true; }
      return;
    }
    uint64_t held = now > since ? now - since : 0;  // `since` is stamped on the session thread
    if (held < (uint64_t)kInterruptGraceMs) return;  // a real interruption: give its end callback a chance
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    pthread_mutex_unlock(&queueMutex);
    OSStatus act = haveSession ? AudioSessionSetActive(true) : 0;
    IOS3_TRACE("%s: interruption began %llu ms ago and never ended, no other audio playing: clearing it (AudioSessionSetActive -> %ld) and restarting",
               from, (unsigned long long)held, (long)act);
    yieldLogged = false;
    resumeFromInterruption(from);
  }
  // 7. start. After a failure (a genuine interruption keeps it failing; so did the system-context
  //    daemon of B1.8) retry with a 250/500/1000/2000 ms backoff, and from the kStartRecreateAfter-th
  //    consecutive failure on destroy and create the queue before the retry (self-heal).
  if (startFailed) {
    if (now - lastStartAttemptMs < startBackoffMs()) return;
    lastStartAttemptMs = now;
    if (startFailures >= kStartRecreateAfter) {
      IOS3_TRACE("%s: AudioQueueStart failed %d times in a row: recreating the AudioQueue before retry #%d", from, startFailures, startFailures + 1);
      destroyQueue();
      if (!createQueue()) return;  // step 4 re-acquires it on a later tick (rate-limited)
    }
    if (haveSession) AudioSessionSetActive(true);  // with a session, a failed start usually means it was taken away
  } else {
    lastStartAttemptMs = now;
  }
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
  ring.clear();
  priming = true;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
  resetAndPrime();
  pthread_mutex_unlock(&lifecycleMutex);
}

void AudioQueueSink::crossfade(size_t fadeFrames) {
  if (fadeFrames == 0) { flush(); return; }
  pthread_mutex_lock(&mutex);
  ring.crossfade(fadeFrames);  // the tail plays out faded; no AudioQueueReset, so no step
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
}

double AudioQueueSink::bufferedSeconds() {
  pthread_mutex_lock(&mutex);
  size_t fill = ring.fill();
  pthread_mutex_unlock(&mutex);
  size_t bytes = fill;
  pthread_mutex_lock(&queueMutex);
  for (int i = 0; i < kNumBuffers; i++) bytes += queuedBytes[i];
  pthread_mutex_unlock(&queueMutex);
  return format.mBytesPerFrame ? (double)bytes / (format.mBytesPerFrame * format.mSampleRate) : 0;
}

bool AudioQueueSink::takeResumed() {
  pthread_mutex_lock(&queueMutex);
  bool r = resumedPending;
  resumedPending = false;
  pthread_mutex_unlock(&queueMutex);
  return r;
}

bool AudioQueueSink::isConsuming() {
  pthread_mutex_lock(&queueMutex);
  bool c = running && !interrupted;
  pthread_mutex_unlock(&queueMutex);
  return c;
}

bool AudioQueueSink::otherAudioIsPlaying() {
  if (!sessionLive()) return false;  // no session: nobody to ask, never yield
  UInt32 v = 0, size = sizeof v;
  if (AudioSessionGetProperty(kAudioSessionProperty_OtherAudioIsPlaying, &size, &v) != 0) return false;
  return v != 0;
}

// The audio in the ring (and whatever the caller buffered) is from before the interruption: drop
// it so playback resumes with current audio instead of a stale half second.
void AudioQueueSink::resumeFromInterruption(const char* why) {
  pthread_mutex_lock(&mutex);
  ring.clear();
  ring.fadeInNext(kResumeFadeFrames);  // the re-prime after a restart must not click either
  priming = true;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_lock(&queueMutex);
  resumedPending = true;
  pthread_mutex_unlock(&queueMutex);
  IOS3_TRACE("%s: resuming; buffered audio from before the interruption dropped", why);
}

unsigned long AudioQueueSink::queueGeneration() {
  pthread_mutex_lock(&queueMutex);
  unsigned long g = generation;
  pthread_mutex_unlock(&queueMutex);
  return g;
}

void AudioQueueSink::volumeChanged(uint16_t volume) { setGain(volume / 65535.0f); }

void AudioQueueSink::setGain(float linear) {
  if (!(linear >= 0.0f)) linear = 0.0f;
  if (linear > 1.0f) linear = 1.0f;
  pthread_mutex_lock(&queueMutex);
  lastVolume = linear;
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
    while (ring.space() == 0) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      struct timespec ts = { tv.tv_sec + kStallMs / 1000, tv.tv_usec * 1000 + (kStallMs % 1000) * 1000000L };
      if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      int rc = pthread_cond_timedwait(&spaceAvailable, &mutex, &ts);
      if (rc == ETIMEDOUT && ring.space() == 0) {
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
    size_t n = ring.feed(data, bytes);  // applies a pending fade-in / crossfade mix
    if (n == 0) { bytes -= bytes % 4; if (bytes == 0) break; continue; }
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
  long firstAfterMs = -1;
  pthread_mutex_lock(&self->queueMutex);
  if (self->queue == q) {
    for (int i = 0; i < kNumBuffers; i++) {
      if (self->buffers[i] == buffer) { self->enqueued[i] = false; self->queuedBytes[i] = 0; index = i; break; }
    }
    if (index >= 0) {
      self->lastCompletionMs = nowMs();
      self->callbacksInFlight++;
      if (!self->completedSinceStart) {
        self->completedSinceStart = true;
        firstAfterMs = (long)(self->lastCompletionMs - self->startedAtMs);
      }
    }
  }
  pthread_mutex_unlock(&self->queueMutex);
  if (firstAfterMs >= 0) IOS3_TRACE("first buffer completion %ld ms after AudioQueueStart", firstAfterMs);
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
  // Claim the buffer here, under the lock (enqueued = "not ours to fill"): the owner's
  // primeBuffers() and a late callback for the same buffer can otherwise both pass the check
  // and write the same mAudioData (B1.3 #1). Reverted below if the enqueue is refused.
  bool owned = queue == q && buffers[index] && !enqueued[index];
  AudioQueueBufferRef buffer = owned ? buffers[index] : nullptr;
  if (buffer) enqueued[index] = true;
  pthread_mutex_unlock(&queueMutex);
  if (!buffer) return;
  uint8_t* out = static_cast<uint8_t*>(buffer->mAudioData);
  size_t want = buffer->mAudioDataBytesCapacity;

  pthread_mutex_lock(&mutex);
  if (priming && ring.fill() >= kPrimeBytes) priming = false;
  size_t take = priming ? 0 : ring.take(out, want);
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
    if (st == 0) queuedBytes[index] = take;
    else {
      enqueued[index] = false;  // ours again; the next primeBuffers() (every tick while running) enqueues it
      // kAudioQueueErr_EnqueueDuringReset (-66632) is expected inside AudioQueueReset/Stop. Its data is lost either way.
      if (st != kAudioQueueErr_EnqueueDuringReset) IOS3_TRACE("AudioQueueEnqueueBuffer -> %ld", (long)st);
    }
  } else if (buffers[index] == buffer) {
    enqueued[index] = false;
  }
  pthread_mutex_unlock(&queueMutex);
}
