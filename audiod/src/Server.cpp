#include "Server.h"

#include <math.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include "ios3_trace.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

namespace audiod {

volatile sig_atomic_t Server::stopRequested = 0;
void Server::requestStop(int) { stopRequested = 1; }

uint64_t Server::nowMs() {
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

// true as soon as one |sample| exceeds thr (early exit: music returns within a few samples)
static bool peakExceeds(const int16_t* s, size_t n, int thr) {
  for (size_t i = 0; i < n; i++) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > thr) return true; }
  return false;
}

Server::Server(const ServerOptions& o, const Tunables& t, AudioOutput* output, HardwareVolume* hwv, DisplayWaker* w)
    : opts(o), arbiter(t), out(output), hw(hwv), waker(w) {
  startMs = nowMs();
  leadFrames = (size_t)opts.leadMs * 44100 / 1000;
  if (leadFrames > kRingFrames) leadFrames = kRingFrames;
  if (!opts.eventLog.empty()) eventFile = fopen(opts.eventLog.c_str(), "w");
  // B1.9 A: absolute Spotify volume needs the hardware API; read the level once (never set at startup)
  if (hw) {
    hwAvailable = hw->available() && hw->get(hwLevel);
    if (!hwAvailable) hwLevel = -1;
  }
  if (opts.absoluteVolume) {
    if (absoluteActive())
      IOS3_TRACE("spotify volume: absolute (the slider drives the hardware; max %d %%, curve %s): %s, level now %.3f (slider %u)", opts.maxVolumePct,
                 opts.hwCurve == 1 ? "log" : opts.hwCurve == 2 ? "cubic" : "linear", hw->describe().c_str(), hwLevel, levelToSlider(hwLevel));
    else
      IOS3_TRACE("spotify volume: absolute requested but the hardware volume is unavailable (%s): falling back to software gain",
                 hw ? hw->describe().c_str() : "no backend");
  } else {
    IOS3_TRACE("spotify volume: software gain (B1.7 curve applied by the producer)");
  }
  IOS3_TRACE("wake-on-play: %s%s%s", opts.wakeOnPlay && waker ? "on (" : "off", opts.wakeOnPlay && waker ? waker->describe().c_str() : "",
             opts.wakeOnPlay && waker ? (", app " + opts.wakeBundle + ")").c_str() : "");
}

// ---------------------------------------------------------------- B1.9 A: absolute volume

float Server::sliderToLevel(uint16_t slider) const {
  if (slider == 0) return 0.f;
  float x = slider / 65535.f;
  if (opts.hwCurve == 1) return powf(10.f, (x - 1.f) * 60.f / 20.f);
  if (opts.hwCurve == 2) return x * x * x;
  return x;
}

uint16_t Server::levelToSlider(float level) const {
  if (!(level > 0.f)) return 0;
  if (level > 1.f) level = 1.f;
  float x;
  if (opts.hwCurve == 1) x = 1.f + 20.f * log10f(level) / 60.f;
  else if (opts.hwCurve == 2) x = cbrtf(level);
  else x = level;
  if (x < 0.f) x = 0.f;
  if (x > 1.f) x = 1.f;
  return (uint16_t)(x * 65535.f + 0.5f);
}

void Server::sendToClient(Client& c, uint32_t type, const void* payload, size_t len) {
  if (c.version < AUDIOD_PROTO_VERSION_2) return;  // a v1 producer never reads: nothing may pile up in its buffer
  uint8_t buf[64];
  AudiodMsgHeader h = { type, (uint32_t)len };
  memcpy(buf, &h, sizeof h);
  memcpy(buf + sizeof h, payload, len);
  int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  ssize_t n = send(c.fd, buf, sizeof h + len, flags);
  if (n != (ssize_t)(sizeof h + len) && !c.sendFailedLogged) {
    IOS3_TRACE("client #%d '%s': could not send message %u (%s); dropped", c.id, c.name.c_str(), type, n < 0 ? strerror(errno) : "short");
    c.sendFailedLogged = true;
  }
}

void Server::sendWelcome(Client& c) {
  AudiodWelcome w;
  memset(&w, 0, sizeof w);
  w.version = AUDIOD_PROTO_VERSION_2;
  bool abs = absoluteActive();
  w.flags = abs ? AUDIOD_VOL_ABSOLUTE : 0;
  w.slider = abs ? levelToSlider(hwLevel) : 0;
  sendToClient(c, AUDIOD_MSG_WELCOME, &w, sizeof w);
}

