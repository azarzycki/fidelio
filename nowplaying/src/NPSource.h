// B1.16.16: which source the Now Playing tab shows (plain C, also host-tested by
// nowplaying/hosttest/source_test.c). audiod writes two one-line files in /var/mobile/dockreceiver, each
// replaced atomically at once on a change and rewritten every 10 s:
//   active-source   the source that owns the output now ("spotify" / "airplay"), or "none"
//   remote-target   (audiod B1.16.16 and later) the dock remote's target: the active source, or the paused
//                   last one until it disconnects, stops, another source of ours plays, or other audio
//                   (the Music app) plays; "none" otherwise. The DockRemote tweak reads the same file.
// The app reads them once a second (NPStatus refresh) and treats a file older than 30 s as absent.
#ifndef NP_SOURCE_H
#define NP_SOURCE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_SOURCE_NAME_MAX 32

typedef struct {
  char shown[NP_SOURCE_NAME_MAX];  // the source to show, "" when the caller picks by status-file mtime
  int paused;                      // 1: shown is the remote target and nothing plays: keep its track up, "Paused"
  int idle;                        // 1: audiod runs and has neither an active source nor a target: the idle view
} NPSourcePick;

// Copy a file's content as a name: leading and trailing whitespace dropped, at most cap-1 bytes.
// text may be NULL (no file): out becomes "".
void NPSourceName(const char* text, size_t len, char* out, size_t cap);

// active: active-source's name, "" when the file is absent or stale (no audiod).
// target: remote-target's name, or NULL when that file is absent or stale (an audiod before B1.16.16).
//   - active names a source: show it (playing).
//   - otherwise target names a source: show it, paused.
//   - otherwise active is "none": idle.
//   - otherwise (no audiod): not idle, shown "" (the caller shows the most recently updated status file).
void NPPickSource(const char* active, const char* target, NPSourcePick* out);

// B1.19: should the dock display be kept awake (idle timer disabled)? 1 iff active_source names a real
// source: audio owns the output now (playing). A paused remote target (active_source "none"/"" with
// remote_target named), an idle audiod (active_source "none") and no audiod (active_source ""/NULL, the
// file absent or stale) all return 0, so the display may auto-lock while the app keeps rendering a paused
// source's cover. remote_target is accepted for symmetry with NPPickSource but never keeps the screen
// awake. Pure: the same none/empty/stale rule as NPPickSource, no I/O.
int NPSourceKeepAwake(const char* active_source, const char* remote_target);

// B1.17: are the suite's daemons running, as far as the data directory shows (the Now Playing tab says
// "Daemons not running" instead of an empty screen when they are not).
typedef enum {
  NP_DAEMONS_RUNNING = 0,  // audiod's active-source is fresh, or the shown player's status file is
  NP_DAEMONS_NOT_RUNNING,  // the directory exists, but nothing in it is fresh: empty, or an old setup's leftovers
  NP_DAEMONS_NO_DIR        // the data directory does not exist (nothing installed, or not yet migrated)
} NPDaemons;

// 1 when path exists and is a directory (stat(2); the app's allow-list Foundation has no isDirectory: call).
int NPIsDirectory(const char* path);

// dirExists:   the data directory exists and is a directory (NPIsDirectory).
// activeFresh: active-source exists and is no older than staleS (audiod rewrites it every 10 s).
// statusAge:   seconds since the shown status-<source>.plist was modified; < 0 when it is absent.
// staleS:      the limit for both (the app: 30 s).
NPDaemons NPDaemonsState(int dirExists, int activeFresh, double statusAge, double staleS);

#ifdef __cplusplus
}
#endif

#endif
