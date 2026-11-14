// B1.10: writeFrame must ride out a backpressure stall (the reader, audiod, deliberately stops
// reading the active source, B1.5) instead of returning failure after one SO_SNDTIMEO; the frame
// must stay byte-exact across the stall, and writeFrame must still give up (so a truly dead audiod is
// detected) once the stall exceeds its budget.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include "AudiodFraming.h"
using namespace audiod;

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

int main() {
  // A small send buffer + a 500 ms send timeout, exactly like SocketSink to audiod.
  int sp[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 1; }
  int snd = 8 * 1024;
  setsockopt(sp[0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof snd);
  int rcv = 8 * 1024;
  setsockopt(sp[1], SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
  struct timeval to = { 0, 500 * 1000 };
  setsockopt(sp[0], SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);

  // 1. The reader pauses 1500 ms (longer than the 500 ms send timeout) then drains everything.
  //    The writer sends frames the whole time with the production budget; all must arrive intact.
  const int N = 400;                   // ~400 * 1408 B = 563 KB, far more than the 8 KB buffer
  std::vector<uint8_t> pcm(1408);
  for (size_t i = 0; i < pcm.size(); i++) pcm[i] = (uint8_t)(i * 7 + 3);
  std::atomic<bool> allSent{false};
  std::atomic<int> sentOk{0};
  std::thread writer([&] {
    for (int i = 0; i < N; i++) {
      if (!writeFrame(sp[0], AUDIOD_MSG_AUDIO, pcm.data(), pcm.size(), AUDIOD_SEND_STUCK_MS)) break;
      sentOk++;
    }
    allSent = true;
  });
  usleep(1500 * 1000);                  // the stall: reader reads nothing for 1.5 s
  // drain, parsing every frame, until the writer is done and the pipe is empty
  FrameParser p;
  std::vector<Message> got;
  uint8_t buf[4096];
  while (!allSent || true) {
    ssize_t n = recv(sp[1], buf, sizeof buf, MSG_DONTWAIT);
    if (n > 0) { p.feed(buf, (size_t)n); Message m; while (p.next(m)) got.push_back(m); continue; }
    if (allSent && n < 0) break;
    usleep(5000);
  }
  writer.join();
  // final drain
  for (;;) { ssize_t n = recv(sp[1], buf, sizeof buf, MSG_DONTWAIT); if (n <= 0) break; p.feed(buf, (size_t)n); Message m; while (p.next(m)) got.push_back(m); }
  bool intact = got.size() == (size_t)N && p.error().empty();
  for (auto& m : got) if (m.type != AUDIOD_MSG_AUDIO || m.payload != pcm) intact = false;
  char msg[160];
  snprintf(msg, sizeof msg, "rode out a 1.5 s reader stall: %d/%d frames sent, %zu received byte-exact (atomic across the stall)", sentOk.load(), N, got.size());
  CHECK(sentOk == N && intact, msg);

  // 2. A short budget must still give up when the reader never comes back (dead audiod): fill the
  //    buffer, then writeFrame with a 300 ms budget returns false within ~1 s, not never.
  int sp2[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sp2);
  setsockopt(sp2[0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof snd);
  setsockopt(sp2[0], SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  std::vector<uint8_t> big(16 * 1024, 0xa5);  // overfill in one go so the next frame blocks
  writeFrame(sp2[0], AUDIOD_MSG_AUDIO, big.data(), big.size(), 0);
  struct timeval t0, t1;
  gettimeofday(&t0, nullptr);
  bool ok = writeFrame(sp2[0], AUDIOD_MSG_AUDIO, big.data(), big.size(), 300);  // reader never reads
  gettimeofday(&t1, nullptr);
  long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
  snprintf(msg, sizeof msg, "a dead reader is detected: writeFrame(budget 300 ms) returned %s after %ld ms", ok ? "true" : "false", ms);
  CHECK(!ok && ms >= 300 && ms < 2500, msg);

  printf(fails ? "\n%d FAILURES\n" : "\nall writeframe checks passed\n", fails);
  return fails ? 1 : 0;
}
