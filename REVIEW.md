# Code review: cspot-ios3 command-line front end (`cli-src/`, cspot patch)

> **Integration note.** This review was made against the initial commit. Its fixes were landed on top of
> `AudioQueueSink: follow the system audio route and survive interruptions` (which rewrote the sink), so the
> three sink findings (C1, H1, H2) are implemented in the route-following sink: buffer ownership tracking
> with re-priming after reset/stop, real-PCM `bufferedSeconds()`, and a 2 s stall timeout that re-acquires the
> output through the route-change path. The library half of C2 (LoginBlob bounds checks) is in the updated
> `patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch`. Line numbers below refer to the original files.


Scope: `cli-src/main.cpp`, `CliPlayer.{h,cpp}`, `AudioQueueSink.{h,cpp}`, `StatusWriter.{h,cpp}`,
`ios3_trace.h`, `CMakeLists.txt`, and `patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch`
(read against the upstream tree it applies to). Nothing in this review ran on hardware. Every fix
was cross-built for ARMv6 / iPhone OS 3.1.2 SDK and passes `armv6-ios3-check`; the AudioQueue
sink was additionally exercised on the host against a small AudioToolbox mock (deleted after
use) that simulates the three possible buffer-callback behaviours of `AudioQueueReset`.

`file:line` references are to the files as of the initial commit (`76fc0cf`) unless marked
"(current)". "Fixed" names the commit subject on branch `review`.

## Critical

### C1. `flush()` can leave the AudioQueue with no buffers: silence and a hung player thread
`AudioQueueSink.cpp:94-101`, `:136`, `:145`

`flush()` calls `AudioQueueReset()` and assumes the queue comes back primed. The iPhone OS 3.1
`AudioQueue.h` says Reset "also invokes callbacks for any flushed buffers" and defines
`kAudioQueueErr_EnqueueDuringReset` (-66632): "During Reset, Stop, or Dispose, it is not
permitted to enqueue buffers." `fillBuffer()` (the callback) re-enqueued without looking at
the status. If the callbacks run inside Reset, all three enqueues fail silently, the queue is
left running with nothing queued, no further callback ever fires, the 128 KB ring fills in
0.74 s and `feedPCMFrames()` blocks on the condvar forever.

Scenario: play a track (queue running), then do anything that flushes: a SEEK, a FLUSH, or
any `PLAYBACK_START` (every Load/NEXT/PREV, `CliPlayer.cpp:60`). Result: audio stops for
good, the Spotify app still says playing, the status files say playing, and
`CliPlayer::disconnect()` at exit never returns (the player thread is stuck in the sink).

Verified: host mock in "callbacks during Reset, enqueue refused" mode: after `flush()`
0 buffers were queued and 6 enqueue errors were counted; a producer feeding 160 KB never
completed. The other two modes (enqueue accepted, callbacks after Reset returns) were fine,
which is presumably why the first track plays on the device: whether this bites depends on
the undocumented timing, so it needs the fix regardless.

Fixed: "AudioQueueSink: track buffer ownership and re-prime after flush/stop". Per-buffer
`enqueued[]` flags under the ring mutex, set only when `AudioQueueEnqueueBuffer` succeeds;
`flush()`/`start()` re-prime exactly the buffers the queue does not own. Passes the mock in
all three modes.

### C2. Unauthenticated LAN POST can reboot the phone or crash the daemon
`main.cpp:127-146`

The `/spotify_info` POST handler is reachable by anyone on the Wi-Fi with no credentials.
Four independent problems:

1. `body.resize(requestInfo->content_length)` with no bound (`:132`). `POST /spotify_info`
   with `Content-Length: 1000000000` makes a 116 MB device zero-fill 1 GB: the same jetsam /
   watchdog reboot the author hit with rev 1's 540 MB buffer, now triggerable remotely.
2. `queryMap[hd[i].name] = hd[i].value` (`:137`): civetweb's `mg_split_form_urlencoded`
   sets `value = NULL` for a key without `=` (civetweb.c, "mid key without value"). Assigning
   `NULL` to a `std::string` is `strlen(NULL)` with the static libc++. Body `a&b=c` crashes
   the process.
