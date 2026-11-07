#include "Server.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
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

Server::Server(const ServerOptions& o, const Tunables& t, AudioOutput* output) : opts(o), arbiter(t), out(output) {
  startMs = nowMs();
  if (!opts.eventLog.empty()) eventFile = fopen(opts.eventLog.c_str(), "w");
}

Server::~Server() {
  for (auto& kv : clients) close(kv.first);
  if (listenFd >= 0) { close(listenFd); unlink(opts.sockPath.c_str()); }
  if (eventFile) fclose(eventFile);
}

bool Server::listen() {
  listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenFd < 0) return false;
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, opts.sockPath.c_str(), sizeof a.sun_path - 1);
  unlink(opts.sockPath.c_str());  // a stale socket from a previous instance
  if (bind(listenFd, (sockaddr*)&a, sizeof a) != 0) { IOS3_TRACE("bind %s: %s", opts.sockPath.c_str(), strerror(errno)); return false; }
  chmod(opts.sockPath.c_str(), 0660);
  if (::listen(listenFd, 8) != 0) return false;
  IOS3_TRACE("listening on %s (gap %llu, takeover %llu, hold %llu, release %llu, idle-stop %llu ms)", opts.sockPath.c_str(),
             (unsigned long long)arbiter.tunables().producingGapMs, (unsigned long long)arbiter.tunables().takeoverMs,
             (unsigned long long)arbiter.tunables().holdMs, (unsigned long long)arbiter.tunables().releaseMs,
             (unsigned long long)arbiter.tunables().idleStopMs);
  writeActiveSourceFile("none");
  return true;
}

void Server::acceptClient() {
  int fd = accept(listenFd, nullptr, nullptr);
  if (fd < 0) return;
  Client c;
  c.fd = fd;
  c.id = nextId++;
  c.ring.resize(kRingFrames * 2);
  clients[fd] = std::move(c);
  IOS3_TRACE("client #%d connected (fd %d)", c.id, fd);
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
    arbiter.addSource(c.id, c.name, h.priority, now);
    IOS3_TRACE("client #%d is source '%s' (priority %u)", c.id, c.name.c_str(), h.priority);
    return;
  }
  switch (m.type) {
    case AUDIOD_MSG_AUDIO:
      if (m.payload.size() % 4 != 0) { dropClient(c.fd, "AUDIO length not a multiple of a frame"); return; }
      ringAppend(c, reinterpret_cast<const int16_t*>(m.payload.data()), m.payload.size() / 4);
      arbiter.onAudio(c.id, m.payload.size(), now);
      afterTick();
      break;
    case AUDIOD_MSG_FLUSH:
      c.ringHead = 0; c.ringFrames = 0;
      if (arbiter.activeId() == c.id) out->flush();
      break;
    case AUDIOD_MSG_STOP:
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
  static const char* names[] = { "takeover", "release", "idle-stop", "output-start" };
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
  if (opts.statusDir.empty() || name == lastActiveWritten) return;
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
    if (e.kind == Event::TAKEOVER) {
      out->flush();
      lastSilenceMs = 0;
      // Start with the new source's lead (the latest kLeadFrames it sent); anything older is stale.
      if (Client* c = clientById(arbiter.activeId())) {
        if (c->ringFrames > kLeadFrames) { c->ringHead = (c->ringHead + c->ringFrames - kLeadFrames) % kRingFrames; c->ringFrames = kLeadFrames; }
      }
    }
    if (e.kind == Event::RELEASE) { out->flush(); lastSilenceMs = e.atMs; }
    if (e.kind == Event::IDLE_STOP) { out->flush(); out->idle(); }
  }
  std::string a = arbiter.activeName();
  writeActiveSourceFile(a.empty() ? "none" : a);
  pump();
}

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
    for (auto& kv : clients) pfds.push_back({kv.first, POLLIN, 0});
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
