// B1.16.16 host test of the Now Playing tab's source pick (nowplaying/src/NPSource.c, the same C the app
// compiles): playing, a paused remote target, the target cleared, a switch to another source, an audiod
// before B1.16.16 (no remote-target) and no audiod at all. B1.17: the "daemons not running" state.
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

  // B1.19: keep the dock display awake only while a source actually owns the output (active-source names
  // it). A paused remote target, an idle audiod and a stale/absent/garbage active-source all release the
  // assertion so the screen may auto-lock. NPSourceKeepAwake(active, target); the target never keeps it awake.
  CHECK(NPSourceKeepAwake("spotify", "spotify") == 1, "playing via spotify: keep the screen awake");
  CHECK(NPSourceKeepAwake("airplay", "airplay") == 1, "playing via airplay: keep the screen awake");
  CHECK(NPSourceKeepAwake("airplay", "spotify") == 1, "an active source keeps the screen awake whatever the target");
  CHECK(NPSourceKeepAwake("spotify", NULL) == 1, "spotify active, no remote-target (older audiod): keep awake");
  CHECK(NPSourceKeepAwake("none", "spotify") == 0, "paused remote target: no source owns the output, let the screen sleep");
  CHECK(NPSourceKeepAwake("none", "airplay") == 0, "paused airplay target: let the screen sleep");
  CHECK(NPSourceKeepAwake("none", "none") == 0, "idle (both none): let the screen sleep");
  CHECK(NPSourceKeepAwake("none", NULL) == 0, "idle, older audiod (no remote-target): let the screen sleep");
  CHECK(NPSourceKeepAwake("", "spotify") == 0, "stale/absent active-source but a fresh target: still paused, let the screen sleep");
  CHECK(NPSourceKeepAwake("", NULL) == 0, "no audiod (active-source stale/absent): let the screen sleep");

  // B1.17: the "daemons not running" state (data directory missing, empty, or only old files).
  CHECK(NPIsDirectory("/tmp") && !NPIsDirectory("/nonexistent-np-source-test") && !NPIsDirectory("/bin/sh") && !NPIsDirectory(NULL),
        "the directory check: a directory yes; a missing path, a plain file and NULL no");
  CHECK(NPDaemonsState(0, 0, -1, 30) == NP_DAEMONS_NO_DIR, "no data directory: daemons not running (no directory)");
  CHECK(NPDaemonsState(0, 1, 5, 30) == NP_DAEMONS_NO_DIR, "no data directory wins over anything else");
  CHECK(NPDaemonsState(1, 0, -1, 30) == NP_DAEMONS_NOT_RUNNING, "an empty data directory: daemons not running");
  CHECK(NPDaemonsState(1, 0, 600, 30) == NP_DAEMONS_NOT_RUNNING, "only leftovers (status file 10 min old, no fresh active-source): not running");
  CHECK(NPDaemonsState(1, 1, -1, 30) == NP_DAEMONS_RUNNING, "audiod's active-source fresh, no player status yet: running");
  CHECK(NPDaemonsState(1, 0, 5, 30) == NP_DAEMONS_RUNNING, "a player's fresh status file without audiod: running (shown as before)");
  CHECK(NPDaemonsState(1, 0, 30, 30) == NP_DAEMONS_RUNNING && NPDaemonsState(1, 0, 31, 30) == NP_DAEMONS_NOT_RUNNING,
        "the status file counts up to the stale limit, not past it");

  printf("\n%s\n", fails ? "FAILURES" : "all source pick checks passed");
  return fails ? 1 : 0;
}
