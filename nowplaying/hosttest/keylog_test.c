#define _POSIX_C_SOURCE 200809L  /* setenv, mkstemp, fdopen under -std=c99 */
// B1.16.13 host test of the Technical tab's dock-remote key log reader (nowplaying/src/NPKeyLog.c, the
// same C the app compiles): the line format the DockRemote tweak writes, newest first, the last six,
// partial and malformed lines, the words shown and the clock.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "NPKeyLog.h"

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static int parse(const char* s, NPKeyEntry* e, int max) { return NPKeyLogParse(s, strlen(s), e, max); }

int main(void) {
  NPKeyEntry e[NP_KEYLOG_SHOWN + 2];
  char w[32];
  int n;

  n = parse("1795369452 0 play-pause taken\n", e, NP_KEYLOG_SHOWN);
  CHECK(n == 1 && e[0].time == 1795369452LL && e[0].code == 0 && !strcmp(e[0].name, "play-pause") && e[0].taken == 1,
        "one line: time, code, name, taken");
  n = parse("1 20 next taken\n2 22 menu passed\n3 4 rewind passed\n", e, NP_KEYLOG_SHOWN);
  CHECK(n == 3 && e[0].time == 3 && e[1].time == 2 && e[2].time == 1, "newest first");
  CHECK(e[1].taken == 0 && e[1].code == 22 && !strcmp(e[1].name, "menu"), "passed is not taken");

  {
    char big[1024] = "";
    int i;
    for (i = 1; i <= 9; i++) { char l[48]; snprintf(l, sizeof l, "%d 23 select passed\n", 100 + i); strcat(big, l); }
    n = parse(big, e, NP_KEYLOG_SHOWN);
    CHECK(n == 6 && e[0].time == 109 && e[5].time == 104, "nine lines: the last six, newest first (%d)", n);
  }
  n = parse("1 0 play-pause taken\n2 21 previous tak", e, NP_KEYLOG_SHOWN);
  CHECK(n == 1 && e[0].time == 1, "a last line without its newline (being written) is ignored");
  n = parse("1 0 play-pause taken\ngarbage\n\n2 x up passed\n3 24 up maybe\n4 24  passed\nabc 24 up passed\n5 24 up passed\r\n", e, NP_KEYLOG_SHOWN);
  CHECK(n == 2 && e[0].time == 5 && e[1].time == 1, "malformed and empty lines are skipped, CRLF accepted (%d)", n);
  n = parse("7 25 a-name-that-is-far-too-long-for-the-field passed\n8 25 down passed\n", e, NP_KEYLOG_SHOWN);
  CHECK(n == 1 && e[0].time == 8, "an over-long name is skipped, not truncated");
  CHECK(parse("", e, NP_KEYLOG_SHOWN) == 0 && NPKeyLogParse(NULL, 0, e, 6) == 0, "empty text: nothing");

  NPKeyWords("play-pause", w, sizeof w);   CHECK(!strcmp(w, "play/pause"), "words: play/pause");
  NPKeyWords("seek-forward", w, sizeof w); CHECK(!strcmp(w, "seek forward"), "words: seek forward");
  NPKeyWords("code-7", w, sizeof w);       CHECK(!strcmp(w, "code 7"), "words: code 7");
  NPKeyWords("previous", w, sizeof w);     CHECK(!strcmp(w, "previous"), "words: previous unchanged");
  NPKeyWords("seek-forward", w, 5);        CHECK(!strcmp(w, "seek"), "words: cut to the buffer");

  setenv("TZ", "UTC", 1); tzset();
  NPKeyClock(1795369452LL, w, sizeof w);
  CHECK(!strcmp(w, "17:44:12"), "clock: HH:MM:SS in local time (UTC here): %s", w);
  setenv("TZ", "CET-1", 1); tzset();
  NPKeyClock(1795369452LL, w, sizeof w);
  CHECK(!strcmp(w, "18:44:12"), "clock: follows the time zone (UTC+1): %s", w);

  {
    char path[] = "/tmp/np-keylog-XXXXXX";
    int fd = mkstemp(path);
    FILE* f = fd >= 0 ? fdopen(fd, "w") : NULL;
    int i;
    if (!f) { printf("FAIL mkstemp\n"); return 1; }
    for (i = 0; i < 1000; i++) fprintf(f, "%d %d %s passed\n", 1000 + i, 22 + i % 3, i % 3 == 0 ? "menu" : i % 3 == 1 ? "select" : "up");
    fclose(f);
    n = NPKeyLogRead(path, e, NP_KEYLOG_SHOWN);
    CHECK(n == 6 && e[0].time == 1999 && e[5].time == 1994, "a 20 KB file: the last six lines, read from its tail (%d)", n);
    unlink(path);
    CHECK(NPKeyLogRead(path, e, NP_KEYLOG_SHOWN) == 0, "a missing file: nothing");
  }

  printf(fails ? "\n%d FAILURES\n" : "\nall key-log checks passed\n", fails);
  return fails ? 1 : 0;
}
