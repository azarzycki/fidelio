/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <QuartzCore/QuartzCore.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for QuartzCore's headers. This file is written for
 * this repository and declares only what its sources use: NowPlaying (nowplaying/src/
 * NowPlayingViewController.m) rounds the corners of the cover through the view's layer. See
 * ../../../README.md, "Stage 3".
 *
 * Interface facts stated here: the layer class CALayer, and the names and types of its two properties
 * cornerRadius (CGFloat) and masksToBounds (BOOL), whose setters the source calls.
 */
#if !defined(FIDELIO_SYSROOT_QUARTZCORE_H)
#define FIDELIO_SYSROOT_QUARTZCORE_H

#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>

@interface CALayer : NSObject
@property CGFloat cornerRadius;
@property BOOL masksToBounds;
@end

#endif /* FIDELIO_SYSROOT_QUARTZCORE_H */
