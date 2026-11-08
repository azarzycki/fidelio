#include "mock_audiotoolbox.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- CF
static __CFString gDefaultMode = { "kCFRunLoopDefaultMode" };
CFStringRef kCFRunLoopDefaultMode = &gDefaultMode;
CFStringRef mock_cfstr(const char* literal) { return new __CFString{ literal }; }  // leaks on purpose: literals

struct __CFRunLoop {
  std::mutex m;
  std::condition_variable cv;
  bool stop = false;
};
static thread_local __CFRunLoop* tlRunLoop = nullptr;
CFRunLoopRef CFRunLoopGetCurrent() { if (!tlRunLoop) tlRunLoop = new __CFRunLoop; return tlRunLoop; }
void CFRunLoopRun() {
  __CFRunLoop* rl = CFRunLoopGetCurrent();
  std::unique_lock<std::mutex> l(rl->m);
  rl->cv.wait(l, [&] { return rl->stop; });
  rl->stop = false;
}
void CFRunLoopStop(CFRunLoopRef rl) { std::lock_guard<std::mutex> l(rl->m); rl->stop = true; rl->cv.notify_all(); }
struct __CFRunLoopTimer { int dummy; };
CFRunLoopTimerRef CFRunLoopTimerCreate(CFAllocatorRef, CFAbsoluteTime, CFTimeInterval, CFOptionFlags, CFIndex, CFRunLoopTimerCallBack, void*) {
  return new __CFRunLoopTimer;
}
void CFRunLoopAddTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef) {}
void CFRunLoopRemoveTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef) {}
CFAbsoluteTime CFAbsoluteTimeGetCurrent() { struct timeval tv; gettimeofday(&tv, nullptr); return tv.tv_sec + tv.tv_usec / 1e6; }
void CFRelease(CFTypeRef) {}
Boolean CFStringGetCString(CFStringRef s, char* buf, CFIndex size, CFStringEncoding) {
  if (!s || !s->s) return 0;
  strncpy(buf, s->s, (size_t)size - 1);
  buf[size - 1] = 0;
  return 1;
}
const void* CFDictionaryGetValue(CFDictionaryRef, const void*) { return nullptr; }
Boolean CFNumberGetValue(CFNumberRef, CFNumberType, void*) { return 0; }

// ---------------------------------------------------------------- AudioQueue
struct OpaqueAudioQueue {
  AudioQueueOutputCallback cb;
  void* ud;
  std::mutex m;
  std::deque<AudioQueueBufferRef> queued;
  std::vector<AudioQueueBufferRef> buffers;
  bool running = false, resetting = false, disposed = false;
  std::thread worker;
};

static std::mutex gMock;
static OpaqueAudioQueue* gLive = nullptr;
static std::atomic<int> gStartCalls{0}, gStartFailures{0}, gQueues{0};
static std::atomic<bool> gStartFails{false};
static std::atomic<unsigned long> gCompleted{0};
static std::string gRoute = "USB";
static AudioSessionInterruptionListener gIntListener = nullptr;
static void* gIntUd = nullptr;
static AudioSessionPropertyListener gRouteListener = nullptr;
static void* gRouteUd = nullptr;

static void workerLoop(OpaqueAudioQueue* q) {
  for (;;) {
    usleep(10000);  // "plays" one buffer per 10 ms: faster than real time, enough to keep the sink's ring moving
    AudioQueueBufferRef b = nullptr;
    {
      std::lock_guard<std::mutex> l(q->m);
      if (q->disposed) return;
      if (q->running && !q->queued.empty()) { b = q->queued.front(); q->queued.pop_front(); }
    }
    if (b) { gCompleted++; q->cb(q->ud, q, b); }
  }
}

