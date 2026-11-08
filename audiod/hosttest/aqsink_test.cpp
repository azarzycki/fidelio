// Host test of AudioQueueSink's recovery invariant (B1.1 FIX 1), against the mock AudioToolbox in
// hosttest/mock. Replays the hardware failure: interruption began, route changes, NO interruption
// end -> the queue must nevertheless be running again within ~1 s whenever playback is wanted;
// and must stay stopped when it is not (idle).
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <vector>

#include "AudioQueueSink.h"
#include "mock_audiotoolbox.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)
static uint64_t nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }

// Behave like audiod: feed 20 ms of audio while the sink has room, tick every 20 ms.
static void driveMs(AudioQueueSink& s, long ms, bool feed = true) {
  static std::vector<uint8_t> chunk(882 * 4, 0x11);
  uint64_t end = nowMs() + ms;
  while (nowMs() < end) {
    if (feed && s.bufferedSeconds() < 0.5) s.feedPCMFrames(chunk.data(), chunk.size());
    s.tick();
    usleep(20000);
  }
}
// Drive until the queue runs or the deadline passes; returns the time it took (-1 = never).
static long untilRunning(AudioQueueSink& s, long maxMs, bool feed = true) {
  uint64_t t0 = nowMs();
  while (nowMs() - t0 < (uint64_t)maxMs) {
    if (mock::queueRunning()) return (long)(nowMs() - t0);
    driveMs(s, 20, feed);
  }
  return -1;
}

int main() {
  AudioQueueSink sink;
  char m[200];

  // 1. first feed starts the queue
  long t = untilRunning(sink, 500);
  snprintf(m, sizeof m, "first feed starts the queue (%ld ms, %d AudioQueueStart)", t, mock::startCalls());
  CHECK(t >= 0 && mock::startCalls() == 1, m);
  driveMs(sink, 200);
  CHECK(mock::completedBuffers() > 5, "the fake queue is completing buffers");

  // 2. THE hardware sequence: interruption begins, no end ever comes, a route change arrives
  mock::fireInterruption(kAudioSessionBeginInterruption);
  CHECK(!mock::queueRunning(), "interruption begin pauses the queue");
  mock::setRoute("MicrophoneBuiltIn");
  mock::fireRouteChange();
  mock::setRoute("USB");
  mock::fireRouteChange();  // on the device this recreated the queue but never started it
  t = untilRunning(sink, 1500);
  snprintf(m, sizeof m, "begin + route changes + no end: playing again after %ld ms (queues created %d)", t, mock::queuesCreated());
  CHECK(t >= 0 && t < 300, m);

  // 3. interruption begins and NOTHING else happens: the watchdog must restart within ~1 s
  mock::fireInterruption(kAudioSessionBeginInterruption);
  CHECK(!mock::queueRunning(), "second interruption pauses the queue");
  t = untilRunning(sink, 2500);
  snprintf(m, sizeof m, "begin with no end and no route change: playing again after %ld ms (grace 1000)", t);
  CHECK(t >= 900 && t <= 1500, m);

  // 4. a genuine interruption: AudioQueueStart keeps failing; the sink keeps retrying, then succeeds
  int before = mock::startCalls();
  mock::setStartFails(true);
  mock::fireInterruption(kAudioSessionBeginInterruption);
  driveMs(sink, 2200);
  int attempts = mock::startCalls() - before;
  snprintf(m, sizeof m, "while AudioQueueStart fails: not running, %d retries in 2.2 s (expect 2..5)", attempts);
  CHECK(!mock::queueRunning() && attempts >= 2 && attempts <= 5, m);
  mock::setStartFails(false);
  t = untilRunning(sink, 1200);
  snprintf(m, sizeof m, "start succeeds again: playing after %ld ms", t);
  CHECK(t >= 0 && t <= 700, m);

  // 5. the normal path still works: begin then end resumes immediately
  mock::fireInterruption(kAudioSessionBeginInterruption);
  mock::fireInterruption(kAudioSessionEndInterruption);
  t = untilRunning(sink, 500);
  snprintf(m, sizeof m, "begin + end: resumed after %ld ms", t);
  CHECK(t >= 0 && t < 100, m);

  // 6. idle: nobody wants audio, so nothing may restart the queue
  sink.stop();
  CHECK(!mock::queueRunning(), "stop() stops the queue");
  int calls = mock::startCalls();
  mock::fireInterruption(kAudioSessionBeginInterruption);
  mock::fireRouteChange();
  driveMs(sink, 1500, /*feed=*/false);
  snprintf(m, sizeof m, "idle + interruption + route change + 1.5 s of ticks: still stopped, %d new starts", mock::startCalls() - calls);
  CHECK(!mock::queueRunning() && mock::startCalls() == calls, m);

  // 7. feeding again restarts
  t = untilRunning(sink, 500);
  snprintf(m, sizeof m, "next feed restarts the queue (%ld ms)", t);
  CHECK(t >= 0, m);

  printf(fails ? "\n%d FAILURES\n" : "\nall AudioQueueSink recovery checks passed\n", fails);
  return fails ? 1 : 0;
}
