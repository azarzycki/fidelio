#include "DockRemoteCore.h"
#include "DockRemoteKeys.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "audiod/proto.h"

namespace dockremote {

static const char* kSock       = "/var/mobile/fidelio/audiod.sock";

// Literals only: constant-initialised, so no guard variable (a function-local static would link
// libc++abi's guard and exception code into the dylib).
static const Paths kDevicePaths = {
  "/var/mobile/fidelio/remote-tweak.disabled",
  "/var/mobile/fidelio/settings.conf",
  "/var/mobile/fidelio/active-source",
  "/var/mobile/fidelio/remote-target",  // B1.16.16
  "/var/mobile/fidelio/audiod.sock",
};

const Paths& devicePaths() { return kDevicePaths; }

static bool exists(const char* p) { struct stat st; return stat(p, &st) == 0; }

// Is audiod.dock_remote=on in settings.conf? Default off. Tiny, line-based, no allocation.
static bool dockRemoteEnabled(const char* conf) {
  FILE* f = fopen(conf, "r");
  if (!f) return false;
  char line[256];
  bool on = false;
  while (fgets(line, sizeof line, f)) {
    // trim leading spaces
    char* p = line; while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "audiod.dock_remote", 18) != 0) continue;
    char* eq = strchr(p, '=');
    if (!eq) continue;
    eq++; while (*eq == ' ' || *eq == '\t') eq++;
    on = (strncmp(eq, "on", 2) == 0 || strncmp(eq, "true", 4) == 0 || *eq == '1');
  }
  fclose(f);
  return on;
}

// Does the file name a source of ours ("spotify"/"airplay"), not "none"? One small read, no allocation.
static bool namesASource(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char s[32] = {0};
  size_t n = fread(s, 1, sizeof s - 1, f);
  fclose(f);
  while (n && (s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\r')) s[--n] = 0;
  return n > 0 && strcmp(s, "none") != 0;
}

// B1.16.16: audiod (B1.16.16 and later) rewrites remote-target at once on every change and every 10 s. A
// file older than this was left by an audiod that no longer writes it (an older audiod started after a
// newer one): it is ignored, and active-source decides as before B1.16.16.
static const long kTargetStaleS = 30;

int remoteTargetState(const Paths& p) {
  struct stat st;
  if (stat(p.target, &st) != 0) return -1;  // absent: an audiod before B1.16.16
  long age = (long)(time(0) - st.st_mtime);
  if (age > kTargetStaleS) return -1;       // not maintained: treated as absent
  return namesASource(p.target) ? 1 : 0;
}

// Non-blocking reachability: a connect() to the socket with a short timeout.
static bool audiodReachable(const char* sock) {
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return false;
  struct timeval to = { 0, 150 * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  strncpy(a.sun_path, sock, sizeof a.sun_path - 1);
  bool ok = connect(s, (sockaddr*)&a, sizeof a) == 0;
  close(s);
  return ok;
}

bool shouldForwardAt(const Paths& p, uint32_t code) {
  if (exists(p.disable)) return false;           // global kill switch, no rebuild needed
  if (!verbForCode(code)) return false;          // not a key we take over
  if (!dockRemoteEnabled(p.conf)) return false;  // the user setting is off
  // B1.16.16: the remote's target (a source of ours, playing or paused and resumable) when audiod
  // writes remote-target; otherwise, as before, a source of ours owning audiod (active-source).
  int t = remoteTargetState(p);
  if (t == 0) return false;                      // no target -> let the iPod app have it
  if (t < 0 && !namesASource(p.active)) return false;
  if (!audiodReachable(p.sock)) return false;    // audiod not up (a leftover file proves nothing) -> fall through
  return true;
}

bool shouldForward(uint32_t code) { return shouldForwardAt(devicePaths(), code); }

bool forward(uint8_t verb) {
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return false;
  struct timeval to = { 0, 200 * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
  sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  strncpy(a.sun_path, kSock, sizeof a.sun_path - 1);
  if (connect(s, (sockaddr*)&a, sizeof a) != 0) { close(s); return false; }
  AudiodHello h; memset(&h, 0, sizeof h);
  h.version = AUDIOD_PROTO_VERSION_2; h.channels = AUDIOD_CHANNELS; h.bits = AUDIOD_BITS; h.sampleRate = AUDIOD_SAMPLE_RATE;
  strncpy(h.name, "control", sizeof h.name - 1);
  AudiodMsgHeader hh = { AUDIOD_MSG_HELLO, (uint32_t)sizeof h };
  AudiodMsgHeader ch = { AUDIOD_MSG_CONTROL, (uint32_t)sizeof(AudiodControl) };
  AudiodControl c; c.verb = verb;
  bool ok = write(s, &hh, sizeof hh) == (ssize_t)sizeof hh && write(s, &h, sizeof h) == (ssize_t)sizeof h
         && write(s, &ch, sizeof ch) == (ssize_t)sizeof ch && write(s, &c, sizeof c) == (ssize_t)sizeof c;
  if (ok) { char buf[8]; recv(s, buf, sizeof buf, 0); }  // let audiod read before we close
  close(s);
  syslog(LOG_NOTICE, "DockRemote: forward verb %u to audiod -> %s", verb, ok ? "sent" : "failed");
  return ok;
}

}  // namespace dockremote