OSStatus AudioQueueNewOutput(const AudioStreamBasicDescription*, AudioQueueOutputCallback cb, void* ud, CFRunLoopRef, CFStringRef, UInt32,
                             AudioQueueRef* out) {
  OpaqueAudioQueue* q = new OpaqueAudioQueue;
  q->cb = cb;
  q->ud = ud;
  q->worker = std::thread(workerLoop, q);
  { std::lock_guard<std::mutex> l(gMock); gLive = q; }
  gQueues++;
  *out = q;
  return 0;
}
OSStatus AudioQueueAllocateBuffer(AudioQueueRef q, UInt32 bytes, AudioQueueBufferRef* out) {
  AudioQueueBuffer* b = new AudioQueueBuffer;
  b->mAudioDataBytesCapacity = bytes;
  b->mAudioData = calloc(1, bytes);
  b->mAudioDataByteSize = 0;
  std::lock_guard<std::mutex> l(q->m);
  q->buffers.push_back(b);
  *out = b;
  return 0;
}
OSStatus AudioQueueEnqueueBuffer(AudioQueueRef q, AudioQueueBufferRef b, UInt32, const void*) {
  std::lock_guard<std::mutex> l(q->m);
  if (q->resetting) return kAudioQueueErr_EnqueueDuringReset;
  q->queued.push_back(b);
  return 0;
}
OSStatus AudioQueueStart(AudioQueueRef q, const AudioTimeStamp*) {
  gStartCalls++;
  if (gStartFails) { gStartFailures++; return kAudioQueueErr_CannotStart; }
  std::lock_guard<std::mutex> l(q->m);
  q->running = true;
  return 0;
}
OSStatus AudioQueuePause(AudioQueueRef q) { std::lock_guard<std::mutex> l(q->m); q->running = false; return 0; }
// Stop/Reset hand the queued buffers back through the callback, synchronously, with enqueueing refused meanwhile.
static void drain(OpaqueAudioQueue* q) {
  std::deque<AudioQueueBufferRef> pending;
  {
    std::lock_guard<std::mutex> l(q->m);
    q->resetting = true;
    pending.swap(q->queued);
  }
  for (AudioQueueBufferRef b : pending) q->cb(q->ud, q, b);
  std::lock_guard<std::mutex> l(q->m);
  q->resetting = false;
}
OSStatus AudioQueueStop(AudioQueueRef q, Boolean) {
  { std::lock_guard<std::mutex> l(q->m); q->running = false; }
  drain(q);
  return 0;
}
OSStatus AudioQueueReset(AudioQueueRef q) { drain(q); return 0; }
OSStatus AudioQueueDispose(AudioQueueRef q, Boolean) {
  { std::lock_guard<std::mutex> l(q->m); q->running = false; q->disposed = true; }
  q->worker.join();
  for (AudioQueueBufferRef b : q->buffers) { free(b->mAudioData); delete b; }
  { std::lock_guard<std::mutex> l(gMock); if (gLive == q) gLive = nullptr; }
  delete q;
  return 0;
}
OSStatus AudioQueueSetParameter(AudioQueueRef, AudioQueueParameterID, AudioQueueParameterValue) { return 0; }

// ---------------------------------------------------------------- AudioSession
OSStatus AudioSessionInitialize(CFRunLoopRef, CFStringRef, AudioSessionInterruptionListener l, void* ud) {
  std::lock_guard<std::mutex> g(gMock);
  gIntListener = l; gIntUd = ud;
  return 0;
}
OSStatus AudioSessionSetProperty(AudioSessionPropertyID, UInt32, const void*) { return 0; }
OSStatus AudioSessionGetProperty(AudioSessionPropertyID id, UInt32* size, void* out) {
  if (id == kAudioSessionProperty_AudioRoute && size && *size == sizeof(CFStringRef)) {
    std::lock_guard<std::mutex> g(gMock);
    *(CFStringRef*)out = new __CFString{ strdup(gRoute.c_str()) };  // leaks a little per call: test only
    return 0;
  }
  return -1;
}
OSStatus AudioSessionAddPropertyListener(AudioSessionPropertyID id, AudioSessionPropertyListener l, void* ud) {
  std::lock_guard<std::mutex> g(gMock);
  if (id == kAudioSessionProperty_AudioRouteChange) { gRouteListener = l; gRouteUd = ud; }
  return 0;
}
OSStatus AudioSessionSetActive(Boolean) { return 0; }

namespace mock {
bool queueRunning() { std::lock_guard<std::mutex> g(gMock); if (!gLive) return false; std::lock_guard<std::mutex> l(gLive->m); return gLive->running; }
int startCalls() { return gStartCalls; }
int startFailures() { return gStartFailures; }
int queuesCreated() { return gQueues; }
void setStartFails(bool f) { gStartFails = f; }
void setRoute(const char* r) { std::lock_guard<std::mutex> g(gMock); gRoute = r; }
void fireInterruption(UInt32 state) {
  AudioSessionInterruptionListener l; void* ud;
  { std::lock_guard<std::mutex> g(gMock); l = gIntListener; ud = gIntUd; }
  if (l) l(ud, state);
}
void fireRouteChange() {
  AudioSessionPropertyListener l; void* ud;
  { std::lock_guard<std::mutex> g(gMock); l = gRouteListener; ud = gRouteUd; }
  if (l) l(ud, kAudioSessionProperty_AudioRouteChange, 0, nullptr);
}
unsigned long completedBuffers() { return gCompleted; }
}  // namespace mock
