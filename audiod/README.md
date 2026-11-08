# audiod — the Fidelio audio arbiter

The one process that owns the iPhone's audio hardware. Spotify (cspot) and
AirPlay (fidelio-raop) stream PCM to it over a Unix socket; audiod plays the
most-recently-active source (debounced) so switching needs no hardware route
handoff. See **DESIGN.md** for the full design and **proto.h** for the wire
contract. Implementation lands component-by-component per the build order in
DESIGN.md.

## Milestone A (host-only) — what is here

- `../common/AudiodFraming.*` — wire framing shared by both sides: `writeFrame()`,
  a streaming `FrameParser` (rejects unknown types and payloads > `AUDIOD_MAX_PAYLOAD`
  without buffering), `makeHello()` / `parseHello()`.
- `../common/SocketSink.*` — an `AudioOutput` (airplay/src/AudioOutput.h) that
  streams to audiod: lazy connect with backoff, HELLO, AUDIO/FLUSH/STOP, reconnect
  + re-HELLO on a broken pipe (frames are dropped, not buffered, while disconnected).
- `src/Arbiter.*` — the pure state machine (no I/O): PRODUCING_GAP / TAKEOVER /
  HOLD / RELEASE / IDLE_STOP, most-recent-producing-source wins, events out.
- `src/Server.*`, `src/main.cpp` — Unix-socket server, poll loop, one parser per
  client, plays only the active source into an `AudioOutput` (FileOutput /
  NullOutput for now; AudioQueue arrives with Milestone B), writes
  `<status-dir>/active-source`, optional `--event-log`.

### Build and test on the host

    cmake -S audiod -B audiod/build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja -C audiod/build-host
    ./audiod/build-host/framing_test
    python3 audiod/hosttest/test_arbiter.py audiod/build-host/fidelio-audiod

`test_arbiter.py` drives two fake sources with timed PCM bursts and asserts that
every transition lands on the TAKEOVER / HOLD / RELEASE / IDLE_STOP boundary, that
a rapidly flapping rival never steals, and that the played PCM is exactly the
active sources' audio. `socketsink_producer NAME SOCK SAMPLE MS` streams through
`SocketSink` for reconnect tests (kill and restart audiod underneath it).

All timers are flags (`--takeover-ms` …) or env (`AUDIOD_TAKEOVER_MS` …); see
`fidelio-audiod --help`.

## Milestone B1 (play on hardware, real producers)

- `fidelio-audiod` with no `--pcm-out`/`--no-audio` plays through the AudioQueue
  (`airplay/src/AudioQueueOutput.h` over the shared route-following `AudioQueueSink`);
  `--audio-route`, `--on-route-change` as in cspot. The idle stop (`IDLE_STOP_MS`) stops
  the queue; the next active source restarts it.
- `cspot-ios3` and `fidelio-raop` stream to audiod by default (`--audiod-sock PATH`, env
  `AUDIOD_SOCK`); `--direct-audio` keeps the old local AudioQueue for standalone debugging.
  `cspot-ios3 --sink-test N` plays N s of a 440 Hz tone through whichever sink and exits:
  the quickest way to prove the audiod path on the device without Spotify.
- Pacing: both producers push as fast as their sink accepts and rely on it blocking (that is
  how the AudioQueue ring paced them). `SocketSink::write()` therefore blocks until the
  stream is at most 500 ms (`kLeadMs`) ahead of the monotonic clock; audiod keeps each
  source's latest second in a ring and pumps the active one into the output only while the
  output holds < 0.5 s, so the arbiter loop never blocks on the hardware and a takeover
  starts with the new source's lead already in hand. The AudioQueue sink re-primes 200 ms
  after an underrun instead of playing a near-empty ring with holes.
- Volume: proto v1 has no VOLUME message; `SocketSink::setVolume()` is applied as gain
  before sending (raop's dB volume, cspot's 0..65535).
- `hosttest/test_producers.py AUDIOD RAOP [CSPOT]`: the host-built fidelio-raop receives a
  scripted RAOP session (`airplay/hosttest/raop_sender.py`) into audiod and the played PCM is
  checked byte-exact (with the -15 dB gain) around the FLUSH; cspot's `--sink-test` tone is
  checked for length, frequency and the STOP release. `AUDIOD_TRACE_RING=1` traces the ring.
- Known tuning interactions (device decisions, not code defects): after a FLUSH raop
  re-primes its full cushion (1200 ms by default), longer than `RELEASE_MS` (1000), so audiod
  releases the source and takes it over again 400 ms into the new audio; raise `RELEASE_MS`
  or shorten the post-FLUSH re-prime if that matters. A TEARDOWN's flush drops the ≤ 500 ms
  lead already in audiod. A paused source plays its ≤ 500 ms lead out (the local queue used
  to play 740 ms).

## B1.1 (fix round after the first hardware run)

- **Recovery invariant** (`airplay/src/AudioQueueSink.*`, shared): while audio is being fed and
  `stop()` has not been called, the queue is (re)started within ~1 s whatever interruption-begin /
  route-change / missing interruption-end sequence arrives: `wantPlaying` intent, a route change
  ends a stuck interruption, `tick()` (audiod's loop) runs a watchdog that clears an interruption
  older than 1 s and retries `AudioQueueStart` every 500 ms while it fails. Host-tested against a
  mock AudioToolbox: `hosttest/aqsink_test.cpp` (`hosttest/mock/`), replaying the device trace.
- **Silence detection**: a frame counts as sound only if its peak |sample| exceeds
  `--silence-threshold` (512; env `AUDIOD_SILENCE_THRESHOLD`; 0 = any frame). `producing` and the
  release use the last *loud* frame, so a paused AirPlay sender streaming digital silence is idle:
  it cannot steal, and if it is active it is released after `RELEASE_MS`.
- **VOLUME message** (proto type 6, `AudiodVolume{float linear}`): the producer's volume no longer
  scales the PCM in SocketSink; audiod applies it at the output (AudioQueue volume parameter;
  FileOutput scales for the host tests) so the silence threshold means the same at every volume.
  Sent after HELLO (if not 1.0) and on every change.
- **Tuning defaults**: `producingGapMs` 1000 (was 120), `releaseMs` 1500 (was 1000, covers raop's
  post-FLUSH re-prime), SocketSink lead 750 ms (was 500; producers `--audiod-lead-ms`, env
  `AUDIOD_LEAD_MS`; audiod `--lead-ms` must match what a takeover starts with).
- **Status ownership**: producers write `status-spotify.*` / `status-airplay.*` and
  `cover-spotify.jpg` / `cover-airplay.jpg`; the now-playing app (1.2) reads `active-source` and
  shows only that source (see `nowplaying/APP.md`).
