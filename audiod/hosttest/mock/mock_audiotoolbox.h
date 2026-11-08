#pragma once
// A small stand-in for the AudioToolbox / AudioSession / CoreFoundation surface that
// airplay/src/AudioQueueSink.cpp uses, so the sink's state machine (start/stop/pause, interruption
// and route-change callbacks, buffer ownership, the B1.1 recovery watchdog) can be exercised on the
// Linux host. The fake queue completes buffers from its own thread like the real one; the test
// drives the AudioSession callbacks directly (mock::fireInterruption / fireRouteChange).
#include <stddef.h>
#include <stdint.h>

typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef unsigned char Boolean;
typedef float Float32;
typedef double Float64;
typedef double CFAbsoluteTime;
typedef UInt32 CFStringEncoding;

struct __CFString { const char* s; };
typedef const __CFString* CFStringRef;
struct __CFRunLoop;
typedef __CFRunLoop* CFRunLoopRef;
struct __CFRunLoopTimer;
typedef __CFRunLoopTimer* CFRunLoopTimerRef;
struct __CFDictionary;
typedef const __CFDictionary* CFDictionaryRef;
struct __CFNumber;
typedef const __CFNumber* CFNumberRef;
typedef const void* CFTypeRef;
typedef void* CFAllocatorRef;
typedef long CFIndex;
typedef UInt32 CFNumberType;
typedef UInt32 CFOptionFlags;
typedef double CFTimeInterval;
typedef void (*CFRunLoopTimerCallBack)(CFRunLoopTimerRef, void*);

enum { kCFNumberSInt32Type = 3 };
enum { kCFStringEncodingUTF8 = 0x08000100 };
extern CFStringRef kCFRunLoopDefaultMode;
CFStringRef mock_cfstr(const char* literal);
#define CFSTR(x) mock_cfstr(x)

CFRunLoopRef CFRunLoopGetCurrent(void);
void CFRunLoopRun(void);
void CFRunLoopStop(CFRunLoopRef rl);
CFRunLoopTimerRef CFRunLoopTimerCreate(CFAllocatorRef, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags, CFIndex order,
                                       CFRunLoopTimerCallBack, void* context);
void CFRunLoopAddTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef mode);
void CFRunLoopRemoveTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef mode);
CFAbsoluteTime CFAbsoluteTimeGetCurrent(void);
void CFRelease(CFTypeRef);
Boolean CFStringGetCString(CFStringRef, char* buffer, CFIndex size, CFStringEncoding);
const void* CFDictionaryGetValue(CFDictionaryRef, const void* key);
Boolean CFNumberGetValue(CFNumberRef, CFNumberType, void* out);

// ---- AudioToolbox: AudioQueue
struct AudioStreamBasicDescription {
  Float64 mSampleRate;
  UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
};
enum { kAudioFormatLinearPCM = 'lpcm' };
enum { kLinearPCMFormatFlagIsSignedInteger = 4, kLinearPCMFormatFlagIsPacked = 8 };
struct AudioQueueBuffer {
  UInt32 mAudioDataBytesCapacity;
  void* mAudioData;
  UInt32 mAudioDataByteSize;
};
typedef AudioQueueBuffer* AudioQueueBufferRef;
struct OpaqueAudioQueue;
typedef OpaqueAudioQueue* AudioQueueRef;
typedef void (*AudioQueueOutputCallback)(void* userData, AudioQueueRef, AudioQueueBufferRef);
typedef UInt32 AudioQueueParameterID;
typedef Float32 AudioQueueParameterValue;
enum { kAudioQueueParam_Volume = 1 };
enum { kAudioQueueErr_EnqueueDuringReset = -66632, kAudioQueueErr_CannotStart = -66681 };
struct AudioTimeStamp;

OSStatus AudioQueueNewOutput(const AudioStreamBasicDescription*, AudioQueueOutputCallback, void* userData, CFRunLoopRef, CFStringRef, UInt32,
                             AudioQueueRef* out);
OSStatus AudioQueueAllocateBuffer(AudioQueueRef, UInt32 bytes, AudioQueueBufferRef* out);
OSStatus AudioQueueEnqueueBuffer(AudioQueueRef, AudioQueueBufferRef, UInt32, const void*);
OSStatus AudioQueueStart(AudioQueueRef, const AudioTimeStamp*);
OSStatus AudioQueuePause(AudioQueueRef);
OSStatus AudioQueueStop(AudioQueueRef, Boolean immediate);
OSStatus AudioQueueReset(AudioQueueRef);
OSStatus AudioQueueDispose(AudioQueueRef, Boolean immediate);
OSStatus AudioQueueSetParameter(AudioQueueRef, AudioQueueParameterID, AudioQueueParameterValue);

// ---- AudioToolbox: AudioSession
typedef UInt32 AudioSessionPropertyID;
typedef void (*AudioSessionInterruptionListener)(void* clientData, UInt32 state);
typedef void (*AudioSessionPropertyListener)(void* clientData, AudioSessionPropertyID, UInt32 size, const void* data);
enum { kAudioSessionBeginInterruption = 1, kAudioSessionEndInterruption = 0 };
enum {
  kAudioSessionProperty_AudioCategory = 'acat',
  kAudioSessionProperty_AudioRouteChange = 'roch',
  kAudioSessionProperty_OverrideAudioRoute = 'ovrd',
  kAudioSessionProperty_AudioRoute = 'rout'
};
enum { kAudioSessionCategory_MediaPlayback = 'medi' };
enum { kAudioSessionOverrideAudioRoute_None = 0, kAudioSessionOverrideAudioRoute_Speaker = 'spkr' };
#define kAudioSession_AudioRouteChangeKey_Reason "OutputDeviceDidChange_Reason"
#define kAudioSession_AudioRouteChangeKey_OldRoute "OutputDeviceDidChange_OldRoute"
OSStatus AudioSessionInitialize(CFRunLoopRef, CFStringRef, AudioSessionInterruptionListener, void* clientData);
OSStatus AudioSessionSetProperty(AudioSessionPropertyID, UInt32 size, const void* data);
OSStatus AudioSessionGetProperty(AudioSessionPropertyID, UInt32* size, void* out);
OSStatus AudioSessionAddPropertyListener(AudioSessionPropertyID, AudioSessionPropertyListener, void* clientData);
OSStatus AudioSessionSetActive(Boolean);

// ---- test control
namespace mock {
bool queueRunning();          // the live queue exists and AudioQueueStart succeeded on it (not paused/stopped)
int startCalls();             // AudioQueueStart invocations so far
int startFailures();          // of which failed
int queuesCreated();
void setStartFails(bool);     // make AudioQueueStart fail (a genuine interruption / session taken away)
void setRoute(const char*);   // what kAudioSessionProperty_AudioRoute reports
void fireInterruption(UInt32 state);  // invoke the registered interruption listener
void fireRouteChange();               // invoke the registered route-change listener (data = nullptr)
unsigned long completedBuffers();     // buffers the fake queue has played back
void setStopDelayMs(int ms);          // make AudioQueueStop / AudioQueueDispose take this long (slow hardware)
void setCompleting(bool on);          // false: the fake queue keeps running but never completes a buffer (mediaserverd wedge)
int liveQueues();                     // queues created and not yet disposed
}  // namespace mock
