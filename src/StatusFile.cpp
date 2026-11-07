#include "StatusFile.h"

#include <errno.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ios3_trace.h"

static std::string jsonEscape(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
  }
  return o;
}

static std::string xmlEscape(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      default:
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        o += (char)c;
    }
  }
  return o;
}

static bool writeAtomic(const std::string& path, const void* data, size_t len) {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  ok = (fclose(f) == 0) && ok;
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
  return true;
}

uint64_t StatusFile::nowMs() {
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

StatusFile::StatusFile(const std::string& d, const std::string& dev) : dir(d), device(dev), enabled(!d.empty()) {
  pthread_mutex_init(&wakeMutex, nullptr);
  pthread_cond_init(&cond, nullptr);
  if (!enabled) return;
  mkdir(dir.c_str(), 0755);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 * 1024);
  if (pthread_create(&thread, &attr, threadEntry, this) != 0) enabled = false;
  pthread_attr_destroy(&attr);
}

StatusFile::~StatusFile() {
  if (enabled) {
    pthread_mutex_lock(&wakeMutex);
    running = false;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&wakeMutex);
    pthread_join(thread, nullptr);
  }
  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&wakeMutex);
}

void* StatusFile::threadEntry(void* self) { static_cast<StatusFile*>(self)->loop(); return nullptr; }

void StatusFile::wake() {
  pthread_mutex_lock(&wakeMutex);
  dirty = true;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&wakeMutex);
}

uint32_t StatusFile::positionNow() {
  uint32_t p = baseMs;
  if (clockRunning) p += (uint32_t)(nowMs() - baseTime);
  if (durationMs && p > durationMs) p = durationMs;
  return p;
}

void StatusFile::setState(const std::string& s) {
  { std::scoped_lock l(mutex); state = s; if (s != "playing" && s != "paused") clockRunning = false; }
  wake();
}
void StatusFile::setTrack(const std::string& t, const std::string& ar, const std::string& al) {
  { std::scoped_lock l(mutex); title = t; artist = ar; album = al; }
  wake();
}
void StatusFile::setProgress(uint32_t pos, uint32_t dur) {
  { std::scoped_lock l(mutex); baseMs = pos; durationMs = dur; baseTime = nowMs(); }
  wake();
}
void StatusFile::setPlaying(bool playing) {
  {
    std::scoped_lock l(mutex);
    uint32_t p = positionNow();
    baseMs = p; baseTime = nowMs(); clockRunning = playing;
  }
  wake();
}
void StatusFile::setCodec(const std::string& c) { { std::scoped_lock l(mutex); codec = c; } wake(); }
void StatusFile::setPeer(const std::string& p) { { std::scoped_lock l(mutex); peer = p; } wake(); }
void StatusFile::clearTrack() {
  { std::scoped_lock l(mutex); title.clear(); artist.clear(); album.clear(); cover.clear(); durationMs = 0; baseMs = 0; clockRunning = false; }
  wake();
}
void StatusFile::poke() { wake(); }

void StatusFile::setCover(const std::vector<uint8_t>& jpeg) {
  if (!enabled) return;
  bool ok = writeAtomic(dir + "/cover.jpg", jpeg.data(), jpeg.size());
  { std::scoped_lock l(mutex); cover = ok ? "cover.jpg" : ""; }
  if (!ok) IOS3_TRACE("cover.jpg write failed: %s", strerror(errno));
  wake();
}

void StatusFile::writeFiles() {
  std::string st, ti, ar, al, cv, dv, co, pe;
  uint32_t dur, pos;
  {
    std::scoped_lock l(mutex);
    st = state; ti = title; ar = artist; al = album; cv = cover; dv = device; co = codec; pe = peer;
    dur = durationMs; pos = positionNow();
  }
  unsigned long rss = ios3_rss_kb();
  long long now = (long long)time(nullptr);
  char num[64];

  std::string j = "{\n  \"source\": \"airplay\",\n";
  j += "  \"state\": \"" + jsonEscape(st) + "\",\n";
  j += "  \"title\": \"" + jsonEscape(ti) + "\", \"artist\": \"" + jsonEscape(ar) + "\", \"album\": \"" + jsonEscape(al) + "\",\n";
  snprintf(num, sizeof num, "  \"duration_ms\": %u, \"position_ms\": %u,\n", (unsigned)dur, (unsigned)pos); j += num;
  j += "  \"bitrate_kbps\": 0, \"codec\": \"" + jsonEscape(co) + "\",\n";
  j += "  \"cover\": \"" + jsonEscape(cv) + "\",\n";
  j += "  \"device\": \"" + jsonEscape(dv) + "\",\n";
  j += "  \"ap\": \"" + jsonEscape(pe) + "\", \"country\": \"\",\n";
  j += "  \"tls\": \"\",\n";
  snprintf(num, sizeof num, "  \"rss_kb\": %lu,\n  \"updated\": %lld\n}\n", rss, now); j += num;

  std::string p = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                  "<plist version=\"1.0\">\n<dict>\n";
  auto ps = [&](const char* k, const std::string& v) { p += std::string("\t<key>") + k + "</key><string>" + xmlEscape(v) + "</string>\n"; };
  auto pi = [&](const char* k, long long v) { snprintf(num, sizeof num, "%lld", v); p += std::string("\t<key>") + k + "</key><integer>" + num + "</integer>\n"; };
  ps("source", "airplay"); ps("state", st); ps("title", ti); ps("artist", ar); ps("album", al);
  pi("duration_ms", dur); pi("position_ms", pos); pi("bitrate_kbps", 0); ps("codec", co);
  ps("cover", cv); ps("device", dv); ps("ap", pe); ps("country", ""); ps("tls", "");
  pi("rss_kb", (long long)rss); pi("updated", now);
  p += "</dict>\n</plist>\n";

  if (!writeAtomic(dir + "/status.json", j.data(), j.size()) || !writeAtomic(dir + "/status.plist", p.data(), p.size())) {
    static bool warned = false;
    if (!warned) { IOS3_TRACE("status: cannot write to %s: %s", dir.c_str(), strerror(errno)); warned = true; }
  }
}

void StatusFile::loop() {
  while (true) {
    pthread_mutex_lock(&wakeMutex);
    if (!dirty && running) {
      struct timeval tv; gettimeofday(&tv, nullptr);
      struct timespec ts; ts.tv_sec = tv.tv_sec + 1; ts.tv_nsec = tv.tv_usec * 1000;
      pthread_cond_timedwait(&cond, &wakeMutex, &ts);
    }
    bool stop = !running;
    dirty = false;
    pthread_mutex_unlock(&wakeMutex);
    if (stop) break;
    bool playing;
    { std::scoped_lock l(mutex); playing = clockRunning; }
    (void)playing;  // once a second regardless: the file age is the app's liveness signal
    writeFiles();
  }
  writeFiles();
}
