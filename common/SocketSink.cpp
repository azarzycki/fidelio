#include "SocketSink.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

#include "AudiodFraming.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

static const uint64_t kMaxBackoffMs = 3000;
static const size_t kMaxFramesPerMsg = AUDIOD_MAX_PAYLOAD / 4;  // 16384 frames = 371 ms

uint64_t SocketSink::nowMs() {
#ifdef __APPLE__
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  return (t / tb.denom) * tb.numer / 1000000ull + ((t % tb.denom) * tb.numer / tb.denom) / 1000000ull;
#else
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

SocketSink::SocketSink(const std::string& n, const std::string& p, uint8_t prio) : name(n), sockPath(p), priority(prio) {
  signal(SIGPIPE, SIG_IGN);  // a dead audiod must not kill the producer (Darwin has no MSG_NOSIGNAL)
}

SocketSink::~SocketSink() {
  if (fd >= 0) close(fd);
}

bool SocketSink::ensureConnected() {
  if (fd >= 0) return true;
  uint64_t now = nowMs();
  if (now < nextAttemptMs) return false;
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return false;
#ifdef SO_NOSIGPIPE
  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, sockPath.c_str(), sizeof a.sun_path - 1);
  if (connect(s, (sockaddr*)&a, sizeof a) != 0) {
    close(s);
    nextAttemptMs = now + backoffMs;
    backoffMs = backoffMs * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoffMs * 2;
    return false;
  }
  AudiodHello h = audiod::makeHello(name, priority);
  if (!audiod::writeFrame(s, AUDIOD_MSG_HELLO, &h, sizeof h)) {
    close(s);
    nextAttemptMs = now + backoffMs;
    return false;
  }
  fd = s;
  backoffMs = 200;
  reconnectCount++;
  fprintf(stdout, "SocketSink[%s]: connected to %s (connection #%lu)\n", name.c_str(), sockPath.c_str(), reconnectCount);
  fflush(stdout);
  return true;
}

void SocketSink::disconnect(const char* why) {
  if (fd >= 0) {
    fprintf(stdout, "SocketSink[%s]: disconnected (%s: %s), will reconnect\n", name.c_str(), why, strerror(errno));
    fflush(stdout);
    close(fd);
    fd = -1;
  }
  nextAttemptMs = nowMs() + backoffMs;
}

void SocketSink::write(const int16_t* frames, size_t numFrames) {
  if (!ensureConnected()) { droppedFrames += numFrames; return; }
  while (numFrames > 0) {
    size_t n = numFrames < kMaxFramesPerMsg ? numFrames : kMaxFramesPerMsg;
    if (!audiod::writeFrame(fd, AUDIOD_MSG_AUDIO, frames, n * 4)) {
      disconnect("write");
      droppedFrames += numFrames;
      return;
    }
    frames += n * 2;
    numFrames -= n;
  }
}

void SocketSink::flush() {
  if (fd < 0) return;
  if (!audiod::writeFrame(fd, AUDIOD_MSG_FLUSH, nullptr, 0)) disconnect("flush");
}

void SocketSink::stop() {
  if (fd < 0) return;
  if (!audiod::writeFrame(fd, AUDIOD_MSG_STOP, nullptr, 0)) disconnect("stop");
}

void SocketSink::setVolume(float linear) { volume = linear; }
