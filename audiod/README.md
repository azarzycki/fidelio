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