void Server::broadcastHwVolume() {
  AudiodHwVolume v = { levelToSlider(hwLevel), (uint16_t)(absoluteActive() ? AUDIOD_VOL_ABSOLUTE : 0) };
  for (auto& kv : clients) if (kv.second.hello) sendToClient(kv.second, AUDIOD_MSG_HWVOLUME, &v, sizeof v);
}

// A user moved the slider of a slider-driven source: set the hardware (clamped), log it, and if the
// clamp bit, tell every v2 client the level it actually got so the controller's slider snaps there.
void Server::applySlider(Client& c, uint16_t slider) {
  float want = sliderToLevel(slider);
  float ceiling = opts.maxVolumePct / 100.f;
  float level = want > ceiling ? ceiling : want;
  bool clamped = want > ceiling + 0.0005f;
  if (hwLevel >= 0 && fabsf(level - hwLevel) < 0.002f && !clamped) return;  // already there (the controller echoing our report)
  bool ok = hw->set(level);
  if (ok) hwLevel = level;
  char reason[160];
  snprintf(reason, sizeof reason, "set %.3f by '%s' slider %u%s%s", level, c.name.c_str(), slider, clamped ? " clamped from " : "", clamped ? std::to_string(want).substr(0, 5).c_str() : "");
  if (!ok) snprintf(reason, sizeof reason, "set FAILED (%.3f) by '%s' slider %u", level, c.name.c_str(), slider);
  logEvent(Event{Event::HWVOLUME, nowMs(), c.name, ok ? std::to_string(level).substr(0, 5) : "?", reason});
  if (ok && clamped) broadcastHwVolume();
}

// Every pollIntervalMs: pick up a change made elsewhere (the phone's buttons, another client) and
// report it to the v2 producers so their controllers show the true level.
void Server::pollHardwareVolume(uint64_t now) {
  if (!absoluteActive()) return;
  if (now - lastHwPollMs < hw->pollIntervalMs()) return;
  lastHwPollMs = now;
  float l = -1;
  bool changed = hw->takeChange(l);
  if (!changed) {
    float cur = -1;
    if (hw->get(cur) && cur >= 0 && (hwLevel < 0 || fabsf(cur - hwLevel) >= 0.002f)) { l = cur; changed = true; }
  }
  if (!changed || !(l >= 0)) return;
  if (hwLevel >= 0 && fabsf(l - hwLevel) < 0.002f) return;
  hwLevel = l;
  logEvent(Event{Event::HWVOLUME, now, "external", std::to_string(l).substr(0, 5), "hardware level changed (buttons / another app): reporting slider " + std::to_string(levelToSlider(l))});
  broadcastHwVolume();
}

// ---------------------------------------------------------------- B1.9 B: wake on play

void Server::maybeWake(const Event& e) {
  if (!opts.wakeOnPlay || !waker) return;
  if (e.kind != Event::TAKEOVER || !e.from.empty() || e.reason == "reconnect") return;  // only none -> source, not a switch or a reconnect
  if (lastWakeMs && e.atMs - lastWakeMs < opts.wakeMinIntervalMs) {
    logEvent(Event{Event::WAKE, e.atMs, "none", e.to, "debounced (" + std::to_string(e.atMs - lastWakeMs) + " ms since the last wake)"});
    return;
  }
  lastWakeMs = e.atMs;
  bool ok = waker->wake(opts.wakeBundle);
  logEvent(Event{Event::WAKE, e.atMs, "none", e.to, (ok ? "wake+launch " : "wake FAILED ") + opts.wakeBundle});
}

Server::~Server() {
  for (auto& kv : clients) close(kv.first);
  if (listenFd >= 0) { close(listenFd); unlink(opts.sockPath.c_str()); }
  if (eventFile) fclose(eventFile);
}

