// B1.16.13: the dock remote's key log for the Technical tab (plain C, also host-tested by
// nowplaying/hosttest/keylog_test.c). The DockRemote tweak inside iapd (phone only) appends one line per
// completed key press to /var/mobile/fidelio/remote-keys.log while it observes or swallows:
//   <unix seconds> <code> <name> <taken|passed>        e.g. "1795369452 0 play-pause taken"
// and starts the file afresh when it passes about 16 KB.
#ifndef NP_KEYLOG_H
#define NP_KEYLOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_KEYLOG_NAME_MAX 24
#define NP_KEYLOG_SHOWN 6  // the Technical tab shows the last six presses

typedef struct {
  long long time;                  // unix seconds
  unsigned long code;              // the tweak's command code
  char name[NP_KEYLOG_NAME_MAX];   // the tweak's name, e.g. "play-pause", "code-7"
  int taken;                       // 1: the tweak swallowed it and sent the verb to audiod
} NPKeyEntry;

// Parse a log text: the last `max` well-formed, complete ('\n'-terminated) lines, newest first. Malformed
// lines are skipped. Returns how many were stored.
int NPKeyLogParse(const char* text, size_t len, NPKeyEntry* out, int max);

// Read the file (its last 8 KB at most) and parse it. Returns the count, 0 when the file is missing or
// holds no complete line.
int NPKeyLogRead(const char* path, NPKeyEntry* out, int max);

// The key's name in words: "play/pause", "next", "previous", "menu", "select", "up", "down",
// "seek forward", "rewind", "code 7" (a name's dashes become spaces; play-pause is play/pause).
void NPKeyWords(const char* name, char* out, size_t cap);

// The local time of day as HH:MM:SS.
void NPKeyClock(long long unixSeconds, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
