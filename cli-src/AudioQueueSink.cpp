#include "AudioQueueSink.h"

#include <AudioToolbox/AudioServices.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <cstdio>

#include "ios3_trace.h"

AudioQueueSink::AudioQueueSink() : opts() { init(); }
AudioQueueSink::AudioQueueSink(const Options& options) : opts(options) { init(); }

void AudioQueueSink::init() {
  ring.resize(kRingBytes);
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
  destroyQueue();
  if (sessionThreadStarted && sessionRunLoop) {
    CFRunLoopStop(sessionRunLoop);
    pthread_join(sessionThread, nullptr);
  }
  pthread_cond_destroy(&readyCond);
  pthread_mutex_destroy(&readyMutex);
  pthread_mutex_destroy(&queueMutex);
  pthread_cond_destroy(&spaceAvailable);
  pthread_mutex_destroy(&mutex);
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
  IOS3_TRACE("audio route change: '%s' -> '%s' (reason %ld: %s)", oldRoute, currentRoute().c_str(), reason, why);
  reacquireRoute("route change");
}

void AudioQueueSink::onInterruption(UInt32 state) {
  if (state == kAudioSessionBeginInterruption) {
    pthread_mutex_lock(&queueMutex);
    interrupted = true;
    wasRunningBeforeInterruption = running;
    if (queue && running) AudioQueuePause(queue);
    pthread_mutex_unlock(&queueMutex);
    IOS3_TRACE("audio interruption began (was %s)", wasRunningBeforeInterruption ? "playing" : "idle");
  } else if (state == kAudioSessionEndInterruption) {
    OSStatus act = AudioSessionSetActive(true);
    IOS3_TRACE("audio interruption ended: AudioSessionSetActive -> %ld, route '%s'", (long)act, currentRoute().c_str());
    pthread_mutex_lock(&queueMutex);
    interrupted = false;
    bool resume = wasRunningBeforeInterruption;
    pthread_mutex_unlock(&queueMutex);
    if (resume) reacquireRoute("interruption end");
  }
}

// Re-point the output at the current route. Runs on the AudioSession thread; the producer keeps
// filling the ring meanwhile (it only blocks when the ring is full).
void AudioQueueSink::reacquireRoute(const char* why) {
  OSStatus act = AudioSessionSetActive(true);
  pthread_mutex_lock(&queueMutex);
  bool wasRunning = running;
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
               currentRoute().c_str(), wasRunning ? ", resuming" : "");
    if (ok && wasRunning) start();
    return;
  }
  // RECOVER_RESTART
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  if (q && running) { AudioQueueStop(q, true); running = false; }
  pthread_mutex_unlock(&queueMutex);
  IOS3_TRACE("%s: AudioQueue stopped, session active %ld, route '%s'%s", why, (long)act, currentRoute().c_str(),
             wasRunning ? ", restarting" : "");
  if (wasRunning) start();
}

// ---------------------------------------------------------------- queue lifecycle

bool AudioQueueSink::createQueue() {
  pthread_mutex_lock(&queueMutex);
  if (queue) { pthread_mutex_unlock(&queueMutex); return true; }
  AudioQueueRef q = nullptr;
  // NULL run loop: AudioQueue uses its own internal thread for the callbacks.
  IOS3_TRACE("AudioQueueNewOutput begin (%u Hz, %u ch, %u bit)", (unsigned)format.mSampleRate, (unsigned)format.mChannelsPerFrame,
             (unsigned)format.mBitsPerChannel);
  OSStatus st = AudioQueueNewOutput(&format, outputCallback, this, nullptr, nullptr, 0, &q);
  IOS3_TRACE("AudioQueueNewOutput -> %ld", (long)st);
  if (st != 0) { open = false; pthread_mutex_unlock(&queueMutex); return false; }
  for (int i = 0; i < kNumBuffers; i++) {
    st = AudioQueueAllocateBuffer(q, kBufferBytes, &buffers[i]);
    if (st != 0) {
      IOS3_TRACE("AudioQueueAllocateBuffer -> %ld", (long)st);
      AudioQueueDispose(q, true);
      open = false;
      pthread_mutex_unlock(&queueMutex);
      return false;
    }
  }
  AudioQueueSetParameter(q, kAudioQueueParam_Volume, lastVolume);
  for (int i = 0; i < kNumBuffers; i++) { enqueued[i] = false; queuedBytes[i] = 0; }
  queue = q;
  open = true;
  running = false;
  pthread_mutex_unlock(&queueMutex);
  return true;
}

void AudioQueueSink::destroyQueue() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  queue = nullptr;  // callbacks that are still in flight see the mismatch and skip their enqueue
  open = false;
  running = false;
  pthread_mutex_unlock(&queueMutex);
  if (!q) return;
  AudioQueueStop(q, true);     // synchronous: no callbacks after this returns
  AudioQueueDispose(q, true);  // frees the buffers too
  pthread_mutex_lock(&queueMutex);
  for (int i = 0; i < kNumBuffers; i++) { buffers[i] = nullptr; enqueued[i] = false; queuedBytes[i] = 0; }
  pthread_mutex_unlock(&queueMutex);
}