3. Missing fields go straight into `LoginBlob::loadZeroconfQuery`. `decodeBlob` slices
   `blob.begin()+16` / `blob.end()-20` of whatever decoded, and `decodeBlobSecondary` runs
   `for (int i = 0; i < l - 16; i++) blobData[l - i - 1] ^= ...` with `l` a `size_t`: a blob
   that base64-decodes to fewer than 16 bytes wraps `l - 16` and writes out of bounds until
   it segfaults (library code, `src/LoginBlob.cpp`). A LAN client can do that with a 40-byte
   request. Exceptions that escape (e.g. `std::length_error` from the reversed range) fell
   through to `BellHTTPServer`, which answers 404.
4. `mg_header hd[10]` (`:134`) silently drops fields past the tenth; the app chooses the
   order, and current apps already send nine.

Verified: by reading civetweb's parser and LoginBlob; the arithmetic in (3) is unambiguous.

Fixed (what the CLI can do): "main: validate the Zeroconf addUser POST before trusting it":
64 KB cap, full-read check (`mg_read` partial results were also ignored), field count from
a counting pass, NULL-safe values, mandatory `userName`/`blob`/`clientKey`, exceptions caught,
protocol error 103 returned. **Not fixed, needs the library:** `decodeBlob` must require
`blob.size() >= 36` and `decodeBlobSecondary` must require `blobData.size() >= 17`; that is two
lines for the patch (`src/LoginBlob.cpp`). Until then a crafted blob of the right outer size
can still crash the process.

## High

### H1. End of playlist is never reported: `bufferedSeconds()` can never drop below 0.05 s
`AudioQueueSink.cpp:103-109`, `CliPlayer.cpp:100`

`bufferedSeconds()` adds `kNumBuffers * kBufferBytes` (24 576 bytes = 0.139 s) whenever the
queue is running, whatever the buffers contain. `runTask()` turns `DEPLETED` into
`notifyAudioEnded()` + `status->audioEnded()` only when `bufferedSeconds() < 0.05`, and
`running` is never cleared once audio has started. So after the last track the player never
tells spirc that audio ended: the app keeps showing the track "playing" at its end, and
`status.json` stays `playing` with `position_ms` clamped to the duration.

Verified: arithmetic, and host mock: after draining, the original reported 0.139 s.

Fixed: "AudioQueueSink: count only real PCM in bufferedSeconds()" (per-buffer byte counts;
drains to 0.000 in the mock).

### H2. Player thread blocks forever when the queue stops on its own
`AudioQueueSink.cpp:145`

The only thing that frees ring space is a buffer completion. An audio-session interruption
(incoming call, alarm) or a mediaserverd restart (`kAudioQueueErr_QueueInvalidated`) stops
the queue; no completion ever comes; `feedPCMFrames()` waits with no timeout. The usual
interruption listener cannot be used as the code stands: `AudioSessionInitialize(NULL, ...)`
delivers it on the main run loop, and `main()` spins in `handlePacket()` instead of running
one. Same symptoms as C1 (silence, "playing" forever, exit hangs).

Fixed: "AudioQueueSink: never block the player thread forever on a stalled queue": 2 s
bounded wait, then the chunk is dropped, logged once per stall, and the documented
post-interruption recovery (`AudioSessionSetActive(true)`, re-prime, `AudioQueueStart`) is
attempted. The bound is verified (mock: resumes after 2 s); the recovery path is not
verified on hardware. The proper fix is to run a `CFRunLoop` on the main thread (move the
`handlePacket()` loop to a thread) and install an interruption listener.

### H3. Credentials file: world-readable, non-atomic, and a corrupt one crash-loops the daemon
`main.cpp:47-53`, `:214-220`, `:267`

`writeFile()` opens with the process umask (0644 on a stock setup): the file is the reusable
auth blob, i.e. the account. It is not atomic, errors are ignored, and the device reboots
under memory pressure. `LoginBlob::loadJson()` does
`cJSON_GetObjectItem(root, "authType")->valueint` with no NULL checks, so a truncated or
garbled file segfaults at every start; under launchd that is a crash loop until someone
deletes the file by hand.

Fixed: "main: write the credentials file privately and atomically, validate on load"
(0600 via tmp+rename, write errors logged; the file is parsed with cJSON and only handed to
`loadJson` if it has the three fields, otherwise Zeroconf login runs and rewrites it).

### H4. Later addUser POSTs rewrite the live LoginBlob from another thread
`main.cpp:139`, `src/Session.cpp` (`authBlob = blob` in `authenticate`)

