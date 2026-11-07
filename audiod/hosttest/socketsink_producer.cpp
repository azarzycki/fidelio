// Host test producer: streams a constant-sample PCM pattern through SocketSink, pushing 20 ms
// chunks as fast as the sink accepts them, like cspot and fidelio-raop do (SocketSink paces).
//   socketsink_producer NAME SOCK SAMPLE_VALUE DURATION_MS [FLUSH_AT_MS]
// Exercises connect-with-backoff, HELLO, AUDIO pacing and reconnect: if audiod dies and comes
// back, the stream resumes (dropped frames are counted and printed at the end).
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
  for (uint64_t i = 0; i * 20 < duration; i++) {
    sink.write(chunk.data(), 882);  // blocks once the stream is SocketSink::kLeadMs ahead
    if (flushAt && !flushed && i * 20 >= flushAt) { sink.flush(); flushed = true; }
  }
  sink.idle();  // waits out the lead, then STOP
  printf("producer %s done in %llu ms: connections=%lu dropped_frames=%lu\n", name.c_str(), (unsigned long long)(nowMs() - t0),
         sink.reconnects(), sink.dropped());
  return 0;
}
