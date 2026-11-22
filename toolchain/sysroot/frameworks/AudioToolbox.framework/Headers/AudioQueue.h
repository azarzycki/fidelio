/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <AudioToolbox/AudioQueue.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for AudioToolbox's headers. This file is written
 * for this repository and declares only the part of the AudioQueue interface that the output sinks
 * use (airplay/src/AudioQueueSink.*, spotify/cli-src/AudioQueueSink.*, and audiod, which builds the
 * former): an output queue, its buffers, starting, pausing, stopping, resetting, disposing, and the
 * volume parameter. See ../../../README.md, "Stage 3".
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - a queue is a pointer to "struct OpaqueAudioQueue" and a buffer a pointer to
 *     "struct AudioQueueBuffer"; both tags are part of the C++ names of functions that take them;
 *   - a buffer begins with its capacity in bytes (UInt32), the address of its data and the number of
 *     valid bytes (UInt32). The framework allocates buffers and the structure has further members after
 *     these three, which are not declared here: never take its size;
 *   - the output callback receives the user data, the queue and the buffer, in this order;
 *   - the volume parameter is number 1 and parameter values are Float32;
 *   - the error "enqueue during reset" is -66632;
 *   - every function returns an OSStatus and has C linkage.
 */
#if !defined(IOS3_SYSROOT_AUDIOQUEUE_H)
#define IOS3_SYSROOT_AUDIOQUEUE_H

#include <CoreAudio/CoreAudioTypes.h>
#include <CoreFoundation/CFRunLoop.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct OpaqueAudioQueue *AudioQueueRef;

typedef struct AudioQueueBuffer {
    const UInt32 mAudioDataBytesCapacity;
    void *const mAudioData;
    UInt32 mAudioDataByteSize;
    /* further members follow; see the comment at the top */
} AudioQueueBuffer;
typedef AudioQueueBuffer *AudioQueueBufferRef;

typedef void (*AudioQueueOutputCallback)(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);

typedef UInt32 AudioQueueParameterID;
typedef Float32 AudioQueueParameterValue;

enum {
    kAudioQueueParam_Volume = 1
};

enum {
    kAudioQueueErr_EnqueueDuringReset = -66632
};

/* a new output queue for the format; callbacks on inCallbackRunLoop, or on a thread of the queue's
   own when it is NULL; inFlags must be 0 */
extern OSStatus AudioQueueNewOutput(const AudioStreamBasicDescription *inFormat, AudioQueueOutputCallback inCallbackProc,
                                    void *inUserData, CFRunLoopRef inCallbackRunLoop, CFStringRef inCallbackRunLoopMode,
                                    UInt32 inFlags, AudioQueueRef *outAQ);
extern OSStatus AudioQueueAllocateBuffer(AudioQueueRef inAQ, UInt32 inBufferByteSize, AudioQueueBufferRef *outBuffer);
/* no packet descriptions for linear PCM: inNumPacketDescs 0, inPacketDescs NULL */
extern OSStatus AudioQueueEnqueueBuffer(AudioQueueRef inAQ, AudioQueueBufferRef inBuffer, UInt32 inNumPacketDescs,
                                        const AudioStreamPacketDescription *inPacketDescs);
/* inStartTime NULL: as soon as possible */
extern OSStatus AudioQueueStart(AudioQueueRef inAQ, const AudioTimeStamp *inStartTime);
extern OSStatus AudioQueuePause(AudioQueueRef inAQ);
extern OSStatus AudioQueueStop(AudioQueueRef inAQ, Boolean inImmediate);
extern OSStatus AudioQueueReset(AudioQueueRef inAQ);
extern OSStatus AudioQueueDispose(AudioQueueRef inAQ, Boolean inImmediate);
extern OSStatus AudioQueueSetParameter(AudioQueueRef inAQ, AudioQueueParameterID inParamID, AudioQueueParameterValue inValue);

#if defined(__cplusplus)
}
#endif

#endif /* IOS3_SYSROOT_AUDIOQUEUE_H */