bool Server::listen() {
  listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenFd < 0) return false;
  // Accepted sockets inherit this; set on the listener so a reconnecting producer never starts on
  // the default (small) buffer. ~1.5 s of PCM the kernel can hold while this loop is in a slow
  // CoreAudio call. The kernel may clamp it (kern.ipc.maxsockbuf): the effective size is logged.
  // Deliberately small (B1.5): with backpressure the active producer blocks on this buffer, and the
  // total downstream of raop's jitter buffer (sink 0.5 s + kernel buffers) must stay well below its
  // latency or the jitter buffer can never hold a cushion. 8 KB = 46 ms per side (Linux doubles it).
  int rcvbuf = 8 * 1024;
  setsockopt(listenFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, opts.sockPath.c_str(), sizeof a.sun_path - 1);
  unlink(opts.sockPath.c_str());  // a stale socket from a previous instance
  if (bind(listenFd, (sockaddr*)&a, sizeof a) != 0) { IOS3_TRACE("bind %s: %s", opts.sockPath.c_str(), strerror(errno)); return false; }
  chmod(opts.sockPath.c_str(), 0660);
  if (::listen(listenFd, 8) != 0) return false;
  IOS3_TRACE("listening on %s (gap %llu, takeover %llu, hold %llu, release %llu, quiet-release %llu, idle-stop %llu ms; silence threshold %d; lead %u ms)",
             opts.sockPath.c_str(), (unsigned long long)arbiter.tunables().producingGapMs, (unsigned long long)arbiter.tunables().takeoverMs,
             (unsigned long long)arbiter.tunables().holdMs, (unsigned long long)arbiter.tunables().releaseMs,
             (unsigned long long)arbiter.tunables().quietReleaseMs, (unsigned long long)arbiter.tunables().idleStopMs,
             arbiter.tunables().silenceThreshold, opts.leadMs);
  IOS3_TRACE("backpressure on the active source: %s; crossfade %u ms", opts.backpressure ? "on" : "off (b14 behaviour)", opts.crossfadeMs);
  writeActiveSourceFile("none");
  return true;
}

void Server::acceptClient() {
  int fd = accept(listenFd, nullptr, nullptr);
  if (fd < 0) return;
  int rcvbuf = 8 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);  // in case the listener's did not carry over
  int eff = 0; socklen_t el = sizeof eff;
  getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &eff, &el);
  Client c;
  c.fd = fd;
  c.id = nextId++;
  c.ring.resize(kRingFrames * 2);
  int id = c.id;
  clients[fd] = std::move(c);
  IOS3_TRACE("client #%d connected (fd %d, SO_RCVBUF %d)", id, fd, eff);
}

void Server::dropClient(int fd, const char* why) {
  auto it = clients.find(fd);
  if (it == clients.end()) return;
  IOS3_TRACE("client #%d '%s' dropped: %s%s", it->second.id, it->second.name.c_str(), why,
             it->second.overflowFrames ? " (while active it ran ahead of the output; oldest frames were dropped)" : "");
  if (it->second.hello) arbiter.removeSource(it->second.id, nowMs());
  close(fd);
  clients.erase(it);
  afterTick();
}

void Server::readClient(Client& c) {
  uint8_t buf[16384];
  ssize_t n = recv(c.fd, buf, sizeof buf, 0);
  if (n <= 0) { dropClient(c.fd, n == 0 ? "closed" : strerror(errno)); return; }
  c.parser.feed(buf, (size_t)n);
  Message m;
  int fd = c.fd;
  while (clients.count(fd) && c.parser.next(m)) handle(c, m);
  if (clients.count(fd) && !c.parser.error().empty()) dropClient(fd, c.parser.error().c_str());
}

void Server::handle(Client& c, const Message& m) {
  uint64_t now = nowMs();
  if (!c.hello) {
    AudiodHello h;
    std::string why;
    if (!parseHello(m, h, why)) { dropClient(c.fd, ("bad HELLO: " + why).c_str()); return; }
    c.hello = true;
    c.name = helloName(h);
    c.version = h.version;
    bool back = arbiter.addSource(c.id, c.name, h.priority, now);
    IOS3_TRACE("client #%d is source '%s' (priority %u, protocol v%u)%s", c.id, c.name.c_str(), h.priority, h.version, back ? ", reconnected: same stream" : "");
    sendWelcome(c);  // v2 producers learn whether the slider drives the hardware, and the current level
    afterTick();  // a reconnecting active source is active again right away
    return;
  }
  switch (m.type) {
    case AUDIOD_MSG_AUDIO: {
      if (m.payload.size() % 4 != 0) { dropClient(c.fd, "AUDIO length not a multiple of a frame"); return; }
      const int16_t* pcm = reinterpret_cast<const int16_t*>(m.payload.data());
      int thr = arbiter.tunables().silenceThreshold;
      bool loud = thr <= 0 || peakExceeds(pcm, m.payload.size() / 2, thr);
      ringAppend(c, pcm, m.payload.size() / 4);
      arbiter.onAudio(c.id, m.payload.size(), now, loud);
      afterTick();
      break;
    }
    case AUDIOD_MSG_VOLUME: {
      float linear;
      uint16_t slider, flags;
      if (!parseVolume(m, linear, slider, flags)) { dropClient(c.fd, "bad VOLUME length"); return; }
      if (flags & AUDIOD_VOL_SLIDER) {
        // B1.9 A: a slider-driven source. Absolute mode: unity gain at the output, the slider sets the
        // hardware (only on an APPLY = a user move; the startup echo is report-only). Software mode
        // (or no hardware API): the producer's curved gain, exactly B1.7.
        c.sliderDriven = true;
        c.slider = slider;
        if (absoluteActive()) {
          c.volume = 1.0f;
          if (flags & AUDIOD_VOL_APPLY) applySlider(c, slider);
        } else {
          c.volume = linear;
        }
      } else {
        c.sliderDriven = false;
        c.volume = linear;
      }
      if (arbiter.activeId() == c.id) out->setVolume(c.volume);
      break;
    }
    case AUDIOD_MSG_FLUSH:
      if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: FLUSH from #%d (had %zu frames, head %zu)", c.id, c.ringFrames, c.ringHead);
      c.ringHead = 0; c.ringFrames = 0;
      arbiter.onFlush(c.id);
      if (arbiter.activeId() == c.id) out->flush();
      break;
    case AUDIOD_MSG_STOP:
      if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: STOP from #%d (%zu frames in ring)", c.id, c.ringFrames);
      arbiter.onStop(c.id, now);
      afterTick();
      break;
    case AUDIOD_MSG_META:
      break;  // v2: unified now-playing status
    case AUDIOD_MSG_HELLO:
      dropClient(c.fd, "second HELLO");
      return;
  }
}

