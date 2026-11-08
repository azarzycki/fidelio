// fidelio-audiod wire protocol — shared by the arbiter and every PCM producer.
// All multi-byte fields are little-endian (ARMv6 and the Linux host are both LE).
// C-compatible, fixed-width, explicitly packed so the layout is identical on both.
#ifndef FIDELIO_AUDIOD_PROTO_H
#define FIDELIO_AUDIOD_PROTO_H

#include <stdint.h>

#define AUDIOD_PROTO_VERSION 1
#define AUDIOD_DEFAULT_SOCK  "/var/mobile/fidelio/audiod.sock"

// Expected PCM format on the AUDIO channel. Producers that cannot deliver this
// must resample before sending; audiod does not resample in v1.
#define AUDIOD_SAMPLE_RATE 44100
#define AUDIOD_CHANNELS    2
#define AUDIOD_BITS        16

#define AUDIOD_NAME_MAX    32
#define AUDIOD_MAX_PAYLOAD (64 * 1024)   // reject frames larger than this (DoS guard)

enum AudiodMsgType {
  AUDIOD_MSG_HELLO = 1,   // client -> server, first message on the connection
  AUDIOD_MSG_AUDIO = 2,   // client -> server, interleaved S16LE stereo PCM
  AUDIOD_MSG_FLUSH = 3,   // client -> server, drop this source's buffered audio
  AUDIOD_MSG_STOP  = 4,   // client -> server, source stopping (optional)
  AUDIOD_MSG_META  = 5,   // client -> server, now-playing text (optional, v2)
  AUDIOD_MSG_VOLUME = 6   // client -> server, this source's playback volume (AudiodVolume)
};

#pragma pack(push, 1)

// Every message is this header followed by exactly `len` payload bytes.
typedef struct {
  uint32_t type;   // AudiodMsgType
  uint32_t len;    // payload length in bytes (0 for FLUSH/STOP)
} AudiodMsgHeader;

// HELLO payload. priority is a tiebreak only (higher wins an exact-time tie).
typedef struct {
  uint8_t  version;        // AUDIOD_PROTO_VERSION
  uint8_t  priority;       // 0 = normal
  uint8_t  channels;       // AUDIOD_CHANNELS
  uint8_t  bits;           // AUDIOD_BITS
  uint32_t sampleRate;     // AUDIOD_SAMPLE_RATE
  char     name[AUDIOD_NAME_MAX];  // nul-padded source id, e.g. "spotify" / "airplay"
} AudiodHello;

// VOLUME payload. The producer's volume (RAOP dB -> linear, Spotify 0..65535 -> linear) travels
// separately from the PCM so audiod sees full-scale samples for silence detection and applies the
// gain at the output. Sent after HELLO and on every change; 1.0 if never sent.
typedef struct {
  float linear;            // 0.0 .. 1.0 (IEEE 754 single, little-endian)
} AudiodVolume;

// META payload (v2). Nul-padded UTF-8; truncate on overflow.
typedef struct {
  char title[96];
  char artist[96];
  char album[96];
} AudiodMeta;

#pragma pack(pop)

// AUDIO payload is raw PCM bytes (no struct): len % (channels*bits/8) == 0.

#endif  // FIDELIO_AUDIOD_PROTO_H
