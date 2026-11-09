// Round-trip and robustness test for the audiod framing (common/AudiodFraming.*):
// messages written with writeFrame() through a socketpair are parsed back byte-exact even when
// delivered in odd-sized pieces; an oversized or unknown frame poisons the parser.
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "AudiodFraming.h"

using namespace audiod;

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); fails++; } } while (0)

int main() {
  int sp[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 1; }
  AudiodHello h = makeHello("spotify", 3);
  CHECK(sizeof(AudiodHello) == 40 && sizeof(AudiodMsgHeader) == 8 && sizeof(AudiodMeta) == 288, "packed struct sizes 8/40/288");
  CHECK(writeFrame(sp[0], AUDIOD_MSG_HELLO, &h, sizeof h), "write HELLO");
  std::vector<uint8_t> pcm(1408);
  for (size_t i = 0; i < pcm.size(); i++) pcm[i] = (uint8_t)(i * 7);
  CHECK(writeFrame(sp[0], AUDIOD_MSG_AUDIO, pcm.data(), pcm.size()), "write AUDIO 1408");
  CHECK(writeFrame(sp[0], AUDIOD_MSG_FLUSH, nullptr, 0), "write FLUSH");
  std::vector<uint8_t> big(AUDIOD_MAX_PAYLOAD, 0x5a);
  CHECK(writeFrame(sp[0], AUDIOD_MSG_AUDIO, big.data(), big.size()), "write AUDIO at the 64 KB limit");
  CHECK(!writeFrame(sp[0], AUDIOD_MSG_AUDIO, big.data(), big.size() + 1), "writer refuses 64 KB + 1");

  // Read everything, feed it to the parser in prime-sized pieces.
  std::vector<uint8_t> all;
  uint8_t buf[4096];
  shutdown(sp[0], SHUT_WR);
  for (;;) { ssize_t n = read(sp[1], buf, sizeof buf); if (n <= 0) break; all.insert(all.end(), buf, buf + n); }
  FrameParser p;
  std::vector<Message> got;
  for (size_t i = 0; i < all.size(); i += 7) {
    p.feed(all.data() + i, i + 7 <= all.size() ? 7 : all.size() - i);
    Message m;
    while (p.next(m)) got.push_back(m);
  }
  CHECK(p.error().empty(), "no parser error");
  CHECK(got.size() == 4, "4 messages parsed");
  AudiodHello h2; std::string why;
  CHECK(got.size() > 0 && parseHello(got[0], h2, why) && helloName(h2) == "spotify" && h2.priority == 3, "HELLO round-trip");
  CHECK(got.size() > 1 && got[1].type == AUDIOD_MSG_AUDIO && got[1].payload == pcm, "AUDIO payload byte-exact");
  CHECK(got.size() > 2 && got[2].type == AUDIOD_MSG_FLUSH && got[2].payload.empty(), "FLUSH");
  CHECK(got.size() > 3 && got[3].payload.size() == AUDIOD_MAX_PAYLOAD, "64 KB AUDIO");

  // DoS guard: a header announcing 1 GB must poison the parser without allocating.
  FrameParser bad;
  AudiodMsgHeader hb = { AUDIOD_MSG_AUDIO, 1u << 30 };
  bad.feed((const uint8_t*)&hb, sizeof hb);
  Message m;
  CHECK(!bad.next(m) && !bad.error().empty() && bad.pending() == sizeof hb, "oversized header rejected without buffering");
  FrameParser unk;
  AudiodMsgHeader hu = { 99, 0 };
  unk.feed((const uint8_t*)&hu, sizeof hu);
  CHECK(!unk.next(m) && !unk.error().empty(), "unknown type rejected");
  // HELLO validation
  AudiodHello hbad = makeHello("x"); hbad.sampleRate = 48000;
  Message mb; mb.type = AUDIOD_MSG_HELLO; mb.payload.assign((uint8_t*)&hbad, (uint8_t*)&hbad + sizeof hbad);
  CHECK(!parseHello(mb, h2, why) && why.find("unsupported format") == 0, "HELLO with 48 kHz rejected");
  // B1.9: protocol v2 (reads server messages) is accepted, v3 is not; the volume comes in two sizes
  CHECK(sizeof(AudiodVolume) == 4 && sizeof(AudiodVolume2) == 8 && sizeof(AudiodWelcome) == 8 && sizeof(AudiodHwVolume) == 4, "B1.9 struct sizes 4/8/8/4");
  AudiodHello hv2 = makeHello("spotify", 0, AUDIOD_PROTO_VERSION_2);
  Message m2; m2.type = AUDIOD_MSG_HELLO; m2.payload.assign((uint8_t*)&hv2, (uint8_t*)&hv2 + sizeof hv2);
  CHECK(parseHello(m2, h2, why) && h2.version == 2, "HELLO v2 accepted");
  hv2.version = 3; m2.payload.assign((uint8_t*)&hv2, (uint8_t*)&hv2 + sizeof hv2);
  CHECK(!parseHello(m2, h2, why) && why.find("protocol version 3") == 0, "HELLO v3 rejected");
  float lin; uint16_t sl, fl;
  AudiodVolume v4 = { 0.25f };
  Message mv; mv.type = AUDIOD_MSG_VOLUME; mv.payload.assign((uint8_t*)&v4, (uint8_t*)&v4 + sizeof v4);
  CHECK(parseVolume(mv, lin, sl, fl) && lin == 0.25f && sl == 0 && fl == 0, "VOLUME 4-byte form: gain only, no slider");
  AudiodVolume2 v8 = { 1.0f, 52428, AUDIOD_VOL_SLIDER | AUDIOD_VOL_APPLY };
  mv.payload.assign((uint8_t*)&v8, (uint8_t*)&v8 + sizeof v8);
  CHECK(parseVolume(mv, lin, sl, fl) && lin == 1.0f && sl == 52428 && fl == (AUDIOD_VOL_SLIDER | AUDIOD_VOL_APPLY), "VOLUME 8-byte form: gain + raw slider + flags");
  v8.linear = 7.0f / 0.0f; mv.payload.assign((uint8_t*)&v8, (uint8_t*)&v8 + sizeof v8);
  CHECK(parseVolume(mv, lin, sl, fl) && lin == 1.0f, "VOLUME gain clamped to 1");
  v8.linear = 0.0f / 0.0f; mv.payload.assign((uint8_t*)&v8, (uint8_t*)&v8 + sizeof v8);
  CHECK(parseVolume(mv, lin, sl, fl) && lin == 0.0f, "VOLUME NaN gain -> 0");
  mv.payload.resize(6);
  CHECK(!parseVolume(mv, lin, sl, fl), "VOLUME of 6 bytes rejected");
  // server -> client messages round-trip through the same framing
  int sp2[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp2) == 0, "socketpair for server messages");
  AudiodWelcome w = { AUDIOD_PROTO_VERSION_2, 0, AUDIOD_VOL_ABSOLUTE, 19660, 0 };
  AudiodHwVolume hw = { 26214, AUDIOD_VOL_ABSOLUTE };
  CHECK(writeFrame(sp2[0], AUDIOD_MSG_WELCOME, &w, sizeof w) && writeFrame(sp2[0], AUDIOD_MSG_HWVOLUME, &hw, sizeof hw), "write WELCOME + HWVOLUME");
  shutdown(sp2[0], SHUT_WR);
  FrameParser ps;
  for (;;) { ssize_t n = read(sp2[1], buf, sizeof buf); if (n <= 0) break; ps.feed(buf, (size_t)n); }
  Message w1, w2;
  bool okw = ps.next(w1) && ps.next(w2) && w1.type == AUDIOD_MSG_WELCOME && w1.payload.size() == sizeof w && w2.type == AUDIOD_MSG_HWVOLUME && w2.payload.size() == sizeof hw;
  AudiodWelcome wp; AudiodHwVolume hwp;
  if (okw) { memcpy(&wp, w1.payload.data(), sizeof wp); memcpy(&hwp, w2.payload.data(), sizeof hwp); }
  CHECK(okw && ps.error().empty() && wp.flags == AUDIOD_VOL_ABSOLUTE && wp.slider == 19660 && hwp.slider == 26214, "WELCOME (absolute, slider 19660) and HWVOLUME (26214) parsed back");
  printf(fails ? "\n%d FAILURES\n" : "\nall framing checks passed\n", fails);
  return fails ? 1 : 0;
}
