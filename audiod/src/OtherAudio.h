#pragma once
// B1.16.16: is audio that is NOT audiod's playing right now (the built-in iPod/Music app, any other
// app)? audiod asks this while a paused source of ours is the dock remote's target (Server.cpp,
// remote target): once other audio plays, the remote's keys belong to that app again.
//
// Device: AudioSessionGetProperty(kAudioSessionProperty_OtherAudioIsPlaying), the same query the
// AudioQueueSink's yield uses (airplay/src/AudioQueueSink.cpp, otherAudioIsPlaying). It answers for the
// other processes, never for audiod's own queue. Before the AudioSession is initialised (bare mode, or
// while AudioSessionInitialize is still being retried) the call fails and the answer is "no".
// Host: AUDIOD_MOCK_OTHER_AUDIO_FILE names a file; "1" as its first character means "other audio is
// playing" (absent or anything else: not playing).
#include <string>

namespace audiod {

bool otherAudioPlaying();
std::string otherAudioDescribe();  // for the start-up line

}  // namespace audiod