The server and the mDNS advertisement stay up for the life of the process. Every later POST
ran `loadZeroconfQuery()` on the same `LoginBlob` on the civetweb thread, with no lock. That
blob is `Session::authBlob`, used by `MercurySession::reconnect()` on the Mercury thread for
every reconnect. A second POST (the app re-selecting the device, a second phone, anyone on
the LAN) races those reads and, with a different account, replaces the credentials that the
next reconnect will use. Nothing here implements a user switch, so the work was pure risk.

Fixed: "main: ignore Zeroconf addUser once a login has been accepted" (answers ERROR-OK,
leaves the blob alone).

## Medium

### M1. `trackIds` grows without bound
`CliPlayer.cpp:32`

The fork gives every queue entry a unique identifier (`<fileId>#<seq>`), so the
`unordered_map<size_t, std::string>` gains a node (~100 bytes) per track played and never
loses one: ~50 KB/day for a dock playing around the clock, in a 3 MB process. Fixed:
"CliPlayer: bound the trackHash -> identifier map" (4-entry vector; only tracks with chunks
still in the 0.75 s buffer need an entry).

### M2. Cover fetch: no status check, no size bound, three copies, and it stalls the writer
`StatusWriter.cpp:209-229`, `:290-327`

`HTTPClient::Response` does not expose the HTTP status, so a 404/403 page of 100+ bytes was
saved as `cover.jpg`. `Response::readRawBody()` allocates `Content-Length` bytes in one go
(server-controlled; TLS is unverified, see accepted risks), then `bytes()` returned the
vector by value and the code copied it again into a `std::string`. Meanwhile the writer
thread, which is the only thing that updates the status files, sat on the new title until
the TLS handshake and transfer finished (seconds on a 412 MHz ARM11): the dock kept showing
the previous track. Fixed: "StatusWriter: check the cover response, bound it, stop copying
it" (requires `image/*` and 100 B..1 MB before reading, writes the response's own buffer,
publishes the new track before the fetch with the cover cleared).

Still open: bell's `TLSSocket` sets no socket timeouts (`PlainConnection` does;
`TLSSocket::open` does not). A Wi-Fi drop in the middle of the transfer blocks the writer
thread indefinitely (status frozen, "(stale)" on the dock, and `~StatusWriter` joins that
thread at exit). Recommend adding `SO_RCVTIMEO`/`SO_SNDTIMEO` on `server_fd.fd` in
`TLSSocket::open` to the patch; it also protects the CDN/token paths.

### M3. Patch: `SO_REUSEPORT` on the listener masks a still-running previous instance
`patches/...0001-armv6-ios3.patch`, civetweb `set_ports_option`

On XNU, `SO_REUSEADDR` (which civetweb already sets) is what allows rebinding over
`TIME_WAIT` connections. `SO_REUSEPORT` goes further: it lets a second process bind the same
TCP port while the first still listens. The EADDRINUSE seen "within ~60 s of a restart" is
therefore most likely the old process still being alive (its exit can hang, see C1/H2), and
the patch now lets two instances coexist: both advertise, one gets the connection, the other
holds the Spotify session. Recommend dropping the `SO_REUSEPORT` hunk and relying on the
existing 2-minute retry loop, which was written for exactly this case.

### M4. Status files rewritten every second even when idle
`StatusWriter.cpp:290-305`

The timed wait fires every second regardless of state, so an idle dock renames two files
per second, 172 800 journaled writes a day, forever, on 2007 NAND. The app treats the file as
stale after 15 s, so the heartbeat can be 5-10 s when nothing is playing. Not fixed: the
interval is part of the contract with the app (`APP.md`), so it is the author's call; a
`clockRunning ? 1 s : 10 s` wait in `threadLoop()` is the whole change.

### M5. `AudioQueueStart` failures were retried and traced on every chunk
`AudioQueueSink.cpp:78-86`

`feedPCMFrames()` calls `start()` per chunk while not running; `start()` wrote two flushed
trace lines each time. A daemon without a usable audio session would write ~86 lines/s to
the log on flash for as long as playback is attempted. Fixed: "AudioQueueSink: do not log
every retried AudioQueueStart".

## Low

- **L1. `nowMs()` timebase initialised lazily from several threads** (`StatusWriter.cpp:73`):
  first caller races; a reader can see `denom != 0` before `numer`. Fixed (initialised in
  the constructor), together with **L2**: `pthread_join()` on an uninitialised `pthread_t` when
  `pthread_create` failed (`:87`, `:99`). "StatusWriter: initialise the timebase once, join
  only a started thread".
