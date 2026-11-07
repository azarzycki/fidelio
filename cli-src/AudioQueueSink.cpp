#include "AudioQueueSink.h"

#include <AudioToolbox/AudioServices.h>
#include <string.h>

#include <cstdio>

#include "ios3_trace.h"

AudioQueueSink::AudioQueueSink() : ring(kRingBytes) {
  pthread_mutex_init(&mutex, nullptr);
  pthread_cond_init(&spaceAvailable, nullptr);
  softwareVolumeControl = false;  // we use the AudioQueue volume parameter

  // Ask for the playback category so audio keeps going with the screen locked.
  // AudioSession is optional for a command-line process; errors are not fatal.
  OSStatus st = AudioSessionInitialize(nullptr, nullptr, nullptr, nullptr);
  if (st == 0) {
    UInt32 category = kAudioSessionCategory_MediaPlayback;
    AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(category), &category);
    AudioSessionSetActive(true);
  } else {
    fprintf(stderr, "AudioQueueSink: AudioSessionInitialize -> %ld (ignored)\n", (long)st);
  }

  setParams(44100, 2, 16);
}

AudioQueueSink::~AudioQueueSink() {
  stop();
  if (queue) AudioQueueDispose(queue, true);
  pthread_cond_destroy(&spaceAvailable);
  pthread_mutex_destroy(&mutex);
}

bool AudioQueueSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  if (open && format.mSampleRate == sampleRate && format.mChannelsPerFrame == channelCount &&
      format.mBitsPerChannel == bitDepth)
    return true;

  stop();
  if (queue) {
    AudioQueueDispose(queue, true);
    queue = nullptr;
  }

  memset(&format, 0, sizeof format);
  format.mSampleRate = sampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  format.mChannelsPerFrame = channelCount;
  format.mBitsPerChannel = bitDepth;
  format.mBytesPerFrame = channelCount * (bitDepth / 8);
  format.mFramesPerPacket = 1;
  format.mBytesPerPacket = format.mBytesPerFrame;

  // NULL run loop: AudioQueue uses its own internal thread for the callbacks.
  IOS3_TRACE("AudioQueueNewOutput begin (%u Hz, %u ch, %u bit)", (unsigned)sampleRate, (unsigned)channelCount, (unsigned)bitDepth);
  OSStatus st = AudioQueueNewOutput(&format, outputCallback, this, nullptr, nullptr, 0, &queue);
  IOS3_TRACE("AudioQueueNewOutput -> %ld", (long)st);
  if (st != 0) {
    fprintf(stderr, "AudioQueueSink: AudioQueueNewOutput -> %ld\n", (long)st);
    queue = nullptr;
    open = false;
    return false;
  }
  for (int i = 0; i < kNumBuffers; i++) {
    st = AudioQueueAllocateBuffer(queue, kBufferBytes, &buffers[i]);
    if (st != 0) {
      fprintf(stderr, "AudioQueueSink: AudioQueueAllocateBuffer -> %ld\n", (long)st);
      return false;
    }
  }
  open = true;
  return true;
}

void AudioQueueSink::start() {
  if (!open || running) return;
  // Prime every buffer (with silence if nothing is queued yet) and start.
  for (int i = 0; i < kNumBuffers; i++) fillBuffer(buffers[i]);
  IOS3_TRACE("AudioQueueStart begin");
  OSStatus st = AudioQueueStart(queue, nullptr);
  IOS3_TRACE("AudioQueueStart -> %ld", (long)st);
  running = (st == 0);
}

void AudioQueueSink::stop() {
  if (!running) return;
  AudioQueueStop(queue, true);
  running = false;
}

void AudioQueueSink::flush() {
  pthread_mutex_lock(&mutex);
  ringHead = 0;
  ringFill = 0;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);
  if (running) AudioQueueReset(queue);  // drops the queued buffers, keeps the queue running
}

double AudioQueueSink::bufferedSeconds() {
  pthread_mutex_lock(&mutex);
  size_t fill = ringFill;
  pthread_mutex_unlock(&mutex);
  size_t bytes = fill + (running ? kNumBuffers * kBufferBytes : 0);
  return format.mBytesPerFrame ? (double)bytes / (format.mBytesPerFrame * format.mSampleRate) : 0;
}

// Runs on the AudioQueue thread.
void AudioQueueSink::outputCallback(void* userData, AudioQueueRef, AudioQueueBufferRef buffer) {
  static_cast<AudioQueueSink*>(userData)->fillBuffer(buffer);
}

void AudioQueueSink::fillBuffer(AudioQueueBufferRef buffer) {
  uint8_t* out = static_cast<uint8_t*>(buffer->mAudioData);
  size_t want = buffer->mAudioDataBytesCapacity;

  pthread_mutex_lock(&mutex);
  size_t take = ringFill < want ? ringFill : want;
  // keep whole frames
  take -= take % format.mBytesPerFrame;
  size_t first = kRingBytes - ringHead;
  if (first > take) first = take;
  memcpy(out, &ring[ringHead], first);
  if (take > first) memcpy(out + first, &ring[0], take - first);
  ringHead = (ringHead + take) % kRingBytes;
  ringFill -= take;
  pthread_cond_broadcast(&spaceAvailable);
  pthread_mutex_unlock(&mutex);

  // Underrun: pad with silence so the queue never starves and stops.
  if (take < want) memset(out + take, 0, want - take);
  buffer->mAudioDataByteSize = want;
  AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void AudioQueueSink::feedPCMFrames(const uint8_t* data, size_t bytes) {
  if (!open) return;
  if (!running) start();

  pthread_mutex_lock(&mutex);
  while (bytes > 0) {
    while (ringFill == kRingBytes) pthread_cond_wait(&spaceAvailable, &mutex);
    size_t space = kRingBytes - ringFill;
    size_t n = bytes < space ? bytes : space;
    size_t tail = (ringHead + ringFill) % kRingBytes;
    size_t first = kRingBytes - tail;
    if (first > n) first = n;
    memcpy(&ring[tail], data, first);
    if (n > first) memcpy(&ring[0], data + first, n - first);
    ringFill += n;
    data += n;
    bytes -= n;
  }
  pthread_mutex_unlock(&mutex);
}

void AudioQueueSink::volumeChanged(uint16_t volume) {
  if (queue) AudioQueueSetParameter(queue, kAudioQueueParam_Volume, volume / 65535.0f);
}
