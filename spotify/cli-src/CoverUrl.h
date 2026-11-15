#pragma once
// B1.18: ask Spotify's image CDN for the 640 px album cover instead of whichever size the track's
// metadata listed first (cspot takes cover_group.image[0]; the trimmed metadata.proto carries no size
// field, so the library cannot choose). Album-art ids on i.scdn.co carry the size in their first 16
// hex digits, followed by the same 24-digit image hash for every size of one image:
//   ab67616d0000b273 = 640x640, ab67616d00001e02 = 300x300, ab67616d00004851 = 64x64.
// Any other id (e.g. an older 40-hex id without that prefix) is returned unchanged. The caller fetches
// the large URL first and falls back to the original one if that fetch fails.
#include <string>

inline std::string largeCoverUrl(const std::string& url) {
  static const char* const kLarge = "ab67616d0000b273";
  static const char* const kSmaller[] = { "ab67616d00001e02", "ab67616d00004851" };
  size_t slash = url.rfind('/');
  if (slash == std::string::npos) return url;
  std::string id = url.substr(slash + 1);
  if (id.size() != 40 || id.find_first_not_of("0123456789abcdef") != std::string::npos) return url;
  for (const char* s : kSmaller)
    if (id.compare(0, 16, s) == 0) return url.substr(0, slash + 1) + kLarge + id.substr(16);
  return url;
}
