// B1.16.16 host test of the DockRemote take-or-pass condition (tweak/DockRemote/DockRemoteCore.cpp, the
// same C++ the tweak runs inside iapd), with its files in a temporary directory: remote-target present
// (a source / "none"), absent or stale (the fall-back to active-source), the socket reachable or not, the
// kill switch and the setting; and the press sequence of the 2026-11-22 trace (play/pause to pause, the
// source released, play/pause to resume) through the PressTracker.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "audiod/proto.h"
#include "tweak/DockRemote/DockRemoteCore.h"
#include "tweak/DockRemote/DockRemoteKeys.h"

using namespace dockremote;

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static std::string dir;
static std::string P(const char* name) { return dir + "/" + name; }
static void put(const char* name, const char* content) {
  std::string tmp = P(name) + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) { perror(tmp.c_str()); exit(2); }
  fputs(content, f);
  fclose(f);
  rename(tmp.c_str(), P(name).c_str());
}
static void del(const char* name) { unlink(P(name).c_str()); }
static void age(const char* name, long seconds) {  // set the file's mtime `seconds` into the past
  struct timeval tv[2];
  gettimeofday(&tv[0], nullptr);
  tv[0].tv_sec -= seconds;
  tv[1] = tv[0];
  utimes(P(name).c_str(), tv);
}

static int listener = -1;
static void listenOn() {  // a socket that accepts connections (the backlog is enough: never accept())
  listener = socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  strncpy(a.sun_path, P("a.sock").c_str(), sizeof a.sun_path - 1);
  unlink(a.sun_path);
  if (bind(listener, (sockaddr*)&a, sizeof a) != 0 || listen(listener, 64) != 0) { perror("listen"); exit(2); }
}
static void stopListening() { close(listener); listener = -1; }  // the socket file stays, as after a crash

// The hook's steps for one record (DockRemoteHook.mm), with the real condition.
struct Hook {
  PressTracker t;
  Paths paths;
  std::vector<int> verbs;
  int passed = 0, dropped = 0;
  static bool mayTake(uint32_t code, void* ctx) { return shouldForwardAt(((Hook*)ctx)->paths, code); }
  void send(uint32_t flags, uint32_t code) {
    unsigned char r[60]; memset(r, 0, sizeof r);
    uint32_t type = 4001, info = 8;
    memcpy(r, &type, 4); memcpy(r + 40, &flags, 4); memcpy(r + 48, &info, 4); memcpy(r + 56, &code, 4);
    KeyEvent k = decodeKey(r);
    if (k.valid) {
      Decision d = t.onKey(k, mayTake, this);
      if (d.verb) verbs.push_back(d.verb);
      if (d.swallow) { dropped++; return; }
    }
    passed++;
  }
  void press(uint32_t code) { send(0x400, code); send(0x800, code); }
};

