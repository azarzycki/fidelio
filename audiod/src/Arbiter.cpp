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

bool Arbiter::addSource(int id, const std::string& name, uint8_t priority, uint64_t now) {
  // A same-name ghost (disconnected within ghostMs) is the same stream coming back: inherit it.
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    Source& g = it->second;
    if (g.connected || g.name != name || now - g.droppedAtMs > tun.ghostMs) continue;
    Source s = g;
    s.id = id; s.priority = priority; s.connected = true; s.droppedAtMs = 0;
    s.newStream = false;  // keep streamSinceMs: not a newer stream
    bool wasActive = active == it->first;
    sources.erase(it);
    sources[id] = s;
    if (wasActive) {
      active = id;
      events.push_back(Event{Event::TAKEOVER, now, name, name, "reconnect"});
    }
    return true;
  }
  Source s;
  s.id = id; s.name = name; s.priority = priority;
  sources[id] = s;
  (void)now;
  return false;
}

void Arbiter::removeSource(int id, uint64_t now) {
  auto it = sources.find(id);
  if (it == sources.end()) return;
  // Keep it as a ghost: a reconnect within ghostMs is the same stream. If it was active, the quiet
  // rule releases it after releaseMs unless it comes back first.
  it->second.connected = false;
  it->second.droppedAtMs = now;
  tick(now);
}

void Arbiter::onAudio(int id, size_t bytes, uint64_t now, bool loud) {
  auto it = sources.find(id);
  if (it == sources.end()) return;
  Source& s = it->second;
  // A new stream (recency event): first audio ever / after STOP / after FLUSH, or after the bytes
  // stopped for releaseMs. Not: a quiet passage, not a hiccup shorter than releaseMs.
  if (s.newStream || !s.haveAudio || now - s.lastAudioMs >= tun.releaseMs) { s.streamSinceMs = now; s.newStream = false; }
  if (loud) {
    if (!producing(s, now)) s.loudRunSinceMs = now;  // a new loud run after a gap (or the first)
    // Sound after >= quietReleaseMs of silence (or the first sound ever) is a resume: a new stream
    // for recency, symmetric with the silent release. A shorter quiet passage is not.
    if (!s.haveLoud || now - s.lastLoudMs >= tun.quietReleaseMs) s.streamSinceMs = now;
    s.haveLoud = true;
    s.lastLoudMs = now;
  }
  s.haveAudio = true;
  s.lastAudioMs = now;
  s.bytes += bytes;
  tick(now);
}

void Arbiter::onFlush(int id) {
  auto it = sources.find(id);
  if (it != sources.end()) it->second.newStream = true;
}

void Arbiter::onStop(int id, uint64_t now) {
  auto it = sources.find(id);
  if (it == sources.end()) return;
  it->second.haveLoud = false;  // not producing from now on
  it->second.haveAudio = false;
  it->second.newStream = true;
  if (id == active) release(now, "stop");
  tick(now);
}

void Arbiter::setActive(int id, uint64_t now, const std::string& reason) {
  Event e{Event::TAKEOVER, now, activeName(), sources[id].name, reason};
  active = id;
  activeSinceMs = now;
  activeStreamSinceMs = sources[id].streamSinceMs;
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
  // 1. release an active source that is gone (no bytes) or has streamed only silence for long
  if (active >= 0) {
    const Source& a = sources[active];
    if (!a.haveAudio || now - a.lastAudioMs >= tun.releaseMs) release(now, "quiet " + std::to_string(now - a.lastAudioMs) + " ms");
    else if (!a.haveLoud || now - a.lastLoudMs >= tun.quietReleaseMs) release(now, "silent " + std::to_string(now - a.lastLoudMs) + " ms");
  }
  // 1b. forget ghosts nobody reconnected to
  for (auto it = sources.begin(); it != sources.end();) {
    if (!it->second.connected && it->first != active && now - it->second.droppedAtMs > tun.ghostMs) it = sources.erase(it);
    else ++it;
  }
  // 2. takeover by the most recent qualified challenger
  bool allowed = active < 0 || now - activeSinceMs >= tun.holdMs;
  if (allowed) {
    const Source* best = nullptr;
    for (auto& kv : sources) {
      const Source& s = kv.second;
      if (s.id == active || !s.connected || !producing(s, now)) continue;
      if (now - s.loudRunSinceMs < tun.takeoverMs) continue;
      if (active >= 0 && s.streamSinceMs <= activeStreamSinceMs) continue;  // older stream: not "more recent"
      if (!best || s.streamSinceMs > best->streamSinceMs ||
          (s.streamSinceMs == best->streamSinceMs && s.priority > best->priority))
        best = &s;
    }
    if (best) {
      std::string why = active < 0 ? "no active source" : "hold expired after " + std::to_string(now - activeSinceMs) + " ms";
      why += ", challenger loud for " + std::to_string(now - best->loudRunSinceMs) + " ms (stream " + std::to_string(now - best->streamSinceMs) + " ms old)";
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
