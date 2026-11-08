#include "SocketSink.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
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
  const char* e = getenv("AUDIOD_LEAD_MS");
  if (e && *e) leadMsCap = (unsigned)atoi(e);
}

bool SocketSink::sendVolume() {
  if (fd < 0) return true;
  AudiodVolume v = { volume };
  if (!audiod::writeFrame(fd, AUDIOD_MSG_VOLUME, &v, sizeof v)) { disconnect("volume"); return false; }
  return true;
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
  struct timeval to = { kSendTimeoutMs / 1000, (kSendTimeoutMs % 1000) * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  int sndbuf = kSendBufBytes;
  setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
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
  int eff = 0; socklen_t el = sizeof eff;
  getsockopt(s, SOL_SOCKET, SO_SNDBUF, &eff, &el);
  fprintf(stdout, "SocketSink[%s]: connected to %s (connection #%lu, lead %u ms, SO_SNDBUF %d)\n", name.c_str(), sockPath.c_str(), reconnectCount,
          leadMsCap, eff);
  fflush(stdout);
  if (volume < 0.999f && !sendVolume()) return false;  // audiod starts every source at 1.0
  return true;
}

void SocketSink::disconnect(const char* why) {
  int err = errno;  // before close()/printf can change it
  if (fd >= 0) {
    fprintf(stdout, "SocketSink[%s]: disconnected (%s: %s), will reconnect\n", name.c_str(), why, strerror(err));
    fflush(stdout);
    close(fd);
    fd = -1;
  }
  nextAttemptMs = nowMs() + backoffMs;
}

long SocketSink::leadMs(uint64_t now) const {
  if (!timelineStartMs) return 0;
  return (long)(timelineFrames * 1000 / 44100) - (long)(now - timelineStartMs);
}

void SocketSink::pace(size_t framesAboutToSend) {
  uint64_t now = nowMs();
  if (!timelineStartMs || now - lastWriteMs > kGapResetMs) { timelineStartMs = now; timelineFrames = 0; }
  long ahead = leadMs(now) + (long)(framesAboutToSend * 1000 / 44100);
  if (ahead > (long)leadMsCap) usleep((useconds_t)((ahead - leadMsCap) * 1000));
}

double SocketSink::bufferedSeconds() {
  std::lock_guard<std::mutex> lk(m);
  long l = leadMs(nowMs());
  return l > 0 ? l / 1000.0 : 0.0;
}

void SocketSink::write(const int16_t* frames, size_t numFrames) {
  std::lock_guard<std::mutex> l(m);
  pace(numFrames);
  lastWriteMs = nowMs();
  timelineFrames += numFrames;
  if (!ensureConnected()) { droppedFrames += numFrames; return; }
  while (numFrames > 0) {
    size_t n = numFrames < kMaxFramesPerMsg ? numFrames : kMaxFramesPerMsg;
    if (!audiod::writeFrame(fd, AUDIOD_MSG_AUDIO, frames, n * 4)) {
      disconnect(errno == EAGAIN || errno == EWOULDBLOCK ? "send timeout, audiod not reading" : "write");
      droppedFrames += numFrames;
      return;
    }
    frames += n * 2;
    numFrames -= n;
  }
}

void SocketSink::flush() {
  std::lock_guard<std::mutex> l(m);
  timelineStartMs = 0;  // the next write starts a fresh lead
  if (fd < 0) return;
  if (!audiod::writeFrame(fd, AUDIOD_MSG_FLUSH, nullptr, 0)) disconnect("flush");
}

void SocketSink::idle() {
  std::lock_guard<std::mutex> lk(m);
  // Let the lead play out first, so audiod's immediate release on STOP cuts nothing.
  long l = leadMs(nowMs());
  if (l > 0) usleep((useconds_t)(l * 1000));
  timelineStartMs = 0;
  if (fd < 0) return;
  if (!audiod::writeFrame(fd, AUDIOD_MSG_STOP, nullptr, 0)) disconnect("stop");
}

void SocketSink::setVolume(float linear) {
  std::lock_guard<std::mutex> l(m);
  if (!(linear >= 0.0f)) linear = 0;  // also catches NaN
  if (linear > 1) linear = 1;
  volume = linear;
  sendVolume();
}
