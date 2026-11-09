// Host test of AudioQueueSink's AudioSession availability handling (B1.8). At cold boot the daemon
// starts before the audio session server: AudioSessionInitialize fails ('ini?') and the AudioQueue
// server then refuses AudioQueueStart with the same code; seconds later it works. The sink must (1) in
// --audio-session off make ZERO AudioSession-family calls while it plays, flushes, crossfades,
// changes volume, recovers a dead queue and self-heals a failing AudioQueueStart; (2) in auto
// (default) retry AudioSessionInitialize forever, play through a bare queue meanwhile (no other
// AudioSession call), and UPGRADE IN PLACE when a retry succeeds (listeners wired, output on the
// real route, ring kept); (3) in on, wait for the session instead of playing bare. The mock counts
// every AudioSession* entry point (mock::sessionCalls()) and the Initialize calls (sessionInitCalls()).
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include <vector>

#include "AudioQueueSink.h"
#include "mock_audiotoolbox.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)
static uint64_t nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }

static void driveMs(AudioQueueSink& s, long ms, bool feed = true) {
  static std::vector<uint8_t> chunk(882 * 4, 0x11);
  uint64_t end = nowMs() + ms;
  while (nowMs() < end) {
    if (feed && s.bufferedSeconds() < 0.5) s.feedPCMFrames(chunk.data(), chunk.size());
    s.tick();
    usleep(20000);
  }
}
static long untilRunning(AudioQueueSink& s, long maxMs, bool feed = true) {
  uint64_t t0 = nowMs();
  while (nowMs() - t0 < (uint64_t)maxMs) {
    if (mock::queueRunning()) return (long)(nowMs() - t0);
    driveMs(s, 20, feed);
  }
  return -1;
}
// Drive until the session is live or the deadline passes; returns the time it took (-1 = never).
static long untilSession(AudioQueueSink& s, long maxMs) {
  uint64_t t0 = nowMs();
  while (nowMs() - t0 < (uint64_t)maxMs) {
    if (s.sessionInUse()) return (long)(nowMs() - t0);
    driveMs(s, 20);
  }
  return -1;
}

