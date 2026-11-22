/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <AVFoundation/AVFoundation.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for AVFoundation's headers. This file is written
 * for this repository and declares only what its sources use: NowPlaying (nowplaying/src/
 * NPRemoteControl.m) plays a second of silence in a loop with an AVAudioPlayer (stage 3 of its
 * remote-control prototype). See ../../../README.md, "Stage 3".
 *
 * Interface facts stated here: the class AVAudioPlayer; its initialiser from data with an error
 * out-parameter; -play, which returns a BOOL; the property numberOfLoops (an NSInteger, -1 for
 * endless), whose setter the source calls.
 */
#if !defined(FIDELIO_SYSROOT_AVFOUNDATION_H)
#define FIDELIO_SYSROOT_AVFOUNDATION_H

#import <Foundation/Foundation.h>

@interface AVAudioPlayer : NSObject
- (id)initWithData:(NSData *)data error:(NSError **)outError;
- (BOOL)play;
@property NSInteger numberOfLoops;
@end

#endif /* FIDELIO_SYSROOT_AVFOUNDATION_H */
