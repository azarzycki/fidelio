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
#include <string>

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
  bool ok;
  if (welcomed && haveSlider) {
    AudiodVolume2 v = { volume, slider, (uint16_t)(AUDIOD_VOL_SLIDER | (sliderApply ? AUDIOD_VOL_APPLY : 0)) };
    ok = audiod::writeFrame(fd, AUDIOD_MSG_VOLUME, &v, sizeof v);
  } else {
    AudiodVolume v = { volume };  // a v1 server (or no WELCOME yet): the gain only
    ok = audiod::writeFrame(fd, AUDIOD_MSG_VOLUME, &v, sizeof v);
  }
  if (!ok) { disconnect("volume"); return false; }
  return true;
}

void SocketSink::enableServerMessages() {
  std::lock_guard<std::mutex> l(m);
  v2 = true;
}

// m held. Reads everything the server sent (non-blocking) and parses WELCOME / HWVOLUME; the
// callback is queued in pendingReport and fired by the caller once the mutex is released.
void SocketSink::drainIncoming() {
  if (fd < 0 || !v2) return;
  uint8_t buf[256];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
    if (n > 0) { inParser.feed(buf, (size_t)n); continue; }
    if (n == 0) { disconnect("server closed"); return; }
    if (errno == EINTR) continue;
    break;  // EAGAIN: nothing more
  }
  if (!inParser.error().empty()) { disconnect("bad server message"); return; }
  audiod::Message msg;
  while (inParser.next(msg)) {
    if (msg.type == AUDIOD_MSG_WELCOME && msg.payload.size() == sizeof(AudiodWelcome)) {
      AudiodWelcome w;
      memcpy(&w, msg.payload.data(), sizeof w);
      welcomed = true;
      absolute = (w.flags & AUDIOD_VOL_ABSOLUTE) != 0;
      hwSlider = w.slider;
      fprintf(stdout, "SocketSink[%s]: WELCOME v%u: hardware volume %s%s\n", name.c_str(), w.version, absolute ? "absolute, level " : "software",
              absolute ? (std::to_string(hwSlider * 100 / 65535) + " % (slider " + std::to_string(hwSlider) + ")").c_str() : "");
      fflush(stdout);
      pendingReport = { true, absolute, hwSlider };
      if (absolute) {
        // The hardware level is the truth now: adopt it, and tell the server (report-only: no set)
        // that this source is slider-driven, so it plays at unity gain under the hardware level.
        slider = hwSlider;
        haveSlider = true;
        volume = 1.0f;
        sliderApply = false;
        sendVolume();
      } else if (haveSlider) {
        sendVolume();  // software mode on a v2 server: the 8-byte form (gain + slider), same effect as B1.7
      }
    } else if (msg.type == AUDIOD_MSG_HWVOLUME && msg.payload.size() == sizeof(AudiodHwVolume)) {
      AudiodHwVolume h;
      memcpy(&h, msg.payload.data(), sizeof h);
      absolute = (h.flags & AUDIOD_VOL_ABSOLUTE) != 0;
      hwSlider = h.slider;
      if (haveSlider) slider = h.slider;  // what we would echo is now the true level
      pendingReport = { true, absolute, hwSlider };
    }
    // anything else from the server is ignored
  }
}

void SocketSink::fireReport() {
  void (*cb)(void*, bool, uint16_t);
  void* user;
  HwReport r;
  {
    std::lock_guard<std::mutex> l(m);
    r = pendingReport;
    pendingReport.fire = false;
    cb = hwCb;
    user = hwUser;
  }
  if (r.fire && cb) cb(user, r.absolute, r.slider);
}

void SocketSink::poll() {
  {
    std::lock_guard<std::mutex> l(m);
    if (fd < 0 && v2) ensureConnected();  // eager (rate-limited): WELCOME should arrive before any audio
    drainIncoming();
  }
  fireReport();
}

void SocketSink::setVolumeSlider(float linear, uint16_t sl, bool apply) {
  {
    std::lock_guard<std::mutex> l(m);
    if (!(linear >= 0.0f)) linear = 0;
    if (linear > 1) linear = 1;
    volume = linear;
    slider = sl;
    haveSlider = true;
    sliderApply = apply;
    if (fd < 0 && v2) ensureConnected();
    sendVolume();
  }
  fireReport();
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
  AudiodHello h = audiod::makeHello(name, priority, v2 ? AUDIOD_PROTO_VERSION_2 : AUDIOD_PROTO_VERSION);
  if (!audiod::writeFrame(s, AUDIOD_MSG_HELLO, &h, sizeof h)) {
    close(s);
    nextAttemptMs = now + backoffMs;
    return false;
  }
  fd = s;
  backoffMs = 200;
  reconnectCount++;
  welcomed = false;  // a new connection (maybe a new audiod): the 8-byte VOLUME waits for its WELCOME
  inParser = audiod::FrameParser();
  int eff = 0; socklen_t el = sizeof eff;
  getsockopt(s, SOL_SOCKET, SO_SNDBUF, &eff, &el);
  fprintf(stdout, "SocketSink[%s]: connected to %s (connection #%lu, lead %u ms, SO_SNDBUF %d)\n", name.c_str(), sockPath.c_str(), reconnectCount,
          leadMsCap, eff);
  fflush(stdout);
  if ((volume < 0.999f || haveSlider) && !sendVolume()) return false;  // audiod starts every source at 1.0
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
  {
    std::lock_guard<std::mutex> l(m);
    pace(numFrames);
    lastWriteMs = nowMs();
    timelineFrames += numFrames;
    if (!ensureConnected()) { droppedFrames += numFrames; return; }
    drainIncoming();  // v2: WELCOME / HWVOLUME must never pile up unread
    if (fd < 0) { droppedFrames += numFrames; return; }
    while (numFrames > 0) {
      size_t n = numFrames < kMaxFramesPerMsg ? numFrames : kMaxFramesPerMsg;
      if (!audiod::writeFrame(fd, AUDIOD_MSG_AUDIO, frames, n * 4)) {
        disconnect(errno == EAGAIN || errno == EWOULDBLOCK ? "send timeout, audiod not reading" : "write");
        droppedFrames += numFrames;
        break;
      }
      frames += n * 2;
      numFrames -= n;
    }
  }
  fireReport();
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
  haveSlider = false;  // a plain gain (raop): never slider-driven
  sendVolume();
}