int main() {
  char tmpl[] = "/tmp/audiod-dockremote-core.XXXXXX";
  if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
  dir = tmpl;
  std::string sDisable = P("remote-tweak.disabled"), sConf = P("settings.conf"), sActive = P("active-source"),
              sTarget = P("remote-target"), sSock = P("a.sock");
  Paths p = { sDisable.c_str(), sConf.c_str(), sActive.c_str(), sTarget.c_str(), sSock.c_str() };
  put("settings.conf", "audiod.dock_remote=on\n");
  listenOn();

  // ---- remote-target present ----
  put("active-source", "none\n");
  put("remote-target", "spotify\n");
  CHECK(remoteTargetState(p) == 1, "remote-target 'spotify': state 1");
  CHECK(shouldForwardAt(p, 0) && shouldForwardAt(p, 20) && shouldForwardAt(p, 3) && shouldForwardAt(p, 21) && shouldForwardAt(p, 4),
        "target 'spotify', active-source 'none' (paused): play/pause, next (20, 3) and previous (21, 4) are taken");
  CHECK(!shouldForwardAt(p, 22) && !shouldForwardAt(p, 23) && !shouldForwardAt(p, 24) && !shouldForwardAt(p, 25),
        "target 'spotify': menu, select, up and down still pass");
  put("remote-target", "airplay\n");
  CHECK(shouldForwardAt(p, 0), "target 'airplay': play/pause taken");
  put("active-source", "spotify\n");
  put("remote-target", "none\n");
  CHECK(remoteTargetState(p) == 0 && !shouldForwardAt(p, 0), "target 'none' wins over active-source 'spotify': passed");
  put("remote-target", "");
  CHECK(remoteTargetState(p) == 0 && !shouldForwardAt(p, 0), "an empty remote-target counts as 'none': passed");

  // ---- remote-target absent: active-source decides, as before B1.16.16 ----
  del("remote-target");
  CHECK(remoteTargetState(p) == -1, "remote-target absent: state -1");
  put("active-source", "spotify\n");
  CHECK(shouldForwardAt(p, 0), "no remote-target (older audiod), active-source 'spotify': taken");
  put("active-source", "none\n");
  CHECK(!shouldForwardAt(p, 0), "no remote-target, active-source 'none': passed (the behaviour of B1.16.15)");
  del("active-source");
  CHECK(!shouldForwardAt(p, 0), "neither file: passed");

  // ---- a stale remote-target (an older audiod started after a newer one) is ignored ----
  put("remote-target", "spotify\n");
  age("remote-target", 31);
  put("active-source", "none\n");
  CHECK(remoteTargetState(p) == -1 && !shouldForwardAt(p, 0), "remote-target 'spotify' 31 s old, active-source 'none': ignored, passed");
  put("active-source", "airplay\n");
  CHECK(shouldForwardAt(p, 0), "remote-target 31 s old, active-source 'airplay': active-source decides, taken");
  put("remote-target", "spotify\n");
  age("remote-target", 25);
  put("active-source", "none\n");
  CHECK(remoteTargetState(p) == 1 && shouldForwardAt(p, 0), "remote-target 25 s old (within the 10 s rewrite's margin): still used, taken");

  // ---- the other conditions still gate it ----
  put("remote-target", "spotify\n");
  put("remote-tweak.disabled", "");
  CHECK(!shouldForwardAt(p, 0), "kill switch: passed although the target is 'spotify'");
  del("remote-tweak.disabled");
  put("settings.conf", "audiod.dock_remote=off\n");
  CHECK(!shouldForwardAt(p, 0), "dock_remote=off: passed");
  put("settings.conf", "audiod.dock_remote=on\n");
  CHECK(shouldForwardAt(p, 0), "back on: taken");
  stopListening();
  CHECK(!shouldForwardAt(p, 0), "audiod not running (socket file left, nobody listening): a leftover 'spotify' is not trusted, passed");
  unlink(sSock.c_str());
  CHECK(!shouldForwardAt(p, 0), "no socket file: passed");
  del("remote-target");
  put("active-source", "spotify\n");
  CHECK(!shouldForwardAt(p, 0), "no socket, old files only (active-source 'spotify'): passed");
  listenOn();

  // ---- the press sequence of the 2026-11-22 trace ----
  // Before B1.16.16: only active-source; after the pause audiod released Spotify and wrote "none", so the
  // second play/pause passed to the iPod app. With remote-target, both presses are taken.
  {
    Hook h; h.paths = p;
    del("remote-target");  // the device files of B1.16.15's time
    put("active-source", "spotify\n");
    h.press(0);              // pause
    put("active-source", "none\n");  // audiod: release 'spotify' -> 'none' (quiet 1516 ms)
    h.press(0);              // resume
    CHECK(h.verbs.size() == 1 && h.dropped == 2 && h.passed == 2,
          "without remote-target (as before): the pause is taken, the resume PASSES to the iPod app (%zu verbs, %d dropped, %d passed)",
          h.verbs.size(), h.dropped, h.passed);
  }
  {
    Hook h; h.paths = p;
    put("active-source", "spotify\n"); put("remote-target", "spotify\n");
    h.press(0);              // pause: TOGGLE
    put("active-source", "none\n");    // released; audiod keeps the paused source as the target
    h.press(0);              // resume: TOGGLE (audiod sends it as PLAY)
    h.send(0x800, 3);        // next, while paused
    CHECK(h.verbs.size() == 3 && h.verbs[0] == AUDIOD_CTRL_TOGGLE && h.verbs[1] == AUDIOD_CTRL_TOGGLE && h.verbs[2] == AUDIOD_CTRL_NEXT &&
          h.dropped == 5 && h.passed == 0,
          "with remote-target: pause and resume both taken (TOGGLE, TOGGLE), next while paused taken (%zu verbs, %d dropped, %d passed)",
          h.verbs.size(), h.dropped, h.passed);
    put("remote-target", "none\n");    // audiod: other audio is playing, or the source left
    h.press(0);
    CHECK(h.verbs.size() == 3 && h.passed == 2, "target cleared: the next play/pause passes to the system (%d passed)", h.passed);
  }

  stopListening();
  for (const char* n : { "remote-tweak.disabled", "settings.conf", "active-source", "remote-target", "a.sock" }) del(n);
  rmdir(dir.c_str());
  printf("\n%s\n", fails ? "FAILURES" : "all dockremote core checks passed");
  return fails ? 1 : 0;
}
