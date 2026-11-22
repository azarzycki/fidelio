/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <CoreAudio/CoreAudioTypes.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for CoreAudio's headers. This file is written for
 * this repository and declares only what its sources use (see ../../../README.md, "Stage 3"): the
 * description of a linear PCM stream that the AudioQueue sinks of airplay/ and spotify/cli-src/ fill
 * in, and the time-stamp and packet-description types that AudioQueueStart and
 * AudioQueueEnqueueBuffer take pointers to.
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - AudioStreamBasicDescription is a Float64 sample rate followed by eight UInt32 fields (format ID,
 *     format flags, bytes per packet, frames per packet, bytes per frame, channels per frame, bits per
 *     channel, reserved), 40 bytes in all;
 *   - the linear PCM format ID is the four-character code 'lpcm'; the flag "signed integer" is bit 2
 *     and "packed" is bit 3;
 *   - AudioTimeStamp and AudioStreamPacketDescription are only passed by pointer here (as NULL), so
 *     their members are not declared.
 */
#if !defined(IOS3_SYSROOT_COREAUDIOTYPES_H)
#define IOS3_SYSROOT_COREAUDIOTYPES_H

#include <MacTypes.h>

struct AudioStreamBasicDescription {
    Float64 mSampleRate;
    UInt32 mFormatID;
    UInt32 mFormatFlags;
    UInt32 mBytesPerPacket;
    UInt32 mFramesPerPacket;
    UInt32 mBytesPerFrame;
    UInt32 mChannelsPerFrame;
    UInt32 mBitsPerChannel;
    UInt32 mReserved;
};
typedef struct AudioStreamBasicDescription AudioStreamBasicDescription;

enum {
    kAudioFormatLinearPCM = 'lpcm'
};

enum {
    kLinearPCMFormatFlagIsSignedInteger = (1U << 2),
    kLinearPCMFormatFlagIsPacked = (1U << 3)
};

/* opaque here: only a pointer to each is ever passed */
typedef struct AudioTimeStamp AudioTimeStamp;
typedef struct AudioStreamPacketDescription AudioStreamPacketDescription;

#endif /* IOS3_SYSROOT_COREAUDIOTYPES_H */
