// B1.16.16: which source the Now Playing tab shows (plain C, also host-tested by
// nowplaying/hosttest/source_test.c). audiod writes two one-line files in /var/mobile/fidelio, each
// replaced atomically at once on a change and rewritten every 10 s:
//   active-source   the source that owns the output now ("spotify" / "airplay"), or "none"
//   remote-target   (audiod B1.16.16 and later) the dock remote's target: the active source, or the paused
//                   last one until it disconnects, stops, another source of ours plays, or other audio
//                   (the Music app) plays; "none" otherwise. The DockRemote tweak reads the same file.
// The app reads them once a second (FidelioStatus refresh) and treats a file older than 30 s as absent.
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

#ifdef __cplusplus
}
#endif

#endif
