// B1.16.16 host test of the Now Playing tab's source pick (nowplaying/src/NPSource.c, the same C the app
// compiles): playing, a paused remote target, the target cleared, a switch to another source, an audiod
// before B1.16.16 (no remote-target) and no audiod at all.
#include <stdio.h>
#include <string.h>

#include "NPSource.h"

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static NPSourcePick pick(const char* active, const char* target) {
  NPSourcePick p;
  NPPickSource(active, target, &p);
  return p;
}

int main(void) {
  char n[NP_SOURCE_NAME_MAX];
  NPSourceName("spotify\n", 8, n, sizeof n);
  CHECK(!strcmp(n, "spotify"), "a file's name: the newline dropped");
  NPSourceName("  airplay \r\n", 12, n, sizeof n);
  CHECK(!strcmp(n, "airplay"), "surrounding whitespace dropped");
  NPSourceName(NULL, 0, n, sizeof n);
  CHECK(!strcmp(n, ""), "no file: empty");
  NPSourceName("abcdefghijklmnopqrstuvwxyz0123456789", 36, n, sizeof n);
  CHECK(strlen(n) == NP_SOURCE_NAME_MAX - 1, "a long content is cut, never overrun");

  NPSourcePick p = pick("spotify", "spotify");
  CHECK(!strcmp(p.shown, "spotify") && !p.paused && !p.idle, "playing: spotify shown, not paused, not idle");
  p = pick("none", "spotify");
  CHECK(!strcmp(p.shown, "spotify") && p.paused && !p.idle, "paused remote target: spotify stays shown, paused, not idle");
  p = pick("none", "none");
  CHECK(!strcmp(p.shown, "") && !p.paused && p.idle, "target cleared (disconnect, STOP, other audio): idle");
  p = pick("airplay", "airplay");
  CHECK(!strcmp(p.shown, "airplay") && !p.paused, "another source takes over: airplay shown");
  p = pick("none", "airplay");
  CHECK(!strcmp(p.shown, "airplay") && p.paused, "that one paused: airplay kept, paused");
  p = pick("airplay", "spotify");
  CHECK(!strcmp(p.shown, "airplay") && !p.paused, "the active source always wins over a target (never seen from one audiod: defensive)");
  p = pick("none", NULL);
  CHECK(p.idle && !p.paused && !strcmp(p.shown, ""), "an audiod before B1.16.16 (no remote-target), none active: idle as in 1.4.2");
  p = pick("spotify", NULL);
  CHECK(!strcmp(p.shown, "spotify") && !p.paused, "no remote-target, spotify active: spotify as in 1.4.2");
  p = pick("", NULL);
  CHECK(!p.idle && !p.paused && !strcmp(p.shown, ""), "no audiod (both files absent or stale): not idle, the caller picks by mtime");
  p = pick("", "spotify");
  CHECK(!strcmp(p.shown, "spotify") && p.paused, "active-source unreadable but a fresh target: the target shown, paused");
  p = pick("none", "");
  CHECK(p.idle && !p.paused, "an empty remote-target counts as none");

  printf("\n%s\n", fails ? "FAILURES" : "all source pick checks passed");
  return fails ? 1 : 0;
}
