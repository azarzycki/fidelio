#include "DockRemoteKeys.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "audiod/proto.h"

namespace dockremote {

static uint32_t rd32(const unsigned char* p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }

KeyEvent decodeKey(const void* record) {
  KeyEvent e = { false, false, 0 };
  if (!record) return e;
  const unsigned char* p = (const unsigned char*)record;
  if (rd32(p, GS_OFF_TYPE) != GS_TYPE_ACCESSORY_KEY || rd32(p, GS_OFF_INFOSIZE) != GS_KEY_INFO_SIZE) return e;
  uint32_t flags = rd32(p, GS_OFF_FLAGS);
  if (flags != KEY_FLAG_DOWN && flags != KEY_FLAG_UP) return e;
  if (rd32(p, GS_OFF_INFO0) != 0) return e;  // 0 in every measured event: anything else is an unknown shape
  e.valid = true;
  e.down = flags == KEY_FLAG_DOWN;
  e.code = rd32(p, GS_OFF_CODE);
  return e;
}

uint8_t verbForCode(uint32_t code) {
  switch (code) {
    case CODE_PLAYPAUSE:     return AUDIOD_CTRL_TOGGLE;
    case CODE_NEXT:
    case CODE_NEXT_IAPD:     return AUDIOD_CTRL_NEXT;
    case CODE_PREVIOUS:
    case CODE_PREVIOUS_IAPD: return AUDIOD_CTRL_PREV;
    default:                 return 0;  // menu/select/up/down, unknown: always pass
  }
}

const char* keyName(uint32_t code, char* buf, size_t cap) {
  switch (code) {
    case CODE_PLAYPAUSE:     return "play-pause";
    case CODE_NEXT:
    case CODE_NEXT_IAPD:     return "next";
    case CODE_PREVIOUS:
    case CODE_PREVIOUS_IAPD: return "previous";
    case CODE_MENU:          return "menu";
    case CODE_SELECT:        return "select";
    case CODE_UP:            return "up";
    case CODE_DOWN:          return "down";
    default:
      if (!buf || !cap) return "code";
      snprintf(buf, cap, "code-%u", (unsigned)code);
      return buf;
  }
}

Decision PressTracker::onKey(const KeyEvent& e, MayTakeFn mayTake, void* ctx) {
  Decision d = { false, 0, false, false };
  if (!e.valid) return d;
  uint8_t verb = verbForCode(e.code);
  if (e.down) {
    // The first event of a two-event press: decide for the whole press.
    bool take = verb && mayTake && mayTake(e.code, ctx);
    pending_ = true; pendingCode_ = e.code; pendingTaken_ = take;
    d.swallow = take; d.verb = take ? verb : 0; d.taken = take;
    return d;
  }
  // 0x800: the press is complete. Follow its down event's decision; a press without one (next 20/3,
  // previous 21/4, or a down that was missed) is decided here. A 0x800 of another code leaves a pending
  // press alone.
  bool take;
  if (pending_ && pendingCode_ == e.code) {
    take = pendingTaken_;
    pending_ = false; pendingTaken_ = false;
  } else {
    take = verb && mayTake && mayTake(e.code, ctx);
    d.verb = take ? verb : 0;
  }
  d.swallow = take; d.taken = take; d.logLine = true;
  return d;
}

int formatKeyLine(char* out, size_t cap, long long unixSeconds, uint32_t code, bool taken) {
  char nb[24];
  return snprintf(out, cap, "%lld %u %s %s", unixSeconds, (unsigned)code, keyName(code, nb, sizeof nb), taken ? "taken" : "passed");
}

bool appendKeyLine(const char* path, const char* line, size_t capBytes) {
  if (!path || !line) return false;
  struct stat st;
  int flags = O_WRONLY | O_CREAT | O_APPEND;
  if (stat(path, &st) == 0 && (unsigned long long)st.st_size > (unsigned long long)capBytes) flags |= O_TRUNC;
  int fd = open(path, flags, 0644);
  if (fd < 0) return false;
  char buf[160];
  size_t n = strlen(line);
  if (n > sizeof buf - 2) n = sizeof buf - 2;
  memcpy(buf, line, n);
  buf[n++] = '\n';
  bool ok = write(fd, buf, n) == (ssize_t)n;  // one write: a reader never sees half a line for long
  close(fd);
  return ok;
}

}  // namespace dockremote
