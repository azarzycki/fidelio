# fidelio-audiod — the audio arbiter

## Why this exists

iPhone OS 3 gives one process at a time a working hardware audio route. Two
independent receivers (cspot for Spotify, fidelio-raop for AirPlay) each open
their own `AudioQueue` + `AudioSession` and override the route. They cannot hand
that route off: when one is killed it never calls `AudioSessionSetActive(false)`,
so `mediaserverd` leaves the route pinned to the dead owner and the next player
is **silent**. (Confirmed on hardware 2026-11-07.)

**Model B — a middle arbiter.** `fidelio-audiod` is the *only* process that ever
touches `AudioQueue` / `AudioSession` / the route. The receivers stop opening
audio hardware; they become route-agnostic **PCM producers** that stream to the
arbiter over a Unix socket. "Switching source" becomes "pick which PCM stream to
play" inside one process — no hardware handoff, so the silence bug cannot happen.

```
  cspot-ios3  ──(PCM over unix socket)──┐
                                        ├──►  fidelio-audiod  ──►  AudioQueue ──► dock
  fidelio-raop ─(PCM over unix socket)──┘        (arbiter)       (route-following)
```

## Transport

- Unix domain `SOCK_STREAM` at `/var/mobile/fidelio/audiod.sock` (override `--sock`).
- Server = audiod. One client connection = one source stream. audiod accepts many.
- All multi-byte fields **little-endian** (ARMv6 + Linux host are both LE).
- Framed messages: `struct MsgHeader { u32 type; u32 len; }` then `len` payload bytes.
  See `proto.h` for the wire contract (shared by audiod and every producer).

### Messages (client → server)
- `HELLO` — first message. `{ u8 version; u8 priority; u8 channels; u8 bits;
  u32 sampleRate; char name[32]; }`. Declares the source (e.g. "spotify",
  "airplay"). Expected format 44100 / 2 / 16. `priority` is a tiebreak only.
- `AUDIO` — interleaved S16LE stereo PCM, any length (a RAOP packet is 352
  frames = 1408 B; cspot sends its own chunking). Sent continuously *while the
  source has sound*. Absence of AUDIO is how audiod infers "this source is idle".
- `FLUSH` — drop this source's buffered audio (seek/track change).
- `STOP` — the source is intentionally stopping (optional; absence of AUDIO also
  counts as idle after `RELEASE_MS`).
- `META` *(optional, v2)* — `{ char title[96]; char artist[96]; char album[96]; }`
  so audiod can publish a unified now-playing status for the active source.

### Server → client
None required in v1. **audiod must keep reading AUDIO from every connected
client and discard the non-active ones**, so a producer never blocks on write.

## Arbitration — most-recent-active-source-wins, debounced

Per source, track `lastAudioMs` (last AUDIO received) and `producingSinceMs`
(when it most recently started producing after being idle). A source is
**producing** if `now - lastAudioMs < PRODUCING_GAP_MS` (120 ms).

State: `activeSource` (the one written to the AudioQueue) + `activeSinceMs`.

- **Takeover.** A non-active source that has been *continuously producing* for
  ≥ `TAKEOVER_MS` becomes active when either: there is no active source; or the
  active source has been quiet ≥ `RELEASE_MS`; or the active source has been
  active ≥ `HOLD_MS`. The newest source to cross the threshold wins
  ("most-recent-wins").
- **Min-hold.** After becoming active a source holds for ≥ `HOLD_MS` before a
  rival can steal it (unless it goes quiet) — this is the anti-flap debounce.
- **Release.** If the active source is quiet ≥ `RELEASE_MS` it is released;
  audiod feeds silence (and after `IDLE_STOP_MS` of nothing, stops the queue to
  drop the route cleanly). The next producing source then takes over after its
  own `TAKEOVER_MS`.
- **On switch:** flush audiod's sink ring and begin the new source (brief gap is
  fine for v1; a short fade can come later). A disconnect releases that source.

### Defaults (all `--flag`/env tunable)
| Param | Default | Meaning |
|-------|---------|---------|
| `TAKEOVER_MS`      | 400  | sustained audio a challenger needs before it can steal |
| `HOLD_MS`          | 1500 | min time the active source is protected from takeover |
| `RELEASE_MS`       | 1000 | quiet time before the active source is released |
| `PRODUCING_GAP_MS` | 120  | gap after which a source counts as not-producing |
| `IDLE_STOP_MS`     | 5000 | quiet time before the AudioQueue is stopped (route released) |

## Audio pipeline inside audiod

- Reuse `airplay/src/AudioOutput.h`, `AudioQueueSink.{h,cpp}` verbatim (route
  following + mediaserverd recovery already live there). audiod owns one sink.
- Only the active source's AUDIO is written to the sink; others are drained and
  dropped. The sink's existing ring absorbs socket jitter.
- `--pcm-out FILE` / `--no-audio`: a file sink (same as raop) so the arbitration
  logic can be **host-tested** on Linux without CoreAudio.

## Producer side — `SocketSink : AudioOutput`

- New `common` output that implements the existing `AudioOutput` interface
  (`write(pcm,frames)`, `flush()`, `stop()`), so cspot and raop switch their sink
  from `AudioQueueSink` to `SocketSink` with no other changes.
- On start: connect to the socket (retry w/ backoff), send `HELLO`, then
  `write()` → `AUDIO`, `flush()` → `FLUSH`. Reconnect + re-HELLO on broken pipe
  (audiod restarted). If audiod is absent after N retries, optional fallback to a
  local `AudioQueueSink` guarded by `--direct-audio` (off by default) so a
  producer can still be run standalone for debugging — never on in the daemon.

## Daemons

- `com.fidelio.audiod.plist` — RunAtLoad + KeepAlive, UserName mobile. Comes up
  first and owns the audio.
- cspot + raop daemons run as today but their sink is `SocketSink`; they
  retry-connect so a race with audiod startup self-heals.
- audiod publishes `/var/mobile/fidelio/active-source` (`spotify|airplay|none`)
  so the now-playing app can show whoever is actually playing.

## Host test (before any hardware deploy)

`audiod --pcm-out out.raw` + a python harness that opens two client connections,
sends HELLO + timed PCM bursts, and asserts the active-source transitions happen
at the TAKEOVER/HOLD/RELEASE boundaries. Lives in `audiod/hosttest/`.

## Build order

1. `proto.h` (this dir) — the wire contract. **Done: pins the ABI.**
2. `SocketSink` in `common/` + host harness; unit-test framing round-trip.
3. `audiod` arbiter (arbitration FSM + reuse AudioQueueSink); host-test switching.
4. Wire cspot + raop to `SocketSink`; host-test both producing into audiod.
5. Cross-build ARMv6; deploy audiod + the two producers; hardware switch test.
6. Daemonize; headless soak test (Spotify ↔ AirPlay, rapid and slow switches).
