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
  printf(fails ? "\n%d FAILURES\n" : "\nall framing checks passed\n", fails);
  return fails ? 1 : 0;
}
