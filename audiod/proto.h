// fidelio-audiod wire protocol — shared by the arbiter and every PCM producer.
// All multi-byte fields are little-endian (ARMv6 and the Linux host are both LE).
// C-compatible, fixed-width, explicitly packed so the layout is identical on both.
#ifndef FIDELIO_AUDIOD_PROTO_H
#define FIDELIO_AUDIOD_PROTO_H

#include <stdint.h>

#define AUDIOD_PROTO_VERSION 1   // producers that never read from the socket
#define AUDIOD_PROTO_VERSION_2 2 // B1.9: the producer reads server -> client messages (WELCOME, HWVOLUME)
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
  AUDIOD_MSG_VOLUME = 6,  // client -> server, this source's playback volume (AudiodVolume, 4 or 8 bytes)
  AUDIOD_MSG_WELCOME = 7, // server -> client (HELLO version >= 2 only), right after HELLO (AudiodWelcome)
  AUDIOD_MSG_HWVOLUME = 8 // server -> client (version >= 2 only), the hardware volume changed (AudiodHwVolume)
};

// B1.9: Spotify Connect's slider can drive the device's real output volume ("absolute" mode in
// audiod). A producer that wants that sends the RAW slider position alongside its software gain
// (the 8-byte AudiodVolume), and learns the current hardware level from WELCOME / HWVOLUME so the
// controller's slider shows the true level. Flags for AudiodVolume / AudiodWelcome / AudiodHwVolume:
#define AUDIOD_VOL_SLIDER   0x0001  // AudiodVolume: `slider` is valid (this source is slider-driven)
#define AUDIOD_VOL_APPLY    0x0002  // AudiodVolume: set the hardware to it (a user slider move); without it: report-only (startup echo)
#define AUDIOD_VOL_ABSOLUTE 0x0001  // AudiodWelcome / AudiodHwVolume: absolute mode is active, `slider` is the hardware level

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

// VOLUME payload, 8-byte form (B1.9). A version-1 server only knows the 4-byte form, so a producer
// sends this one only after WELCOME proved the server speaks v2. `linear` is still the software gain
// the producer would apply (used in software mode); `slider` the raw 0..65535 controller position.
typedef struct {
  float    linear;
  uint16_t slider;         // 0..65535 raw Connect slider
  uint16_t flags;          // AUDIOD_VOL_SLIDER | AUDIOD_VOL_APPLY
} AudiodVolume2;

// WELCOME payload (server -> client, after a version >= 2 HELLO).
typedef struct {
  uint8_t  version;        // AUDIOD_PROTO_VERSION_2
  uint8_t  reserved;
  uint16_t flags;          // AUDIOD_VOL_ABSOLUTE: hardware volume is driven by the slider on this server
  uint16_t slider;         // current hardware level as a slider position (valid with AUDIOD_VOL_ABSOLUTE)
  uint16_t reserved2;
} AudiodWelcome;

// HWVOLUME payload (server -> client, version >= 2): the hardware level changed (phone buttons,
// another client's slider, a clamp) -> the producer updates what it reports to its controller.
typedef struct {
  uint16_t slider;         // 0..65535
  uint16_t flags;          // AUDIOD_VOL_ABSOLUTE
} AudiodHwVolume;

// META payload (v2). Nul-padded UTF-8; truncate on overflow.
typedef struct {
  char title[96];
  char artist[96];
  char album[96];
} AudiodMeta;

#pragma pack(pop)

// AUDIO payload is raw PCM bytes (no struct): len % (channels*bits/8) == 0.

#endif  // FIDELIO_AUDIOD_PROTO_H