int main() {
  char m[300];

  // ---------------------------------------------------------------- 1. --audio-session off: zero AudioSession calls
  {
    AudioQueueSink::Options o;
    o.session = AudioQueueSink::SESSION_OFF;
    o.route = AudioQueueSink::ROUTE_SPEAKER;  // must be a no-op: no OverrideAudioRoute call
    AudioQueueSink sink(o);
    snprintf(m, sizeof m, "off: constructed with %d AudioSession calls (expect 0), sessionInUse=%d, route '%s'", mock::sessionCalls(),
             (int)sink.sessionInUse(), sink.currentRoute().c_str());
    CHECK(mock::sessionCalls() == 0 && !sink.sessionInUse() && sink.currentRoute() == "default", m);

    long t = untilRunning(sink, 500);
    snprintf(m, sizeof m, "off: first feed starts the bare queue (%ld ms, %d AudioQueueStart)", t, mock::startCalls());
    CHECK(t >= 0 && mock::startCalls() == 1, m);
    driveMs(sink, 300);
    CHECK(mock::completedBuffers() > 5, "off: the bare queue completes buffers");

    sink.setGain(0.3f);
    sink.volumeChanged(30000);
    sink.flush();
    driveMs(sink, 200);
    sink.crossfade(441);
    driveMs(sink, 200);
    unsigned long gen = sink.queueGeneration();
    driveMs(sink, 1000);
    snprintf(m, sizeof m, "off: volume/flush/crossfade + 1 s of playback: running=%d, generation %lu (unchanged), %d AudioSession calls",
             (int)mock::queueRunning(), sink.queueGeneration(), mock::sessionCalls());
    CHECK(mock::queueRunning() && sink.queueGeneration() == gen && mock::sessionCalls() == 0, m);

    mock::fireInterruption(kAudioSessionBeginInterruption);  // nothing registered: no effect
    mock::fireRouteChange();
    driveMs(sink, 100);
    CHECK(mock::queueRunning() && sink.queueGeneration() == gen, "off: interruption/route-change events are not wired: still playing, no recreate");

    mock::setCompleting(false);
    uint64_t t0 = nowMs();
    while (nowMs() - t0 < 3000 && sink.queueGeneration() == gen) driveMs(sink, 20);
    long recovered = (long)(nowMs() - t0);
    mock::setCompleting(true);
    unsigned long c0 = mock::completedBuffers();
    driveMs(sink, 400);
    snprintf(m, sizeof m, "off: dead queue re-acquired after %ld ms (expect ~1000..1500), generation %lu -> %lu, running=%d, %lu completions after, %d AudioSession calls",
             recovered, gen, sink.queueGeneration(), (int)mock::queueRunning(), mock::completedBuffers() - c0, mock::sessionCalls());
    CHECK(sink.queueGeneration() == gen + 1 && recovered >= 900 && recovered <= 1800 && mock::queueRunning() && mock::completedBuffers() - c0 >= 8 &&
              mock::sessionCalls() == 0, m);

    // 2. start self-heal: AudioQueueStart fails: retries at 250, 500 (recreate from here), 1000, 2000 ms
    sink.stop();
    CHECK(!mock::queueRunning(), "off: stop() stops the queue");
    int startsBefore = mock::startCalls(), queuesBefore = mock::queuesCreated();
    mock::setStartFails(true);
    driveMs(sink, 2200);  // attempts at ~0, 250, 750, 1750
    int attempts = mock::startCalls() - startsBefore, recreated = mock::queuesCreated() - queuesBefore;
    snprintf(m, sizeof m, "off: AudioQueueStart failing for 2.2 s: %d attempts (expect 4: 0/250/750/1750 ms), queue recreated %d times (expect 2), not running",
             attempts, recreated);
    CHECK(!mock::queueRunning() && attempts >= 3 && attempts <= 5 && recreated >= 1 && recreated <= 3, m);
    mock::setStartFails(false);
    t = untilRunning(sink, 2500);
    snprintf(m, sizeof m, "off: AudioQueueStart works again: playing after %ld ms (next retry was due within 2000 ms)", t);
    CHECK(t >= 0 && t <= 2100, m);
    driveMs(sink, 300);
    snprintf(m, sizeof m, "off: %d AudioSession calls after the whole bare run (expect 0)", mock::sessionCalls());
    CHECK(mock::sessionCalls() == 0 && mock::queueRunning(), m);
    sink.stop();
  }

  // ---------------------------------------------------------------- 3. auto: Initialize fails for good -> bare, retried with backoff
  {
    mock::setSessionInitFails(true);
    int before = mock::sessionCalls(), initBefore = mock::sessionInitCalls();
    AudioQueueSink::Options o;  // session = SESSION_AUTO
    uint64_t t0 = nowMs();
    AudioQueueSink sink(o);
    long ctor = (long)(nowMs() - t0);
    snprintf(m, sizeof m, "auto + init fails: constructor back after %ld ms (first attempt failed at once), %d AudioSession call (the Initialize), bare (sessionInUse=%d)",
             ctor, mock::sessionCalls() - before, (int)sink.sessionInUse());
    CHECK(ctor < 500 && mock::sessionCalls() - before == 1 && !sink.sessionInUse(), m);
    long t = untilRunning(sink, 500);
    driveMs(sink, 3300);  // retries at ~1 s and ~3 s
    int inits = mock::sessionInitCalls() - initBefore, calls = mock::sessionCalls() - before;
    snprintf(m, sizeof m, "auto + init fails: plays bare (started after %ld ms); after 3.3 s %d Initialize attempts (expect 3: 0/1/3 s, backoff), %d AudioSession calls in all (expect = attempts: nothing else), attempts()=%d",
             t, inits, calls, sink.sessionAttempts());
    CHECK(t >= 0 && inits == 3 && calls == inits && sink.sessionAttempts() == 3 && mock::queueRunning(), m);
    // a failing-start episode while bare must not call AudioSessionSetActive either
    mock::setStartFails(true);
    mock::setCompleting(false);
    driveMs(sink, 1600);
    mock::setStartFails(false);
    mock::setCompleting(true);
    long t2 = untilRunning(sink, 2500);
    snprintf(m, sizeof m, "auto + init fails: survives a failing-start episode bare (back after %ld ms), AudioSession calls still = Initialize attempts (%d = %d)",
             t2, mock::sessionCalls() - before, mock::sessionInitCalls() - initBefore);
    CHECK(t2 >= 0 && mock::sessionCalls() - before == mock::sessionInitCalls() - initBefore, m);
    sink.stop();
    mock::setSessionInitFails(false);
  }

  // ---------------------------------------------------------------- 4. auto: Initialize fails twice, then succeeds -> upgrade in place
  {
    mock::setSessionInitFailures(2);
    int before = mock::sessionCalls();
    AudioQueueSink::Options o;
    AudioQueueSink sink(o);
    long t = untilRunning(sink, 500);
    CHECK(t >= 0 && !sink.sessionInUse(), "auto + init fails twice: plays bare at once");
    driveMs(sink, 300);
    unsigned long gen = sink.queueGeneration();
    sink.takeResumed();
    uint64_t t0 = nowMs();
    long ts = untilSession(sink, 5000);  // third attempt at ~3 s (0, 1 s, 3 s)
    // let the owner perform the upgrade on its next ticks
    uint64_t tu = nowMs();
    while (nowMs() - tu < 1000 && sink.queueGeneration() == gen) driveMs(sink, 20);
    long upgradeMs = (long)(nowMs() - t0);
    double buffered = sink.bufferedSeconds();
    driveMs(sink, 100);
    snprintf(m, sizeof m, "auto + init fails twice: session live after %ld ms (expect ~3000), output upgraded in place %ld ms after start (generation %lu -> %lu), running=%d, %.2f s still buffered (ring kept), attempts=%d",
             ts, upgradeMs, gen, sink.queueGeneration(), (int)mock::queueRunning(), buffered, sink.sessionAttempts());
    CHECK(ts >= 2500 && ts <= 4000 && sink.queueGeneration() == gen + 1 && mock::queueRunning() && buffered > 0.0 && sink.sessionAttempts() == 3, m);  // the mock drains 4.6x real time, so the level swings; "kept" = not cleared (see the resume check)
    CHECK(!sink.takeResumed(), "auto + init fails twice: the upgrade did not drop the stream (no resume signalled)");
    unsigned long c0 = mock::completedBuffers();
    driveMs(sink, 400);
    snprintf(m, sizeof m, "auto + init fails twice: full path after the upgrade: %lu completions in 400 ms, route '%s', %d AudioSession calls (init x3 + category/listener/override/SetActive/route...)",
             mock::completedBuffers() - c0, sink.currentRoute().c_str(), mock::sessionCalls() - before);
    CHECK(mock::completedBuffers() - c0 >= 10 && sink.currentRoute() != "default" && mock::sessionCalls() - before >= 7, m);
    // the listeners are wired now: an interruption pauses, END resumes
    mock::fireInterruption(kAudioSessionBeginInterruption);
    driveMs(sink, 50, false);
    bool paused = !mock::queueRunning();
    mock::fireInterruption(kAudioSessionEndInterruption);
    long tr = untilRunning(sink, 500);
    snprintf(m, sizeof m, "auto + init fails twice: interruption listener wired after the upgrade (paused=%d), END resumes after %ld ms", (int)paused, tr);
    CHECK(paused && tr >= 0, m);
    sink.stop();
  }

  // ---------------------------------------------------------------- 5. auto: the first Initialize hangs 2.6 s then succeeds -> bare at 2 s, then upgrade
  {
    mock::setSessionInitDelayMs(2600);
    int before = mock::sessionCalls();
    uint64_t t0 = nowMs();
    AudioQueueSink::Options o;
    AudioQueueSink sink(o);
    long ctor = (long)(nowMs() - t0);
    snprintf(m, sizeof m, "auto + init hangs: constructor returned after %ld ms (the 2 s wait), bare (sessionInUse=%d)", ctor, (int)sink.sessionInUse());
    CHECK(ctor >= 1900 && ctor <= 2400 && !sink.sessionInUse(), m);
    mock::setSessionInitDelayMs(0);
    long t = untilRunning(sink, 500);
    unsigned long gen = sink.queueGeneration();
    long ts = untilSession(sink, 2000);  // lands at ~2.6 s after construction
    uint64_t tu = nowMs();
    while (nowMs() - tu < 1000 && sink.queueGeneration() == gen) driveMs(sink, 20);
    snprintf(m, sizeof m, "auto + init hangs then succeeds: played bare (%ld ms), session live %ld ms after the constructor, upgraded (generation %lu -> %lu), running=%d, %d AudioSession calls",
             t, ts, gen, sink.queueGeneration(), (int)mock::queueRunning(), mock::sessionCalls() - before);
    CHECK(t >= 0 && ts >= 0 && sink.queueGeneration() == gen + 1 && mock::queueRunning() && sink.sessionInUse(), m);
    sink.stop();
  }

  // ---------------------------------------------------------------- 6. on: Initialize fails twice -> no bare playback, then plays once the session is up
  {
    mock::setSessionInitFailures(2);
    int startsBefore = mock::startCalls();
    AudioQueueSink::Options o;
    o.session = AudioQueueSink::SESSION_ON;
    AudioQueueSink sink(o);
    driveMs(sink, 1500);  // feeding, ticking: must not start
    snprintf(m, sizeof m, "on + init fails twice: 1.5 s of feeds: no AudioQueueStart (%d), not running, sessionInUse=%d", mock::startCalls() - startsBefore, (int)sink.sessionInUse());
    CHECK(mock::startCalls() - startsBefore == 0 && !mock::queueRunning() && !sink.sessionInUse(), m);
    long ts = untilSession(sink, 4000);
    long t = untilRunning(sink, 1500);
    snprintf(m, sizeof m, "on + init fails twice: session live after a further %ld ms, then playing after %ld ms (attempts=%d)", ts, t, sink.sessionAttempts());
    CHECK(ts >= 0 && t >= 0 && sink.sessionAttempts() == 3, m);
    sink.stop();
  }

  // ---------------------------------------------------------------- 7. on / auto with the session available at once: the b17 path
  {
    int before = mock::sessionCalls();
    AudioQueueSink::Options o;
    o.session = AudioQueueSink::SESSION_ON;
    AudioQueueSink sink(o);
    long t = untilRunning(sink, 500);
    snprintf(m, sizeof m, "on + session ok: in use from the start (sessionInUse=%d, %d setup calls, 1 attempt), plays (%ld ms)", (int)sink.sessionInUse(),
             mock::sessionCalls() - before, t);
    CHECK(sink.sessionInUse() && mock::sessionCalls() - before >= 5 && t >= 0 && sink.sessionAttempts() == 1, m);
    sink.stop();
  }

  printf(fails ? "\n%d FAILURES\n" : "\nall AudioSession availability checks passed\n", fails);
  return fails ? 1 : 0;
}
