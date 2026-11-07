#pragma once
// One RAOP audio session: the three UDP channels (audio / control / timing), decryption, decoding,
// a seq-ordered jitter buffer and the player thread that feeds the AudioOutput.
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AlacWrapper.h"
#include "AudioOutput.h"
#include "RaopCrypto.h"

class StatusFile;

class RaopSession {
 public:
  enum Codec { CODEC_NONE, CODEC_PCM, CODEC_ALAC };

  RaopSession(AudioOutput* out, StatusFile* status, int latencyMs);
  ~RaopSession();

  // ANNOUNCE
  bool configure(Codec codec, const std::string& fmtp, const std::vector<uint8_t>& aesKey, const std::vector<uint8_t>& aesIv);
  // SETUP: binds the three UDP sockets; the sender's control/timing ports are where we send
  // resend requests / timing requests.
  bool setup(const sockaddr_in& peer, uint16_t peerControlPort, uint16_t peerTimingPort);
  uint16_t audioPort() const { return localAudioPort; }
  uint16_t controlPort() const { return localControlPort; }
  uint16_t timingPort() const { return localTimingPort; }
  // RECORD: first expected seq. FLUSH: drop everything, expect `seq` next.
  void record(uint16_t seq, uint32_t rtptime);
  void flush(uint16_t seq, bool haveSeq);
  void teardown();
  void setVolumeDb(float db);
  float volumeDb() const { return volDb; }
  bool active() const { return running; }
  const char* codecName() const { return codec == CODEC_ALAC ? "ALAC" : codec == CODEC_PCM ? "PCM" : ""; }
  // Packets received in the last ~3 s? (drives the playing/paused state)
  bool audioFlowing();
  // Simple stats for the log.
  std::string stats();

 private:
  struct Slot {
    uint16_t seq = 0;
    bool valid = false;
    uint16_t frames = 0;
    int16_t pcm[352 * 2];
  };
  static const int kSlots = 512;       // 4.08 s at 352 frames/packet: headroom to absorb a bursty sender
  static const int kFramesPerPacket = 352;

  static void* audioThreadEntry(void*);
  static void* controlThreadEntry(void*);
  static void* timingThreadEntry(void*);
  static void* playerThreadEntry(void*);
  void audioLoop();
  void controlLoop();
  void timingLoop();
  void playerLoop();
  void onRtpPacket(const uint8_t* pkt, size_t len, bool resent);
  void requestResend(uint16_t first, uint16_t count);
  void closeSockets();
  bool startThread(pthread_t& t, void* (*fn)(void*), size_t stack);
  static uint64_t ntpNow();
  static uint64_t nowMs();

  AudioOutput* out;
  StatusFile* status;
  int latencyPackets;
  int reprimePackets = 0;     // after a mid-stream drain, resume on this many packets (not the full cushion)
  Codec codec = CODEC_NONE;
  AlacWrapper alac;
  AesCbcDecryptor aes;
  std::atomic<float> volDb{-20.0f};

  int audioSock = -1, controlSock = -1, timingSock = -1;
  uint16_t localAudioPort = 0, localControlPort = 0, localTimingPort = 0;
  sockaddr_in peerAddr{};
  uint16_t peerControlPort = 0, peerTimingPort = 0;

  std::atomic<bool> running{false};
  std::atomic<bool> playing{false};  // between RECORD and FLUSH/TEARDOWN
  pthread_t audioThread{}, controlThread{}, timingThread{}, playerThread{};
  bool audioThreadUp = false, controlThreadUp = false, timingThreadUp = false, playerThreadUp = false;

  // jitter buffer (protected by bufMutex)
  std::mutex bufMutex;
  pthread_mutex_t waitMutex;
  pthread_cond_t waitCond;
  std::vector<Slot> slots;
  bool haveExpected = false;
  uint16_t expectedSeq = 0;   // next seq the player will play
  uint16_t newestSeq = 0;     // highest seq received since RECORD/FLUSH
  bool haveNewest = false;
  uint16_t lastRxSeq = 0;
  bool haveLastRx = false;
  bool primed = false;        // player started output (buffer filled to latency once)
  bool everPrimed = false;    // primed at least once this stream: later primes use the shorter reprime cushion

  std::atomic<uint64_t> lastPacketMs{0};
  std::atomic<uint32_t> statPackets{0}, statResent{0}, statLost{0}, statLate{0}, statDecodeErr{0}, statSkipped{0};
  std::atomic<uint32_t> statConcealed{0}, statUnderrun{0};
  uint8_t* rxBuf = nullptr;
  int16_t* decodeBuf = nullptr;
};
