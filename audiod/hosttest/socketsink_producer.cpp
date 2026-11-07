// Host test producer: streams a constant-sample PCM pattern through SocketSink in real time.
//   socketsink_producer NAME SOCK SAMPLE_VALUE DURATION_MS [FLUSH_AT_MS]
// Exercises connect-with-backoff, HELLO, AUDIO pacing (20 ms chunks) and reconnect: if audiod
// dies and comes back, the stream resumes (dropped frames are counted and printed at the end).
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "SocketSink.h"

static uint64_t nowMs() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: %s NAME SOCK SAMPLE DURATION_MS [FLUSH_AT_MS]\n", argv[0]); return 1; }
  std::string name = argv[1], sock = argv[2];
  int16_t sample = (int16_t)atoi(argv[3]);
  uint64_t duration = strtoull(argv[4], nullptr, 10), flushAt = argc > 5 ? strtoull(argv[5], nullptr, 10) : 0;
  SocketSink sink(name, sock);
  std::vector<int16_t> chunk(882 * 2, sample);  // 20 ms
  uint64_t t0 = nowMs();
  bool flushed = false;
  for (uint64_t i = 0;; i++) {
    uint64_t at = t0 + i * 20;
    uint64_t now = nowMs();
    if (at > now) usleep((useconds_t)((at - now) * 1000));
    if (nowMs() - t0 >= duration) break;
    sink.write(chunk.data(), 882);
    if (flushAt && !flushed && nowMs() - t0 >= flushAt) { sink.flush(); flushed = true; }
  }
  sink.stop();
  printf("producer %s done: connections=%lu dropped_frames=%lu\n", name.c_str(), sink.reconnects(), sink.dropped());
  return 0;
}