void Server::logEvent(const Event& e) {
  static const char* names[] = { "takeover", "release", "idle-stop", "output-start", "wake", "hwvolume" };
  const char* k = names[e.kind];
  time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char wall[32];
  strftime(wall, sizeof wall, "%H:%M:%SZ", &tmv);
  IOS3_TRACE("%s +%llums %s: '%s' -> '%s' (%s)", wall, (unsigned long long)(e.atMs - startMs), k, e.from.empty() ? "none" : e.from.c_str(), e.to.empty() ? "none" : e.to.c_str(),
             e.reason.c_str());
  if (eventFile) {
    fprintf(eventFile, "%llu %s %s %s %s\n", (unsigned long long)(e.atMs - startMs), k, e.from.empty() ? "none" : e.from.c_str(),
            e.to.empty() ? "none" : e.to.c_str(), e.reason.c_str());
    fflush(eventFile);
  }
}

void Server::writeActiveSourceFile(const std::string& name) {
  if (opts.statusDir.empty()) return;
  uint64_t now = nowMs();
  if (name == lastActiveWritten && now - lastActiveWriteMs < 10000) return;  // rewritten every 10 s so a reader can tell a dead audiod by the mtime
  lastActiveWriteMs = now;
  std::string path = opts.statusDir + "/active-source", tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return;
  fprintf(f, "%s\n", name.c_str());
  fclose(f);
  rename(tmp.c_str(), path.c_str());
  lastActiveWritten = name;
}

void Server::afterTick() {
  for (const Event& e : arbiter.drainEvents()) {
    logEvent(e);
    maybeWake(e);  // B1.9 B: none -> source
    if (e.kind == Event::TAKEOVER) {
      out->crossfade(fadeFrames());  // the old source's tail ramps out under the new one's ramp-in; its lead is dropped
      lastSilenceMs = 0;
      // Start with the new source's lead (the latest leadFrames it sent); anything older is stale.
      if (Client* c = clientById(arbiter.activeId())) {
        if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: takeover #%d with %zu frames (head %zu)", c->id, c->ringFrames, c->ringHead);
        if (c->ringFrames > leadFrames) { c->ringHead = (c->ringHead + c->ringFrames - leadFrames) % kRingFrames; c->ringFrames = leadFrames; }
        out->setVolume(c->volume);
      }
    }
    if (e.kind == Event::RELEASE) {
      // "quiet" (bytes stopped) / stop / disconnect: drop what is left. "silent" (digital silence
      // still arriving): let the buffered tail play out; a fade or a pause between tracks is not a cut.
      if (e.reason.compare(0, 6, "silent") != 0) out->crossfade(fadeFrames());  // a fade-out, not a cut
      lastSilenceMs = e.atMs;
    }
    if (e.kind == Event::IDLE_STOP) { out->flush(); out->idle(); }
  }
  std::string a = arbiter.activeName();
  writeActiveSourceFile(a.empty() ? "none" : a);
  pump();
}

size_t Server::fadeFrames() const { return (size_t)opts.crossfadeMs * 44100 / 1000; }

Server::Client* Server::clientById(int id) {
  for (auto& kv : clients) if (kv.second.id == id) return &kv.second;
  return nullptr;
}