- **L3. SEEK while a track is loading is lost** (`StatusWriter.cpp:186`): `TRACK_INFO` re-bases
  the clock on `pendingStartMs`, which a SEEK did not update, although the audio really starts
  at the seek position. The comment at `:160` ("starts when the audio really reaches the sink")
  was also wrong: spirc sends `PLAY_PAUSE(false)` right after `PLAYBACK_START` and that starts
  the clock; `TRACK_INFO` only re-bases it (so the position visibly jumps back by up to ~1 s on
  each track start; cosmetic, inherent in where TRACK_INFO is generated). Fixed:
  "StatusWriter: keep a seek made while a track is loading".
- **L4. Pause keeps the audio hardware running** (`CliPlayer.cpp:96`): pausing stops feeding,
  the queue keeps playing silence (callbacks at 21 Hz, hardware on). `AudioQueuePause`/
  `AudioQueueStart` around `isPaused` would save battery; not changed.
- **L5. `/close` is unauthenticated** (`main.cpp:123`): anyone on the LAN stops the player.
  Inherited from upstream; acceptable on a trusted LAN, worth a note in the README.
- **L6. `--port`/`--bitrate` unvalidated** (`main.cpp:78-79`): `atoi` garbage gives port 0
  (advertised as such, unusable) and any non-96/160/320 bitrate silently means 96.
- **L7. `MercurySession::getCountryCode()` is read by the writer thread while the main thread
  assigns it** (`StatusWriter.cpp:242`): a formal data race on a 2-byte `std::string`; SSO
  makes it benign in practice.
- **L8. Invalid UTF-8 in metadata would make `status.plist` unparseable** (`StatusWriter.cpp:43`):
  `xmlEscape` strips control characters but passes bytes through; Spotify metadata is UTF-8,
  so this is defensive only.
- **L9. The account e-mail goes to the log** (`main.cpp:140`): `loadZeroconfQuery done (user
  '...')` on a log file that is meant to survive and be sent around.
- **L10. Trace lines from several threads interleave** (`ios3_trace.h:16-17`): three separate
  `fprintf` calls per line.
- **L11. Comments / strings:** `CMakeLists.txt:6` says "vorbis only" while AAC and MP3 are ON
  (the reason is in CSPOT.md); `main.cpp:189` still says "rev3".
- **L12. Patch nits:** the civetweb `USE_STACK_SIZE` fix moves `pthread_create` into the
  `#else`, so the (irrelevant here) `__ZEPHYR__` branch now has no thread creation at all;
  `WrappedSemaphore(int maxVal)` ignores `maxVal` (so did the dispatch version);
  `BELL_HTTP_NUM_THREADS="1"` means one slow or idle client blocks addUser for civetweb's
  30 s request timeout. Upstream `CentralAudioBuffer::writePCM` computes
  `size() - capacity()` (unsigned wrap) for its full check; it only works because every write
  is exactly one chunk, which the 32-chunk configuration preserves.

## Accepted risks (documented, not changed)

- **TLS certificate verification is off** in bell (no X.509 bundle is installed). Every
  HTTPS endpoint (apresolve, token, CDN audio, cover) can be impersonated on the LAN. The
  cover path is bounded (M2) but a hostile JPEG still reaches iOS 3's ImageIO in the app.
  Shipping a bundle through `X509Bundle::init()` is the fix when there is room for it.
- **Secrets on the command line** (`-p`, `--client-secret`) are visible in `ps` to every
  process on the device, and `-p` keeps the password in `argv` for the life of the process.
  Use `-c` (now 0600) and let Zeroconf do the login; the client secret has no such path.
- **The Zeroconf endpoint trusts the LAN** by protocol design (so does every Spotify Connect
  receiver); the CLI now limits the damage to "wrong credentials rejected".

## Overall assessment

The port itself is careful where the author looked (memory budget, thread stacks, log
flushing, atomic status files) and the documentation is unusually honest about what was not
run. The parts that were not exercised on hardware are where the problems are: the seek /
track-change path in the AudioQueue sink (C1, H1, H2) could take the whole player down after
the first track and then hang exit, and the Zeroconf endpoint (C2, H4) was written as if the
LAN were friendly, which on a device that reboots under memory pressure means a one-line
remote reboot. Those are fixed on this branch except for the two bounds checks that belong in
`LoginBlob` (library). The remaining items are policy (idle write rate, `SO_REUSEPORT`) and
hygiene. Before relying on the branch: test seek, NEXT/PREV and playlist end on the device,
and an incoming call during playback.
