#include "StatusWriter.h"

#include <errno.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <variant>
#include <vector>

#include "HTTPClient.h"
#include "Logger.h"
#include "TLSSocket.h"
#include "TrackQueue.h"
#include "ios3_trace.h"

static std::string jsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += (char)c;  // UTF-8 passes through
        }
    }
  }
  return o;
}

static std::string xmlEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      default:
        if (c < 0x20 && c != '\n' && c != '\t') continue;  // not representable in XML 1.0
        o += (char)c;
    }
  }
  return o;
}

static bool writeAtomic(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = (fclose(f) == 0) && ok;
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  return true;
}

uint64_t StatusWriter::nowMs() {
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  return (t / tb.denom) * tb.numer / 1000000ull + ((t % tb.denom) * tb.numer / tb.denom) / 1000000ull;
}

StatusWriter::StatusWriter(const std::string& d, const std::string& deviceName, int bitrateKbps)
    : dir(d), device(deviceName), bitrate(bitrateKbps) {
  mkdir(dir.c_str(), 0755);  // ok if it exists
  pthread_mutex_init(&wakeMutex, nullptr);
  pthread_cond_init(&wake, nullptr);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 192 * 1024);  // does an HTTPS fetch (mbedTLS) for the cover
  if (pthread_create(&thread, &attr, threadEntry, this) != 0) {
    running = false;
    CSPOT_LOG(error, "StatusWriter: cannot start thread");
  }
  pthread_attr_destroy(&attr);
}

StatusWriter::~StatusWriter() {
  pthread_mutex_lock(&wakeMutex);
  running = false;
  pthread_cond_signal(&wake);
  pthread_mutex_unlock(&wakeMutex);
  pthread_join(thread, nullptr);
  pthread_cond_destroy(&wake);
  pthread_mutex_destroy(&wakeMutex);
}

void* StatusWriter::threadEntry(void* self) {
  static_cast<StatusWriter*>(self)->threadLoop();
  return nullptr;
}

uint32_t StatusWriter::currentPositionMs() {
  uint32_t pos = baseMs;
  if (clockRunning) pos += (uint32_t)(nowMs() - baseTime);
  if (durationMs && pos > durationMs) pos = durationMs;
  return pos;
}

void StatusWriter::setState(const char* s) {
  {
    std::scoped_lock l(mutex);
    state = s;
    if (state != "playing" && state != "paused") clockRunning = false;
  }
  pthread_mutex_lock(&wakeMutex);
  dirty = true;
  pthread_cond_signal(&wake);
  pthread_mutex_unlock(&wakeMutex);
}

void StatusWriter::setConnection(const std::string& a, const std::string& c) {
  {
    std::scoped_lock l(mutex);
    ap = a;
    country = c;
  }
  setState("connected");
}

void StatusWriter::setCountryProvider(std::function<std::string()> provider) {
  std::scoped_lock l(mutex);
  countryProvider = provider;
}

void StatusWriter::audioEnded() {
  {
    std::scoped_lock l(mutex);
    clockRunning = false;
  }
  setState("stopped");
}

void StatusWriter::onEvent(const cspot::SpircHandler::Event& event) {
  using E = cspot::SpircHandler::EventType;
  {
    std::scoped_lock l(mutex);
    switch (event.eventType) {
      case E::PLAYBACK_START:
        pendingStart = true;
        pendingStartMs = (uint32_t)std::get<int>(event.data);
        baseMs = pendingStartMs;
        baseTime = nowMs();
        clockRunning = false;  // starts when the audio really reaches the sink (TRACK_INFO)
        if (state != "paused") state = "playing";
        break;
      case E::TRACK_INFO: {
        auto& info = std::get<cspot::TrackInfo>(event.data);
        title = info.name;
        artist = info.artist;
        album = info.album;
        imageUrl = info.imageUrl;
        durationMs = info.duration;
        baseMs = pendingStart ? pendingStartMs : 0;
        pendingStart = false;
        baseTime = nowMs();
        clockRunning = (state == "playing");
        if (state != "paused") state = "playing";
        break;
      }
      case E::PLAY_PAUSE: {
        bool paused = std::get<bool>(event.data);
        uint32_t pos = currentPositionMs();
        baseMs = pos;
        baseTime = nowMs();
        clockRunning = !paused;
        state = paused ? "paused" : "playing";
        break;
      }
      case E::SEEK:
        baseMs = (uint32_t)std::get<int>(event.data);
        baseTime = nowMs();
        break;
      case E::DISC:
        clockRunning = false;
        state = "stopped";
        break;
      case E::DEPLETED:
      case E::FLUSH:
      case E::NEXT:
      case E::PREV:
      case E::VOLUME:
      default:
        break;
    }
  }
  pthread_mutex_lock(&wakeMutex);
  dirty = true;
  pthread_cond_signal(&wake);
  pthread_mutex_unlock(&wakeMutex);
}

