// Host test of AudioQueueSink's recovery invariant (B1.1 FIX 1), against the mock AudioToolbox in
// hosttest/mock. Replays the hardware failure: interruption began, route changes, NO interruption
// end -> the queue must nevertheless be running again within ~1 s whenever playback is wanted;
// and must stay stopped when it is not (idle).
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <thread>
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
// The callbacks only set flags (B1.2); the owner thread acts on them on its next tick (<= 20 ms).
static void settle(AudioQueueSink& s) { driveMs(s, 50, false); }
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
  settle(sink);
  CHECK(!mock::queueRunning(), "interruption begin pauses the queue (on the owner thread's next tick)");
  mock::setRoute("MicrophoneBuiltIn");
  mock::fireRouteChange();
  mock::setRoute("USB");
  mock::fireRouteChange();  // on the device this recreated the queue but never started it
  t = untilRunning(sink, 1500);
  snprintf(m, sizeof m, "begin + route changes + no end: playing again after %ld ms (queues created %d)", t, mock::queuesCreated());
  CHECK(t >= 0 && t < 300, m);

  // 3. interruption begins and NOTHING else happens: the watchdog must restart within ~1 s
  mock::fireInterruption(kAudioSessionBeginInterruption);
  settle(sink);
  CHECK(!mock::queueRunning(), "second interruption pauses the queue");
  t = untilRunning(sink, 2500);
  snprintf(m, sizeof m, "begin with no end and no route change: playing again after %ld ms (grace 1000, minus the 50 ms settle)", t);
  CHECK(t >= 850 && t <= 1500, m);

  // 4. a genuine interruption: AudioQueueStart keeps failing; the sink keeps retrying, then succeeds
  int before = mock::startCalls();
  mock::setStartFails(true);
  mock::fireInterruption(kAudioSessionBeginInterruption);
  settle(sink);
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
  settle(sink);
  CHECK(!mock::queueRunning(), "third interruption pauses the queue");
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

  // 8. (B1.2 #1) RACE: route changes and interruptions fired from another thread (the AudioSession
  //    thread on the device) while the owner thread feeds/ticks, with Stop/Dispose made SLOW (60 ms
  //    > one 20 ms tick). The B1.1 code interleaved destroy/create across the two threads and left a
  //    running queue with no buffers; the sink must end up running AND completing buffers.
  mock::setStopDelayMs(60);
  {
    std::atomic<bool> go{true};
    std::thread session([&] {
      for (int i = 0; i < 14 && go; i++) {
        usleep(150000);
        if (i % 4 == 1) mock::fireInterruption(kAudioSessionBeginInterruption);
        mock::fireRouteChange();
        if (i % 4 == 3) mock::fireInterruption(kAudioSessionEndInterruption);
      }
    });
    driveMs(sink, 2300);
    go = false;
    session.join();
  }
  driveMs(sink, 300);  // let the dust settle
  unsigned long c0 = mock::completedBuffers();
  driveMs(sink, 400);
  unsigned long c1 = mock::completedBuffers();
  snprintf(m, sizeof m, "after 2.3 s of racing route changes/interruptions with slow Stop/Dispose: running=%d, live queues=%d, completions in 400 ms=%lu",
           (int)mock::queueRunning(), mock::liveQueues(), c1 - c0);
  CHECK(mock::queueRunning() && mock::liveQueues() == 1 && c1 - c0 >= 10, m);
  mock::setStopDelayMs(0);

  // 9. (B1.2 #2) running but DEAD: the queue stops completing buffers (mediaserverd wedge) while the
  //    flag still says running. The completion watchdog must re-acquire within ~1 s + retry gate.
  unsigned long gen = sink.queueGeneration();
  mock::setCompleting(false);
  uint64_t t0 = nowMs();
  long recovered = -1;
  while (nowMs() - t0 < 3000) {
    driveMs(sink, 20);
    if (sink.queueGeneration() > gen) { recovered = (long)(nowMs() - t0); break; }
  }
  snprintf(m, sizeof m, "running but no completions: re-acquired after %ld ms (expect ~1000..1600)", recovered);
  CHECK(recovered >= 900 && recovered <= 1700, m);
  mock::setCompleting(true);
  driveMs(sink, 300);
  c0 = mock::completedBuffers();
  driveMs(sink, 400);
  c1 = mock::completedBuffers();
  snprintf(m, sizeof m, "completions resumed after the re-acquire: running=%d, %lu in 400 ms", (int)mock::queueRunning(), c1 - c0);
  CHECK(mock::queueRunning() && c1 - c0 >= 10, m);

  // 10. (B1.2 #8) a late interruption END while already playing must not recreate the queue
  gen = sink.queueGeneration();
  mock::fireInterruption(kAudioSessionEndInterruption);
  driveMs(sink, 200);
  snprintf(m, sizeof m, "late interruption end while playing: generation %lu -> %lu (no recreate), running=%d", gen, sink.queueGeneration(), (int)mock::queueRunning());
  CHECK(sink.queueGeneration() == gen && mock::queueRunning(), m);

  // 11. (B1.3 #1a) FLUSH storm with the dropped buffers delivered asynchronously on the queue thread
  //     (device behaviour): the owner's re-prime and the late callbacks race for the same buffers.
  //     No buffer may end up stranded; the queue must keep completing.
  mock::setAsyncResetDelivery(true);
  for (int i = 0; i < 40; i++) { sink.flush(); driveMs(sink, 30); }
  driveMs(sink, 300);
  c0 = mock::completedBuffers();
  driveMs(sink, 400);
  c1 = mock::completedBuffers();
  snprintf(m, sizeof m, "40 flushes with async dropped-buffer delivery: running=%d, queue owns %d buffers, %lu completions in 400 ms",
           (int)mock::queueRunning(), mock::queuedBuffers(), c1 - c0);
  CHECK(mock::queueRunning() && mock::queuedBuffers() >= 1 && c1 - c0 >= 10, m);
  mock::setAsyncResetDelivery(false);

  // 12. (B1.3 #1b) every buffer stranded: the queue refuses the next enqueues, hands everything back,
  //     then owns nothing while "running". The per-tick re-prime must put them back within a tick or two.
  mock::refuseNextEnqueues(6);
  sink.flush();  // Reset -> 3 callbacks re-enqueue (refused) -> prime (refused): 3 stranded buffers
  driveMs(sink, 20, false);
  int owned = mock::queuedBuffers();
  driveMs(sink, 100);
  c0 = mock::completedBuffers();
  driveMs(sink, 400);
  c1 = mock::completedBuffers();
  snprintf(m, sizeof m, "all buffers stranded by refused enqueues (%d owned right after): re-primed by tick, running=%d, %lu completions in 400 ms",
           owned, (int)mock::queueRunning(), c1 - c0);
  CHECK(mock::queueRunning() && c1 - c0 >= 10, m);

  // 13. (B1.3 #4) AudioQueueStart returns 0 but the queue never renders (a long call, a wedged
  //     mediaserverd right after a restart): the watchdog must recover, with the 3 s first-completion
  //     window and backing off (no tight recreate loop).
  gen = sink.queueGeneration();
  mock::setCompleting(false);
  sink.flush();  // reset: completions stop, the queue stays "running"
  t0 = nowMs();
  recovered = -1;
  while (nowMs() - t0 < 4000) {
    driveMs(sink, 20);
    if (sink.queueGeneration() > gen) { recovered = (long)(nowMs() - t0); break; }
  }
  snprintf(m, sizeof m, "running but never completing: first re-acquire after %ld ms (expect ~1000..1700: the previous start had completed)", recovered);
  CHECK(recovered >= 900 && recovered <= 1800, m);
  unsigned long g1 = sink.queueGeneration();
  driveMs(sink, 9000);  // still not completing: each new queue starts OK but never renders
  unsigned long g2 = sink.queueGeneration();
  snprintf(m, sizeof m, "start OK but no render for 9 more s: %lu further re-acquires (3 s first window + 1,2,4 s backoff: expect 2..3, not a tight loop)", g2 - g1);
  CHECK(g2 - g1 >= 1 && g2 - g1 <= 3, m);
  mock::setCompleting(true);
  t = untilRunning(sink, 500);
  driveMs(sink, 300);
  c0 = mock::completedBuffers();
  driveMs(sink, 400);
  c1 = mock::completedBuffers();
  snprintf(m, sizeof m, "rendering resumes: running=%d, %lu completions in 400 ms", (int)mock::queueRunning(), c1 - c0);
  CHECK(mock::queueRunning() && c1 - c0 >= 10, m);

  // 14. (B1.4 A) yield: the interruption never ends but ANOTHER APP IS PLAYING (the user pressed play
  //     in the Music app). The watchdog must not clear/restart while that is true; once the other
  //     audio stops it resumes after the grace window, and the stale ring is dropped on resume.
  mock::setOtherAudioPlaying(true);
  int startsBefore = mock::startCalls();
  sink.takeResumed();  // consume what the earlier resumes left
  mock::fireInterruption(kAudioSessionBeginInterruption);
  settle(sink);
  CHECK(!mock::queueRunning(), "interruption with other audio playing pauses the queue");
  driveMs(sink, 2500);
  snprintf(m, sizeof m, "yields while other audio plays: after 2.5 s still paused, %d new AudioQueueStart (expect 0), %.2f s buffered",
           mock::startCalls() - startsBefore, sink.bufferedSeconds());
  CHECK(!mock::queueRunning() && mock::startCalls() == startsBefore, m);
  bool resumedSeen = sink.takeResumed();
  CHECK(!resumedSeen, "no resume signalled while yielding");
  mock::setOtherAudioPlaying(false);
  t = untilRunning(sink, 2500);
  double buffered = sink.bufferedSeconds();
  snprintf(m, sizeof m, "other audio stopped: resumed after %ld ms (grace ~1000), %.2f s buffered right after (stale lead dropped: expect < 0.3)", t, buffered);
  CHECK(t >= 800 && t <= 1700 && buffered < 0.3, m);
  CHECK(sink.takeResumed() && !sink.takeResumed(), "resume signalled exactly once to the caller");

  // 15. (B1.4 B) soak: normal playback must never trigger a spurious no-completion re-acquire
  //     (a completion stamped a few ms after the tick's clock read used to underflow to 2^64 ms).
  gen = sink.queueGeneration();
  driveMs(sink, 3000);
  snprintf(m, sizeof m, "3 s of normal playback: generation %lu -> %lu (no spurious re-acquire), running=%d", gen, sink.queueGeneration(), (int)mock::queueRunning());
  CHECK(sink.queueGeneration() == gen && mock::queueRunning(), m);

  printf(fails ? "\n%d FAILURES\n" : "\nall AudioQueueSink recovery checks passed\n", fails);
  return fails ? 1 : 0;
}
