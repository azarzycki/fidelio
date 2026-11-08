#include "Arbiter.h"

namespace audiod {

std::string Arbiter::activeName() const {
  auto it = sources.find(active);
  return it == sources.end() ? "" : it->second.name;
}

const Source* Arbiter::source(int id) const {
  auto it = sources.find(id);
  return it == sources.end() ? nullptr : &it->second;
}

void Arbiter::addSource(int id, const std::string& name, uint8_t priority, uint64_t now) {
  Source s;
  s.id = id; s.name = name; s.priority = priority;
  sources[id] = s;
  (void)now;
}

void Arbiter::removeSource(int id, uint64_t now) {
  if (id == active) release(now, "disconnect");
  sources.erase(id);
  tick(now);
}

void Arbiter::onAudio(int id, size_t bytes, uint64_t now, bool loud) {
  auto it = sources.find(id);
  if (it == sources.end()) return;
  Source& s = it->second;
  if (loud) {
    if (!producing(s, now)) s.producingSinceMs = now;  // a new producing run after a gap (or the first)
    s.haveLoud = true;
    s.lastLoudMs = now;
  }
  s.lastAudioMs = now;
  s.bytes += bytes;
  tick(now);
}

void Arbiter::onStop(int id, uint64_t now) {
  auto it = sources.find(id);
  if (it == sources.end()) return;
  it->second.haveLoud = false;  // not producing from now on
  if (id == active) release(now, "stop");
  tick(now);
}

void Arbiter::setActive(int id, uint64_t now, const std::string& reason) {
  Event e{Event::TAKEOVER, now, activeName(), sources[id].name, reason};
  active = id;
  activeSinceMs = now;
  activeProducingSinceMs = sources[id].producingSinceMs;
  everActive = true;
  if (!outputOn) { outputOn = true; events.push_back(Event{Event::OUTPUT_START, now, "", "", "first active source"}); }
  events.push_back(e);
}

void Arbiter::release(uint64_t now, const std::string& reason) {
  if (active < 0) return;
  events.push_back(Event{Event::RELEASE, now, activeName(), "", reason});
  active = -1;
  releasedAtMs = now;
}

void Arbiter::tick(uint64_t now) {
  // 1. release a quiet active source
  if (active >= 0) {
    const Source& a = sources[active];
    if (!a.haveLoud || now - a.lastLoudMs >= tun.releaseMs) release(now, "quiet " + std::to_string(now - a.lastLoudMs) + " ms");
  }
  // 2. takeover by the most recent qualified challenger
  bool allowed = active < 0 || now - activeSinceMs >= tun.holdMs;
  if (allowed) {
    const Source* best = nullptr;
    for (auto& kv : sources) {
      const Source& s = kv.second;
      if (s.id == active || !producing(s, now)) continue;
      if (now - s.producingSinceMs < tun.takeoverMs) continue;
      if (active >= 0 && s.producingSinceMs <= activeProducingSinceMs) continue;  // older stream: not "more recent"
      if (!best || s.producingSinceMs > best->producingSinceMs ||
          (s.producingSinceMs == best->producingSinceMs && s.priority > best->priority))
        best = &s;
    }
    if (best) {
      std::string why = active < 0 ? "no active source" : "hold expired after " + std::to_string(now - activeSinceMs) + " ms";
      why += ", challenger producing " + std::to_string(now - best->producingSinceMs) + " ms";
      setActive(best->id, now, why);
    }
  }
  // 3. idle stop
  if (active < 0 && outputOn && everActive && now - releasedAtMs >= tun.idleStopMs) {
    outputOn = false;
    events.push_back(Event{Event::IDLE_STOP, now, "", "", "no active source for " + std::to_string(now - releasedAtMs) + " ms"});
  }
}

std::vector<Event> Arbiter::drainEvents() {
  std::vector<Event> out;
  out.swap(events);
  return out;
}

}  // namespace audiod