bool StatusWriter::fetchCover(const std::string& url, std::string& outName) {
  try {
    auto response = bell::HTTPClient::get(url);
    auto bytes = response->bytes();
    if (bytes.size() < 100) {
      CSPOT_LOG(error, "cover: short response (%d bytes) from %s", (int)bytes.size(), url.c_str());
      return false;
    }
    std::string data(bytes.begin(), bytes.end());
    if (!writeAtomic(dir + "/cover.jpg", data)) {
      CSPOT_LOG(error, "cover: cannot write %s/cover.jpg: %s", dir.c_str(), strerror(errno));
      return false;
    }
    outName = "cover.jpg";
    IOS3_TRACE("cover saved (%d bytes) from %s", (int)bytes.size(), url.c_str());
    return true;
  } catch (const std::exception& e) {
    CSPOT_LOG(error, "cover: fetch failed: %s", e.what());
    return false;
  }
}

void StatusWriter::writeFiles() {
  std::string st, ti, ar, al, cv, dv, apS, co, tls;
  uint32_t dur, pos;
  std::function<std::string()> cp;
  {
    std::scoped_lock l(mutex);
    st = state; ti = title; ar = artist; al = album; cv = coverFile; dv = device; apS = ap; co = country;
    dur = durationMs; pos = currentPositionMs();
    cp = countryProvider;
  }
  if (co.empty() && cp) {
    co = cp();
    if (!co.empty()) {
      std::scoped_lock l(mutex);
      country = co;
    }
  }
  tls = bell::TLSSocket::lastNegotiated();
  unsigned long rss = ios3_rss_kb();
  time_t now = time(nullptr);

  char num[64];
  std::string j = "{\n";
  j += "  \"source\": \"spotify\",\n";
  j += "  \"state\": \"" + jsonEscape(st) + "\",\n";
  j += "  \"title\": \"" + jsonEscape(ti) + "\", \"artist\": \"" + jsonEscape(ar) + "\", \"album\": \"" + jsonEscape(al) + "\",\n";
  snprintf(num, sizeof num, "%u, \"position_ms\": %u,\n", (unsigned)dur, (unsigned)pos);
  j += std::string("  \"duration_ms\": ") + num;
  snprintf(num, sizeof num, "%d", bitrate);
  j += std::string("  \"bitrate_kbps\": ") + num + ", \"codec\": \"OGG/Vorbis\",\n";
  j += "  \"cover\": \"" + jsonEscape(cv) + "\",\n";
  j += "  \"device\": \"" + jsonEscape(dv) + "\",\n";
  j += "  \"ap\": \"" + jsonEscape(apS) + "\", \"country\": \"" + jsonEscape(co) + "\",\n";
  j += "  \"tls\": \"" + jsonEscape(tls) + "\",\n";
  snprintf(num, sizeof num, "%lu", rss);
  j += std::string("  \"rss_kb\": ") + num + ",\n";
  snprintf(num, sizeof num, "%lld", (long long)now);
  j += std::string("  \"updated\": ") + num + "\n}\n";

  std::string p = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                  "<plist version=\"1.0\">\n<dict>\n";
  auto ps = [&](const char* k, const std::string& v) { p += std::string("\t<key>") + k + "</key><string>" + xmlEscape(v) + "</string>\n"; };
  auto pi = [&](const char* k, long long v) { snprintf(num, sizeof num, "%lld", v); p += std::string("\t<key>") + k + "</key><integer>" + num + "</integer>\n"; };
  ps("source", "spotify"); ps("state", st); ps("title", ti); ps("artist", ar); ps("album", al);
  pi("duration_ms", dur); pi("position_ms", pos);
  pi("bitrate_kbps", bitrate); ps("codec", "OGG/Vorbis");
  ps("cover", cv); ps("device", dv); ps("ap", apS); ps("country", co); ps("tls", tls);
  pi("rss_kb", (long long)rss); pi("updated", (long long)now);
  p += "</dict>\n</plist>\n";

  if (!writeAtomic(dir + "/status.json", j) || !writeAtomic(dir + "/status.plist", p)) {
    static bool warned = false;
    if (!warned) {
      CSPOT_LOG(error, "StatusWriter: cannot write to %s: %s", dir.c_str(), strerror(errno));
      warned = true;
    }
  }
}

void StatusWriter::threadLoop() {
  while (true) {
    // Wait for a change or 1 s, whichever first.
    pthread_mutex_lock(&wakeMutex);
    if (!dirty && running) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      struct timespec ts;
      ts.tv_sec = tv.tv_sec + 1;
      ts.tv_nsec = tv.tv_usec * 1000;
      pthread_cond_timedwait(&wake, &wakeMutex, &ts);
    }
    bool stop = !running;
    dirty = false;
    pthread_mutex_unlock(&wakeMutex);
    if (stop) break;

    // Cover art: fetch when the track's image URL changed.
    std::string url;
    {
      std::scoped_lock l(mutex);
      if (imageUrl != coverFetchedUrl) {
        url = imageUrl;
        coverFetchedUrl = imageUrl;
        if (url.empty()) coverFile.clear();
      }
    }
    if (!url.empty()) {
      std::string name;
      bool ok = fetchCover(url, name);
      std::scoped_lock l(mutex);
      coverFile = ok ? name : "";
    }

    writeFiles();
  }
  writeFiles();
}
