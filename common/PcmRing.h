#pragma once
// A ring of interleaved S16 stereo PCM with a click-free source switch. Used by the AudioQueue
// sink (under its ring mutex) and by the host paced output model, so both fade identically.
//
// crossfade(fadeFrames): the next `fadeFrames` at the head (the audio about to play) are ramped
// to zero in place and everything after them is dropped; the next `fadeFrames` fed are ramped
// from zero and MIXED over that tail, then appended normally. Frames fed for positions the
// playhead has already passed are dropped (the tail played out faded). With nothing fed
// afterwards it is a plain fade-out. fadeInNext(): after a hard clear (resume), ramp what comes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <vector>

class PcmRing {
 public:
  explicit PcmRing(size_t bytes) : buf(bytes) {}
  size_t capacity() const { return buf.size(); }
  size_t fill() const { return fillBytes; }
  size_t space() const { return buf.size() - fillBytes; }

  // Append whole frames from `data` (up to `bytes`, limited by space); returns the bytes consumed.
  size_t feed(const uint8_t* data, size_t bytes) {
    bytes -= bytes % 4;
    size_t done = 0;
    while (done < bytes) {
      if (fadeInPos < fadeInTotal || tailStart < tailEnd) {  // slow path: one frame with gain / mix
        if (!feedFrame(reinterpret_cast<const int16_t*>(data + done))) break;
        done += 4;
        continue;
      }
      size_t n = bytes - done;
      if (n > space()) n = space();
      if (n == 0) break;
      size_t tail = (head + fillBytes) % buf.size();
      size_t first = buf.size() - tail;
      if (first > n) first = n;
      memcpy(&buf[tail], data + done, first);
      if (n > first) memcpy(&buf[0], data + done + first, n - first);
      fillBytes += n;
      done += n;
    }
    return done;
  }

  // Take up to `bytes` (whole frames) from the head; returns the bytes copied.
  size_t take(uint8_t* out, size_t bytes) {
    size_t n = fillBytes < bytes ? fillBytes : bytes;
    n -= n % 4;
    size_t first = buf.size() - head;
    if (first > n) first = n;
    memcpy(out, &buf[head], first);
    if (n > first) memcpy(out + first, &buf[0], n - first);
    head = (head + n) % buf.size();
    fillBytes -= n;
    headFrame += n / 4;
    return n;
  }

  void clear() {
    head = 0; fillBytes = 0;
    tailStart = tailEnd = 0;
    fadeInTotal = fadeInPos = 0;
  }

  // Ramp the next fed `fadeFrames` from zero (after a clear / restart, so the re-prime does not click).
  void fadeInNext(size_t fadeFrames) { fadeInTotal = fadeFrames; fadeInPos = 0; }

  void crossfade(size_t fadeFrames) {
    if (fadeFrames == 0) { clear(); return; }
    size_t tailFrames = fillBytes / 4;
    if (tailFrames > fadeFrames) tailFrames = fadeFrames;
    for (size_t i = 0; i < tailFrames; i++) {  // 1 -> 0 over the tail, in place
      int g = (int)(((fadeFrames - i) * 1024) / fadeFrames);
      int16_t* f = frameAt(i);
      f[0] = (int16_t)((f[0] * g) >> 10);
      f[1] = (int16_t)((f[1] * g) >> 10);
    }
    fillBytes = tailFrames * 4;  // drop the rest of the old source's lead
    tailStart = headFrame;
    tailEnd = headFrame + tailFrames;
    fadeInTotal = fadeFrames;
    fadeInPos = 0;
  }

 private:
  int16_t* frameAt(size_t framesFromHead) { return reinterpret_cast<int16_t*>(&buf[(head + framesFromHead * 4) % buf.size()]); }
  static int16_t sat(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
  bool feedFrame(const int16_t* in) {
    int g = fadeInPos < fadeInTotal ? (int)((fadeInPos * 1024) / fadeInTotal) : 1024;
    if (fadeInPos < fadeInTotal) fadeInPos++;
    int l = (in[0] * g) >> 10, r = (in[1] * g) >> 10;
    if (tailStart < tailEnd) {           // mix over the fading tail
      uint64_t p = tailStart++;
      if (p < headFrame) return true;    // the playhead already passed this frame: the tail played out, this frame is dropped
      int16_t* f = frameAt((size_t)(p - headFrame));
      f[0] = sat(f[0] + l);
      f[1] = sat(f[1] + r);
      return true;
    }
    if (space() < 4) return false;
    int16_t* f = reinterpret_cast<int16_t*>(&buf[(head + fillBytes) % buf.size()]);
    f[0] = (int16_t)l;
    f[1] = (int16_t)r;
    fillBytes += 4;
    return true;
  }

  std::vector<uint8_t> buf;
  size_t head = 0, fillBytes = 0;
  uint64_t headFrame = 0;             // frames taken so far (the playhead, in stream frames)
  uint64_t tailStart = 0, tailEnd = 0;  // crossfade tail still to be mixed, in stream frames
  size_t fadeInTotal = 0, fadeInPos = 0;
};
