/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <AudioToolbox/AudioServices.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for AudioToolbox's headers. This file is written
 * for this repository and declares only the audio-session interface that its sources use: the
 * AudioQueue sinks (airplay/, spotify/cli-src/, audiod) initialise the session on a run loop of their
 * own, choose the media-playback category, follow route changes and interruptions and override the
 * route; NowPlaying (nowplaying/src/NPRemoteControl.m) also asks to mix with other audio. See
 * ../../../README.md, "Stage 3".
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - property IDs are UInt32 four-character codes: audio category 'acat', route change 'roch',
 *     route 'rout', other audio is playing 'othr', route override 'ovrd', mix with others 'cmix';
 *   - the media-playback category is 'medi'; the route overrides are 0 (none) and 'spkr' (speaker);
 *   - the interruption listener gets 1 when an interruption begins and 0 when it ends;
 *   - AudioSessionInitialize returns 'init' when the session was initialised already and 'ini?' when
 *     it could not be initialised;
 *   - the route-change notification is a CFDictionary whose keys are the strings
 *     "OutputDeviceDidChange_Reason" and "OutputDeviceDidChange_OldRoute" (they are used with CFSTR);
 *   - the two listeners' parameter lists, and C linkage for every function.
 */
#if !defined(FIDELIO_SYSROOT_AUDIOSERVICES_H)
#define FIDELIO_SYSROOT_AUDIOSERVICES_H

#include <CoreFoundation/CFRunLoop.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef UInt32 AudioSessionPropertyID;

typedef void (*AudioSessionInterruptionListener)(void *inClientData, UInt32 inInterruptionState);
typedef void (*AudioSessionPropertyListener)(void *inClientData, AudioSessionPropertyID inID, UInt32 inDataSize,
                                             const void *inData);

/* the interruption listener's state */
enum {
    kAudioSessionBeginInterruption = 1,
    kAudioSessionEndInterruption = 0
};

/* results of AudioSessionInitialize */
enum {
    kAudioSessionAlreadyInitialized = 'init',
    kAudioSessionInitializationError = 'ini?'
};

/* properties */
enum {
    kAudioSessionProperty_AudioCategory = 'acat',
    kAudioSessionProperty_AudioRouteChange = 'roch',
    kAudioSessionProperty_AudioRoute = 'rout',
    kAudioSessionProperty_OtherAudioIsPlaying = 'othr',
    kAudioSessionProperty_OverrideAudioRoute = 'ovrd',
    kAudioSessionProperty_OverrideCategoryMixWithOthers = 'cmix'
};

/* values of kAudioSessionProperty_AudioCategory (a UInt32) */
enum {
    kAudioSessionCategory_MediaPlayback = 'medi'
};

/* values of kAudioSessionProperty_OverrideAudioRoute (a UInt32) */
enum {
    kAudioSessionOverrideAudioRoute_None = 0,
    kAudioSessionOverrideAudioRoute_Speaker = 'spkr'
};

/* keys of the dictionary that a kAudioSessionProperty_AudioRouteChange listener receives */
#define kAudioSession_AudioRouteChangeKey_Reason "OutputDeviceDidChange_Reason"
#define kAudioSession_AudioRouteChangeKey_OldRoute "OutputDeviceDidChange_OldRoute"

/* inRunLoop NULL: the main run loop; inRunLoopMode NULL: the default mode */
extern OSStatus AudioSessionInitialize(CFRunLoopRef inRunLoop, CFStringRef inRunLoopMode,
                                       AudioSessionInterruptionListener inInterruptionListener, void *inClientData);
extern OSStatus AudioSessionSetActive(Boolean active);
extern OSStatus AudioSessionGetProperty(AudioSessionPropertyID inID, UInt32 *ioDataSize, void *outData);
extern OSStatus AudioSessionSetProperty(AudioSessionPropertyID inID, UInt32 inDataSize, const void *inData);
extern OSStatus AudioSessionAddPropertyListener(AudioSessionPropertyID inID, AudioSessionPropertyListener inProc,
                                                void *inClientData);

#if defined(__cplusplus)
}
#endif

#endif /* FIDELIO_SYSROOT_AUDIOSERVICES_H */