bool AudioQueueSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  pthread_mutex_lock(&queueMutex);
  bool same = open && format.mSampleRate == sampleRate && format.mChannelsPerFrame == channelCount && format.mBitsPerChannel == bitDepth;
  pthread_mutex_unlock(&queueMutex);
  if (same) return true;

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
  return createQueue();
}

void AudioQueueSink::start() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  if (!q || running || interrupted) { pthread_mutex_unlock(&queueMutex); return; }
  running = true;  // set before priming so a callback racing us behaves as "running"
  pthread_mutex_unlock(&queueMutex);
  // Prime every buffer the queue does not own (with silence if nothing is queued yet) and start.
  primeBuffers(q);
  if (!startFailed) IOS3_TRACE("AudioQueueStart begin (route '%s')", currentRoute().c_str());
  OSStatus st = AudioQueueStart(q, nullptr);
  // feedPCMFrames() retries this for every chunk (every ~23 ms) while it fails: log changes only.
  if (st == 0 || !startFailed) IOS3_TRACE("AudioQueueStart -> %ld", (long)st);
  startFailed = st != 0;
  if (st != 0) {
    pthread_mutex_lock(&queueMutex);
    if (queue == q) running = false;
    pthread_mutex_unlock(&queueMutex);
  }
}

void AudioQueueSink::stop() {
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = queue;
  bool r = running;
  running = false;
  pthread_mutex_unlock(&queueMutex);
  if (q && r) AudioQueueStop(q, true);
}

void AudioQueueSink::flush() {
  pthread_mutex_lock(&mutex);
  ringHead = 0;
  ringFill = 0;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_lock(&queueMutex);
  AudioQueueRef q = (queue && running) ? queue : nullptr;
  pthread_mutex_unlock(&queueMutex);
  if (q) {
    // Drops the queued buffers and keeps the queue running. The callbacks for the dropped
    // buffers run inside the reset, where enqueueing is refused (kAudioQueueErr_EnqueueDuringReset),
    // so the queue would be left with nothing to play: re-prime afterwards (a no-op for the
    // buffers it still owns).
    AudioQueueReset(q);
    primeBuffers(q);
  }
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

// Runs on the AudioQueue thread (and inside AudioQueueReset/Stop for the dropped buffers).
void AudioQueueSink::outputCallback(void* userData, AudioQueueRef q, AudioQueueBufferRef buffer) {
  AudioQueueSink* self = static_cast<AudioQueueSink*>(userData);
  int index = -1;
  pthread_mutex_lock(&self->mutex);
  self->stalled = false;
  pthread_mutex_unlock(&self->mutex);
  pthread_mutex_lock(&self->queueMutex);
  if (self->queue == q) {
    for (int i = 0; i < kNumBuffers; i++) {
      if (self->buffers[i] == buffer) { self->enqueued[i] = false; self->queuedBytes[i] = 0; index = i; break; }
    }
  }
  pthread_mutex_unlock(&self->queueMutex);
  if (index >= 0) self->fillBuffer(q, index);  // a dead queue's buffer is just dropped
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
  size_t take = ringFill < want ? ringFill : want;
  take -= take % format.mBytesPerFrame;  // keep whole frames
  size_t first = kRingBytes - ringHead;
  if (first > take) first = take;
  memcpy(out, &ring[ringHead], first);
  if (take > first) memcpy(out + first, &ring[0], take - first);
  ringHead = (ringHead + take) % kRingBytes;
  ringFill -= take;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);

  // Underrun: pad with silence so the queue never starves and stops.
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

void AudioQueueSink::feedPCMFrames(const uint8_t* data, size_t bytes) {
  pthread_mutex_lock(&queueMutex);
  bool haveQueue = open;
  bool needStart = open && !running && !interrupted;
  pthread_mutex_unlock(&queueMutex);
  if (!haveQueue) return;
  if (needStart) start();

  pthread_mutex_lock(&mutex);
  while (bytes > 0) {
    while (ringFill == kRingBytes) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      struct timespec ts = { tv.tv_sec + kStallMs / 1000, tv.tv_usec * 1000 + (kStallMs % 1000) * 1000000L };
      if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      int rc = pthread_cond_timedwait(&spaceAvailable, &mutex, &ts);
      if (rc == ETIMEDOUT && ringFill == kRingBytes) {
        // The queue stopped completing buffers (mediaserverd restart, an interruption we did not
        // get a callback for, ...). Never block the player thread forever: drop this chunk and
        // re-acquire the output once per stall.
        bool first = !stalled;
        stalled = true;
        pthread_mutex_unlock(&mutex);
        if (first) {
          IOS3_TRACE("no buffer completion for %ld ms, re-acquiring the audio output", kStallMs);
          reacquireRoute("stall");
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

void AudioQueueSink::volumeChanged(uint16_t volume) {
  pthread_mutex_lock(&queueMutex);
  lastVolume = volume / 65535.0f;
  if (queue) AudioQueueSetParameter(queue, kAudioQueueParam_Volume, lastVolume);
  pthread_mutex_unlock(&queueMutex);
}