void Server::ringAppend(Client& c, const int16_t* frames, size_t n) {
  bool active = arbiter.activeId() == c.id;  // an inactive source's ring overwriting itself is normal
  if (n > kRingFrames) { frames += (n - kRingFrames) * 2; if (active) c.overflowFrames += n - kRingFrames; n = kRingFrames; }
  if (c.ringFrames + n > kRingFrames) {  // drop the oldest (active: the producer ran ahead of the pump)
    size_t drop = c.ringFrames + n - kRingFrames;
    c.ringHead = (c.ringHead + drop) % kRingFrames;
    c.ringFrames -= drop;
    if (active) c.overflowFrames += drop;
  }
  size_t tail = (c.ringHead + c.ringFrames) % kRingFrames;
  size_t first = kRingFrames - tail;
  if (first > n) first = n;
  memcpy(&c.ring[tail * 2], frames, first * 4);
  if (n > first) memcpy(&c.ring[0], frames + first * 2, (n - first) * 4);
  c.ringFrames += n;
}

void Server::pump() {
  if (arbiter.activeId() < 0 || !arbiter.outputRunning()) return;
  Client* c = clientById(arbiter.activeId());
  if (!c) return;
  while (c->ringFrames > 0 && out->bufferedSeconds() < opts.sinkTargetSec) {
    size_t n = c->ringFrames < 882 ? c->ringFrames : 882;  // 20 ms slices
    if (getenv("AUDIOD_TRACE_RING")) IOS3_TRACE("ring: pump #%d %zu frames (head %zu, %zu in ring)", c->id, n, c->ringHead, c->ringFrames);
    size_t first = kRingFrames - c->ringHead;
    if (first > n) first = n;
    out->write(&c->ring[c->ringHead * 2], first);
    if (n > first) out->write(&c->ring[0], n - first);
    c->ringHead = (c->ringHead + n) % kRingFrames;
    c->ringFrames -= n;
  }
}

void Server::run() {
  static int16_t silence[441 * 2 * 4];  // 40 ms
  memset(silence, 0, sizeof silence);
  while (!stopRequested) {
    std::vector<pollfd> pfds;
    pfds.push_back({listenFd, POLLIN, 0});
    // Backpressure: do not read the active source while its staging ring already holds audio the
    // output has no room for; its producer then blocks on the kernel buffers and is paced by the
    // output's real consumption. Not while the output is paused (interruption / yield): then it is
    // drained and overwritten like an inactive source, so it never hard-blocks.
    int activeId = arbiter.activeId();
    bool gateActive = opts.backpressure && activeId >= 0 && arbiter.outputRunning() && out->consuming();
    for (auto& kv : clients) {
      short ev = POLLIN;
      if (gateActive && kv.second.id == activeId && kv.second.ringFrames >= kStagingFrames) ev = 0;
      pfds.push_back({kv.first, ev, 0});
    }
    int rc = poll(pfds.data(), pfds.size(), opts.tickMs);
    if (rc < 0) { if (errno == EINTR) continue; IOS3_TRACE("poll: %s", strerror(errno)); return; }
    for (size_t i = 1; i < pfds.size(); i++) {
      if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      auto it = clients.find(pfds[i].fd);
      if (it != clients.end()) readClient(it->second);
    }
    if (pfds[0].revents & POLLIN) acceptClient();
    uint64_t now = nowMs();
    arbiter.tick(now);
    afterTick();
    out->tick();
    pollHardwareVolume(now);  // B1.9 A: buttons / external changes -> the producers
    if (out->resumed()) {
      // The output was paused (interruption / yielding to another app) and just resumed: the active
      // source's ring holds audio from before the pause. Drop it; the producer keeps sending current audio.
      if (Client* c = clientById(arbiter.activeId())) { c->ringHead = 0; c->ringFrames = 0; }
      IOS3_TRACE("output resumed: stale buffered audio of the active source dropped");
    }
    // Keep the output primed with silence between sources so the queue (and the route) stays up
    // until the idle stop; paced by wall clock in 40 ms slices.
    if (opts.feedSilence && arbiter.activeId() < 0 && arbiter.outputRunning()) {
      if (lastSilenceMs == 0) lastSilenceMs = now;
      while (now - lastSilenceMs >= 40) {
        out->write(silence, 441 * 4);
        lastSilenceMs += 40;
      }
    }
  }
  IOS3_TRACE("shutdown requested: releasing the output");
  out->flush();
  out->idle();
  writeActiveSourceFile("none");
}

}  // namespace audiod
