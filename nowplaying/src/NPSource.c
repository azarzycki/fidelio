#include "NPSource.h"

#include <string.h>
#include <sys/stat.h>

static int isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void NPSourceName(const char* text, size_t len, char* out, size_t cap) {
  if (!cap) return;
  out[0] = 0;
  if (!text) return;
  while (len && isSpace(*text)) { text++; len--; }
  while (len && isSpace(text[len - 1])) len--;
  if (len > cap - 1) len = cap - 1;
  memcpy(out, text, len);
  out[len] = 0;
}

static int namesSource(const char* s) { return s && s[0] && strcmp(s, "none") != 0; }

static void copyName(char* dst, const char* src) {
  size_t n = strlen(src);
  if (n > NP_SOURCE_NAME_MAX - 1) n = NP_SOURCE_NAME_MAX - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

void NPPickSource(const char* active, const char* target, NPSourcePick* out) {
  memset(out, 0, sizeof *out);
  if (!active) active = "";
  if (namesSource(active)) {
    copyName(out->shown, active);
  } else if (namesSource(target)) {
    copyName(out->shown, target);
    out->paused = 1;
  } else if (strcmp(active, "none") == 0) {
    out->idle = 1;
  }
}

int NPSourceKeepAwake(const char* active_source, const char* remote_target) {
  (void)remote_target;  // for symmetry with NPPickSource; a paused target never keeps the screen awake
  return namesSource(active_source) ? 1 : 0;
}

int NPIsDirectory(const char* path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
}

NPDaemons NPDaemonsState(int dirExists, int activeFresh, double statusAge, double staleS) {
  if (!dirExists) return NP_DAEMONS_NO_DIR;
  if (activeFresh) return NP_DAEMONS_RUNNING;
  if (statusAge >= 0 && statusAge <= staleS) return NP_DAEMONS_RUNNING;  // a player without audiod
  return NP_DAEMONS_NOT_RUNNING;
}
