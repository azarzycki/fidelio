#include "NPKeyLog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// One line, without its '\n': "<digits> <digits> <name> <taken|passed>".
static int parseLine(const char* s, size_t n, NPKeyEntry* e) {
  char buf[96];
  char *p, *end, *name, *tail;
  size_t nl;
  if (n == 0 || n >= sizeof buf) return 0;
  memcpy(buf, s, n);
  buf[n] = 0;
  p = buf;
  if (*p < '0' || *p > '9') return 0;
  e->time = strtoll(p, &end, 10);
  if (*end != ' ') return 0;
  p = end + 1;
  if (*p < '0' || *p > '9') return 0;
  e->code = strtoul(p, &end, 10);
  if (*end != ' ') return 0;
  name = end + 1;
  tail = strchr(name, ' ');
  if (!tail || tail == name) return 0;
  nl = (size_t)(tail - name);
  if (nl >= NP_KEYLOG_NAME_MAX) return 0;
  if (strcmp(tail + 1, "taken") == 0) e->taken = 1;
  else if (strcmp(tail + 1, "passed") == 0) e->taken = 0;
  else return 0;
  memcpy(e->name, name, nl);
  e->name[nl] = 0;
  return 1;
}

int NPKeyLogParse(const char* text, size_t len, NPKeyEntry* out, int max) {
  int count = 0;
  size_t end;
  if (!text || !out || max <= 0) return 0;
  // Only complete lines: drop a last line that has no '\n' yet (the tweak is writing it).
  end = len;
  while (end > 0 && text[end - 1] != '\n') end--;
  // Walk backwards, newest line first.
  while (end > 0 && count < max) {
    size_t lineEnd = end - 1;  // the '\n'
    size_t start = lineEnd;
    while (start > 0 && text[start - 1] != '\n') start--;
    {
      size_t n = lineEnd - start;
      if (n > 0 && text[start + n - 1] == '\r') n--;
      if (parseLine(text + start, n, &out[count])) count++;
    }
    end = start;
  }
  return count;
}

int NPKeyLogRead(const char* path, NPKeyEntry* out, int max) {
  enum { HALF = 4096 };
  static char buf[2 * HALF];
  size_t have = 0, n, skip = 0;
  int dropped = 0;
  FILE* f;
  if (!path) return 0;
  f = fopen(path, "r");
  if (!f) return 0;
  // Keep the file's last 4-8 KB (six lines need well under 1 KB): read on, and when the buffer is full
  // move its second half to the front.
  while ((n = fread(buf + have, 1, sizeof buf - have, f)) > 0) {
    have += n;
    if (have == sizeof buf) {
      memmove(buf, buf + HALF, HALF);
      have = HALF;
      dropped = 1;
    }
  }
  fclose(f);
  if (dropped) {  // the first line in the buffer may be cut: start after its end
    while (skip < have && buf[skip] != '\n') skip++;
    if (skip < have) skip++;
  }
  return NPKeyLogParse(buf + skip, have - skip, out, max);
}

void NPKeyWords(const char* name, char* out, size_t cap) {
  size_t i;
  if (!out || cap == 0) return;
  if (!name) name = "";
  if (strcmp(name, "play-pause") == 0) {
    snprintf(out, cap, "%s", "play/pause");
    return;
  }
  for (i = 0; name[i] && i + 1 < cap; i++) out[i] = name[i] == '-' ? ' ' : name[i];
  out[i] = 0;
}

void NPKeyClock(long long unixSeconds, char* out, size_t cap) {
  time_t t = (time_t)unixSeconds;
  struct tm* tm = localtime(&t);  // main thread only (the app's UI); localtime_r is not in the sysroot's list
  if (!out || cap == 0) return;
  if (!tm || strftime(out, cap, "%H:%M:%S", tm) == 0) snprintf(out, cap, "%s", "--:--:--");
}
