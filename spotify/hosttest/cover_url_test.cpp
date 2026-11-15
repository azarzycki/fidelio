#include <cstdio>
#include "CoverUrl.h"  // spotify/cli-src (B1.18)
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)
int main() {
  const std::string base = "https://i.scdn.co/image/";
  const std::string hash = "0123456789abcdef01234567";
  CHECK(largeCoverUrl(base + "ab67616d00001e02" + hash) == base + "ab67616d0000b273" + hash, "300 px album id -> the 640 px id, same image hash");
  CHECK(largeCoverUrl(base + "ab67616d00004851" + hash) == base + "ab67616d0000b273" + hash, "64 px album id -> the 640 px id");
  CHECK(largeCoverUrl(base + "ab67616d0000b273" + hash) == base + "ab67616d0000b273" + hash, "already 640 px: unchanged");
  CHECK(largeCoverUrl(base + "3e1f4a76d0c6a0b3a8b0c8f0e1d2c3b4a5968778") == base + "3e1f4a76d0c6a0b3a8b0c8f0e1d2c3b4a5968778", "an id without a size prefix: unchanged");
  CHECK(largeCoverUrl(base + "ab67616d00001e02" + "0123") == base + "ab67616d00001e02" + "0123", "a short id: unchanged");
  CHECK(largeCoverUrl("") == "" && largeCoverUrl("no-slash") == "no-slash", "empty / malformed URL: unchanged");
  printf(fails ? "\n%d FAILURES\n" : "\nall cover-url checks passed\n", fails);
  return fails ? 1 : 0;
}
