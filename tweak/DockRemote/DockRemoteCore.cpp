#include "DockRemoteCore.h"
#include "DockRemoteKeys.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include "audiod/proto.h"

namespace dockremote {

static const char* kDisable    = "/var/mobile/fidelio/remote-tweak.disabled";
static const char* kConf       = "/var/mobile/fidelio/settings.conf";
static const char* kActive     = "/var/mobile/fidelio/active-source";
static const char* kSock       = "/var/mobile/fidelio/audiod.sock";

static bool exists(const char* p) { struct stat st; return stat(p, &st) == 0; }

// Is audiod.dock_remote=on in settings.conf? Default off. Tiny, line-based, no allocation.
static bool dockRemoteEnabled() {
  FILE* f = fopen(kConf, "r");
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

// Does a Fidelio source currently own audiod? (active-source file holds "spotify"/"airplay", not "none".)
static bool sourceActive() {
  FILE* f = fopen(kActive, "r");
  if (!f) return false;
  char s[32] = {0};
  size_t n = fread(s, 1, sizeof s - 1, f);
  fclose(f);
  while (n && (s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\r')) s[--n] = 0;
  return n > 0 && strcmp(s, "none") != 0;
}

// Non-blocking reachability: a connect() to the socket with a short timeout.
static bool audiodReachable() {
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return false;
  struct timeval to = { 0, 150 * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  strncpy(a.sun_path, kSock, sizeof a.sun_path - 1);
  bool ok = connect(s, (sockaddr*)&a, sizeof a) == 0;
  close(s);
  return ok;
}

bool shouldForward(uint32_t code) {
  if (exists(kDisable)) return false;      // global kill switch, no rebuild needed
  if (!verbForCode(code)) return false;    // not a key we take over
  if (!dockRemoteEnabled()) return false;  // the user setting is off
  if (!sourceActive()) return false;       // nothing Fidelio is playing -> let the iPod app have it
  if (!audiodReachable()) return false;    // audiod not up -> fall through
  return true;
}

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
