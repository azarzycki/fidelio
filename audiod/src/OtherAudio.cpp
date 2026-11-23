#include "OtherAudio.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <AudioToolbox/AudioServices.h>
#endif

namespace audiod {

#ifdef __APPLE__
bool otherAudioPlaying() {
  UInt32 v = 0, size = sizeof v;
  if (AudioSessionGetProperty(kAudioSessionProperty_OtherAudioIsPlaying, &size, &v) != 0) return false;
  return v != 0;
}
std::string otherAudioDescribe() { return "AudioSession OtherAudioIsPlaying"; }
#else
bool otherAudioPlaying() {
  const char* path = getenv("AUDIOD_MOCK_OTHER_AUDIO_FILE");
  if (!path || !*path) return false;
  FILE* f = fopen(path, "r");
  if (!f) return false;
  int c = fgetc(f);
  fclose(f);
  return c == '1';
}
std::string otherAudioDescribe() {
  const char* path = getenv("AUDIOD_MOCK_OTHER_AUDIO_FILE");
  return path && *path ? std::string("mock (") + path + ")" : "mock (never: AUDIOD_MOCK_OTHER_AUDIO_FILE unset)";
}
#endif

}  // namespace audiod
