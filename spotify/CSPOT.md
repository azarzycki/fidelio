# cspot on ARMv6 iPhone OS 3.1.3: first cross-build

**Result: a linked, fake-signed command-line player (`cspot-ios3`, 2.1 MB stripped) that passes every static check.**

**Hardware status (reported from the device, iOS 3.1.3 / ARM11, 2026-11-06):** the toolchain output runs. `hello_c`, `hello_cpp`, `hello_cxx17`, `hello_objc` pass; AudioQueue plays; dns_sd discovery works; `tls_test` completes a full TLS 1.3 handshake to Spotify; `cspot-ios3` advertises over Bonjour and serves `GET /spotify_info` (valid JSON with the DH public key, HTTP 200). Login, decoding and playback through to the speaker are still unreported.

**Revision 2 (restart robustness):** restarting `cspot-ios3` within ~60 s made bell/civetweb fail to rebind the Zeroconf port and abort with an uncaught `CivetException null`. Three changes, all in the patch / `cli-src`:
1. `bell/external/civetweb/civetweb.c`: the listener also sets `SO_REUSEPORT` on Apple (civetweb already set `SO_REUSEADDR`). Accepted sockets inherit it, so a restart can rebind while the previous instance's connections sit in TIME_WAIT.
2. `bell/main/io/BellHTTPServer.cpp`: civetweb's internal errors now go to the bell log through a `log_message` callback (before, "cannot bind to 7864: 48 (Address already in use)" was dropped and only "null" reached the exception). Optional `BELL_HTTP_NUM_THREADS` caps the worker pool; the CLI sets 4 instead of civetweb's default 50 (50 × 512 KB stacks on a 128 MB device).
3. `cli-src/main.cpp`: the Zeroconf server start is retried every 2 s for up to 2 minutes with the civetweb message logged, and exits with status 3 only after that.
Not verified on the device yet: if the log still shows errno 48 for the whole minute after a restart, the previous process is probably still alive; if it shows a different errno, send me the line.

**Revision 3 (device reboot on connect, 2026-11-07).** Observed with rev 1: tapping the device in the Spotify app stalls the whole phone within 10–20 s and it reboots with no crash log (jetsam/watchdog pattern); only "Server listening"/"Advertised" ever reached the log because stdout was block-buffered.

*Root cause found by reading, not yet confirmed on the device:* `CliPlayer` constructed `bell::CentralAudioBuffer(128 * 1024)`, copied from upstream's CLI. That argument is a **chunk count**, each chunk is `sizeof(AudioChunk)` = 4,118 bytes, and the constructor allocates `chunks × 4118` as a zero-filled `std::vector` — **≈540 MB, written through**. `CliPlayer` is created right after `authenticate()`, i.e. seconds after the tap, which matches the timing. On a swapless 116 MB device that allocation thrashes the VM until the watchdog reboots it. Upstream's CLI only survives this on desktops.

Changes in rev 3:
1. **Logs that survive a reboot.** `setvbuf(stdout, NULL, _IOLBF, 0)`, `setvbuf(stderr, NULL, _IONBF, 0)` at the top of `main()`; bell's logger now `fflush(stdout)` after every line (`BellLogger.h`). Trace lines (`T [rss NNNNK] …`, `cli-src/ios3_trace.h`, resident size from `task_info(TASK_BASIC_INFO)`) at: start, AudioQueueSink created, zeroconf server + mDNS registered, zeroconf GET, zeroconf POST received / body / `loadZeroconfQuery done`, `connectWithRandomAp begin/done`, `authenticate begin/done, authData len=N`, `SpircHandler created`, `startTask done`, `CliPlayer created` (or `no-audio mode`), `entering handlePacket loop`, and in the sink at `AudioQueueNewOutput begin/->status` and `AudioQueueStart begin/->status`. Every trace is flushed.
2. **Memory.** `CentralAudioBuffer(32)`: 132 KB, 0.75 s of audio. AudioQueue sink: ring 128 KB (0.74 s) and 3 × 8 KB queue buffers (was 256 KB + 4 × 16 KB). civetweb `num_threads` 1. Thread stacks: bell Tasks get explicit `pthread_attr_setstacksize` = max(128 KB, 4 × the ESP figure), capped at 512 KB, i.e. 128 KB for mercury/track-queue/player-output and 192 KB for `cspot_player` (it runs tremor, which `alloca`s per Vorbis block); civetweb master and worker 128 KB via `USE_STACK_SIZE` (whose code path had an undeclared-variable bug in this fork; fixed). Default bitrate is now 96. Vorbis is the only codec cspot uses; MP3/AAC decoders are still linked because bell's codec framework needs their headers, but they allocate nothing until used. Nothing is stubbed; everything is still static.
   *Expected peak resident size:* own heap well under 2 MB (audio ≈ 300 KB, two or three mbedTLS sessions ≈ 40 KB each, tremor state 100–200 KB, cspot/nanopb/cJSON small, civetweb ≈ 100 KB), thread stacks touched ≈ 0.3 MB, binary text ≈ 1.5 MB paged in; plus the shared-library pages this process touches (libSystem, CoreFoundation, AudioToolbox), which `resident_size` on iOS 3 counts too. Roughly **6–10 MB resident**, versus the ≈540 MB rev 1 tried to write.
3. **`--no-audio`.** No AudioQueue sink and no player thread. The full cspot pipeline still runs (track queue, CDN fetch, Vorbis decode); the data callback discards the PCM and every Spirc event is logged with its payload. Use it to test Zeroconf login + AP authentication + token fetch in isolation from the audio allocations.

**Revision 4 (status files for the dock display, 2026-11-07).** Hardware status after rev 3 (your report): no reboot, peak RSS ≈ 2.7 MB, full chain works — Zeroconf POST → login → AP → Mercury → token → CDN → Vorbis → AudioQueue → speaker; runs as a launchd daemon as `mobile`.
New in rev 4 (`cli-src/StatusWriter.{h,cpp}`, plus two small cspot/bell changes):
- cspot-ios3 writes `<status-dir>/status.json` **and** `status.plist` (same keys; the iOS 3 app reads the plist because Foundation has no JSON parser before iOS 5), atomically (`.tmp` + `rename`), on every Spirc event / state change and once a second while playing. Field contract and semantics: see `APP.md` in the app bundle. Options: `--status-dir DIR` (default `/var/mobile/fidelio`), `--no-status`.
- Position: Spirc position (PLAYBACK_START / SEEK / PLAY_PAUSE) plus a `mach_absolute_time` clock that runs while playing, frozen while paused, restarted on track change (the clock starts at `TRACK_INFO`, which the fork sends when the track's audio reaches the sink).
- Cover art: **implemented**. `TrackInfo.imageUrl` is `https://i.scdn.co/image/<hex>` from `album.cover_group.image[0]`; the writer thread fetches it with bell's `HTTPClient::get` (HTTPS, mbedTLS) and saves `cover.jpg` atomically; `"cover"` is `"cover.jpg"` or `""`. One extra TLS connection per track change.
- `tls`: `TLSSocket::lastNegotiated()` (new static in bell, set after every successful handshake) → e.g. `TLSv1.3 TLS1-3-CHACHA20-POLY1305-SHA256`. Note this is the HTTPS side (apresolve/token/CDN/cover); the AP connection itself is Spotify's Shannon cipher, not TLS.
- `ap`: `Session::lastApAddress` (new member, set in `connectWithRandomAp`). `country`: `MercurySession::getCountryCode()`, polled at write time because it arrives on the Mercury thread after `startTask()`.
- The writer runs on its own 192 KB-stack pthread; memory cost ≈ the cover JPEG (~30–60 KB) plus one TLS session while fetching.
Patch now has four commits (13 files, +161/−4): `Session.h/.cpp` (`lastApAddress`), `TLSSocket.h/.cpp` (`lastNegotiated`).

**Revision 5 (2026-11-07, one key).** `status.json`/`status.plist` now carry `"source": "spotify"` (first key) so the dock app can show which input is live next to the AirPlay receiver (`fidelio-raop`, which writes `"source": "airplay"` to the same files). No other change; same patch (four commits), only `cli-src/StatusWriter.cpp` and the start banner (`rev5`) differ.

Suggested test order with rev 3: `./cspot-ios3 --no-audio -b 96 2>&1 | tee log.txt` (also pass the client id/secret), tap the device, then read the last `T [rss …]` line; then the same without `--no-audio`.

Read §5 before deploying.

## 1. Which cspot
| Candidate | Commit | State |
|---|---|---|
| feelfreelinux/cspot (upstream) | `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26` (2024-07-12), bell `e8373736` (2024-06-27) | Last touched mid-2024. Uses the login5 protobuf endpoint to get a Web API token, which Spotify has since broken for third-party clients. Its ApResolve/AccessKeyFetcher are the older versions. |
| **squeezelite-esp32 `components/spotify/cspot`** (chosen) | squeezelite-esp32 `ee1720c7556ffec5f43329aadd47c20546bdbb79` (branch `master-v4.3`, 2026-10-15) | A maintained fork of the same tree with 20+ files changed: token via `accounts.spotify.com/api/token` with a developer client id/secret, reconnect loop hardening, TrackQueue race fixes (`notifyTrackReached`, per-entry identifiers), `cJSON_ParseWithLength` fixes, audio-chunk read error handling. Same bell submodule lineage. |

The squeezelite copy is the one in better shape; the upstream repo's CLI target was used as the template for the player program. The baseline tree was committed as-is to a local git repo, so `cspot-squeezelite-ee1720c-0001-armv6-ios3.patch` applies with `git am` / `patch -p1` on top of that directory.

Consequence of the fork's token method: **you need a Spotify developer application client id and secret** (`--client-id`/`--client-secret`); without them `AccessKeyFetcher` cannot get a bearer token, `QueuedTrack::stepLoadCDNUrl` cannot resolve CDN URLs, and nothing plays. squeezelite-esp32 documents the same requirement.

## 2. What was built
```
cspot-armv6-ios3/
  cspot-ios3        the player, armv6 MH_EXECUTE, stripped, ldid fake-signed (SHA-1, CD v0x20001)
  tls_test, tls_test.c   mbedTLS smoke test: entropy + TCP + TLS 1.2/1.3 handshake + HTTP GET
  libcspot.a, libbell.a  the libraries (unstripped, in case you want to link your own front end)
  cli-src/          CMakeLists.txt, main.cpp, CliPlayer.{h,cpp}, AudioQueueSink.{h,cpp}
  check-report.txt  armv6-ios3-check output
patches/
  cspot-squeezelite-ee1720c-0001-armv6-ios3.patch
```
Dependencies, all static, all built with the step-1-to-3 toolchain:
- mbedTLS **v3.6.7** (`068ff080b369adfac81509f9b57b2afabaf82dc5`, LTS), unpatched. Its only non-iOS-3 symbol is `clock_gettime`, supplied by `libios3compat.a`. TLS 1.2 and 1.3, ECDHE, AES-GCM/ChaCha are all pure C; fine on ARM11, just slow-ish.
- libc++ 18.1.3 + libc++abi (static, with `<fstream>`; see BUILD.md §3c).
- In-tree bell externals: nanopb, cJSON, tremor (integer Vorbis), libhelix-mp3, opencore-aacdec, civetweb, picohttpparser. Opus and MQTT off. nlohmann/json off (`BELL_ONLY_CJSON`). fmt and `std::regex` off.

Binary facts (from `armv6-ios3-check`, `otool`, `nm`, `objdump`):
- Links only `AudioToolbox`, `CoreFoundation`, `libSystem.B`, `libgcc_s.1`. All **241** undefined symbols exist in the 3.1.2 SDK's dylibs.
- Load commands: `LC_SEGMENT×4, LC_SYMTAB, LC_DYSYMTAB, LC_LOAD_DYLINKER, LC_UUID, LC_UNIXTHREAD, LC_ENCRYPTION_INFO(0), LC_LOAD_DYLIB×4, LC_CODE_SIGNATURE`. No dyld-info, no PIE, no TLV.
- No ARMv7/Thumb-2/NEON/VFPv3 instructions (scan of the whole disassembly). Atomics are `ldrex/strex` (ARMv6K, which the ARM1176 has).
- Exceptions: SjLj through the system `libgcc_s`/`libSystem` unwinder.

### Float ABI
`-mfloat-abi=softfp -mfpu=vfpv2 -marm`. VFPv2 instructions are used for float math (AAC/MP3/tremor are fixed-point anyway; the SDK's BellDSP float path is not compiled in), while float arguments are passed in core registers per the APCS soft-float convention. That is the convention of every armv6 dylib in the SDK (its libgcc_s exports both `__addsf3` and `__addsf3vfp`), so `softfp` is binary-compatible with the system and faster than `soft`. `hard` would break every call into the SDK that takes a float.

## 3. What was changed (and why)
### cspot/bell patch (13 files, +161/-4; four commits on top of the baseline)
| File | Change |
|---|---|
| `bell/main/platform/WrappedSemaphore.h/.cpp` | Apple builds use `dispatch_semaphore_*`; **iPhone OS 3 has no libdispatch** (iOS 4). Under `-DBELL_APPLE_NO_DISPATCH` the semaphore is a pthread mutex + condvar counting semaphore with the same `wait()/twait(ms)/give()` semantics. |
| `bell/main/platform/apple/MDNSService.cpp` | `#include "i386/endian.h"` does not exist on an ARM SDK; replaced by `<arpa/inet.h>` for `htons`. The rest is the stock `DNSServiceRegister`/`TXTRecord*` code, i.e. **Zeroconf goes through the system mDNSResponder (dns_sd)**, not mdnssvc/avahi. |
| `bell/CMakeLists.txt` | The codec glob always compiled `OPUSDecoder.cpp`/`AACDecoder.cpp`/`MP3Decoder.cpp` even when the codec option is off, so Opus could not be disabled. Disabled codecs' decoder sources are now removed from the list. |
| `bell/main/io/BellTar.cpp` | `#include <fstream>`; libc++ 18 no longer includes it transitively. |
| `bell/external/civetweb/civetweb.c` | `SO_REUSEPORT` on the listening socket on Apple (rev. 2). |
| `bell/main/io/BellHTTPServer.cpp` | civetweb `log_message` → bell log; optional `BELL_HTTP_NUM_THREADS` (rev. 2). |
| `bell/main/utilities/include/BellLogger.h` | `fflush(stdout)` after every log line (rev. 3). |
| `bell/main/utilities/include/BellTask.h` | explicit pthread stack sizes under `BELL_TASK_STACK_FLOOR` (rev. 3). |
| `bell/external/civetweb/civetweb.c` | `USE_STACK_SIZE` branch of `mg_start_thread_with_id` referenced an undeclared `attr`; fixed (rev. 3). |
| `src/Session.cpp`, `include/Session.h` | `lastApAddress` for the status display (rev. 4). |
| `bell/main/io/TLSSocket.cpp`, `include/TLSSocket.h` | `TLSSocket::lastNegotiated()` (rev. 4). |

### New code (not patches: new files in `cli-src/`)
- `AudioQueueSink`: `AudioQueueNewOutput` with NULL run loop (AudioToolbox's own thread), 3×8 KB buffers, a 128 KB ring in between. `feedPCMFrames()` blocks on a condvar while the ring is full, which is how the player thread is paced. Underruns are padded with silence so the queue never stops. `flush()` empties the ring and `AudioQueueReset`s. Volume via `kAudioQueueParam_Volume`. It also calls `AudioSessionInitialize`/`MediaPlayback` category; failures are logged and ignored.
- `CliPlayer`: upstream's CLI player minus BellDSP fades, adapted to the fork's `notifyAudioReachedPlayback(identifier)` (keeps a hash→identifier map filled from the data callback). `DEPLETED` is reported only once the sink has drained.
- `main.cpp`: upstream CLI main plus `--client-id/--client-secret`, `-n` name, `--port`, `SIGPIPE` ignored (no `MSG_NOSIGNAL` on Darwin), C stdio for the credentials file. Zeroconf login endpoint is bell's civetweb server on port 7864, advertised as `_spotify-connect._tcp` via dns_sd.

### Toolchain-side items that cspot needed
- `clock_gettime` (mbedTLS `platform_util.c`): `libios3compat.a`, declared through `IOS3_COMPAT_INCLUDE=1`.
- `std::ofstream` (BellTar): libc++ rebuilt with filesystem support (BUILD.md §3c).
- `thread_local`: not used by cspot/bell at all, so no change was needed.
- Blocks/libdispatch: only the semaphore (above).
- `arc4random_buf`/`getentropy`: not used; mbedTLS's entropy module uses `/dev/urandom` via its platform poll, and bell's `Crypto` uses mbedTLS CTR-DRBG.
- `std::chrono::steady_clock`: `mach_absolute_time` (libc++ patch).

### Build commands (reproducible)
```sh
export IOS3_SDK=.../iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1
# mbedTLS
git clone -b v3.6.7 --recurse-submodules https://github.com/Mbed-TLS/mbedtls && cd mbedtls
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<toolchain>/share/armv6-ios3/toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DCMAKE_INSTALL_PREFIX=$SYSROOT
ninja -C build install
# cspot (tree = squeezelite-esp32/components/spotify/cspot at ee1720c, plus the patch), CLI from cli-src/
cmake -S cli-src -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<toolchain>/share/armv6-ios3/toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
  -DBELL_EXTERNAL_MBEDTLS=$SYSROOT/lib/cmake/MbedTLS -DMBEDTLS_RELEASE=RELEASE
ninja -C build && armv6-ios3-strip -x build/cspot-ios3 && armv6-ios3-ldid -S build/cspot-ios3 && armv6-ios3-check build/cspot-ios3
```
(`cli-src/CMakeLists.txt` expects the cspot tree at `../cspot`. `MBEDTLS_RELEASE=RELEASE` is needed because bell's `FindMbedTLS.cmake` assumes a `NOCONFIG` imported location. nanopb's generator needs host `protoc` + `python3-protobuf`.)

## 4. Nothing is stubbed
No functionality was removed or stubbed to make it link. Things that are *not compiled in*, by configuration rather than by stubbing: Opus decoding, MQTT, nlohmann/json, fmt, `std::regex`-based URL parsing (bell has a hand-written parser under `BELL_DISABLE_REGEX`), BellDSP effects (volume fades; the player pauses hard instead). TLS certificate verification is **off**, as in upstream's own CLI: bell only verifies when an X.509 bundle was handed to `X509Bundle::init()`, and the CLI never calls it. Easy to add later by embedding a bundle generated with bell's python tool.

## 5. Not verified, and what to try first on the phone
Nothing here ran on hardware or under an emulator. Suggested order:
1. `hello_c` (from the step-3 bundle): does the signature format and the 16 KB segment alignment get accepted at all. If `Killed: 9`, re-sign with stock `ldid -S` and report.
2. `tls_test` (defaults to `apresolve.spotify.com 443`): proves `/dev/urandom` entropy, BSD sockets, DNS, and mbedTLS 3.6 on ARM11, and prints the negotiated TLS version/ciphersuite. Try `tls_test example.com 443` too.
3. `hello_audio`: AudioQueue tone from a command-line process, and dns_sd browsing.
4. `cspot-ios3 --client-id … --client-secret … -b 96`, then open Spotify on a phone on the same Wi-Fi and look for "iPhone Fidelio". Run it over SSH first so you see the log. Expect the Vorbis decode (tremor, integer) to be the CPU hog on a 412 MHz ARM11; 96 kbps is the kindest setting.

Known unknowns, in the order I would expect to hit them:
- **AudioSession from a non-app process.** `AudioSessionInitialize` may fail or mediaserverd may refuse a process with no bundle id. The sink logs the status and continues; if there is no sound but no error, that is the first suspect. Running under `launchctl`/as `mobile` instead of root may matter.
- **Memory.** Rev 1 allocated ≈540 MB for `CentralAudioBuffer` and rebooted the phone (see Revision 3 at the top). Rev 3 keeps the whole process in the single-digit MB range; if it is still killed, the `T [rss …]` trace lines show where the resident size climbs.
- **Thread stacks**: bell's `Task` requests small stacks (1–4 KB) that only mean something on ESP32; on Darwin they are pthread defaults. Fine.
- **Spotify protocol drift.** This fork was current as of 2026-10; Spotify changes AP/login behaviour without notice. If login fails with a protocol error, that is upstream, not the port.
- **civetweb** compiled with its Unix defaults; nothing iOS-specific was checked beyond "links and symbols exist".
- **Audio format**: the sink is hard-wired to 44.1 kHz/16-bit/stereo (what cspot delivers).

## 6. Next steps if it works
- Enable TLS verification by embedding a CA bundle (`bell/main/io/X509Bundle`).
- Replace tremor's generic C with ARM-optimised paths if CPU-bound (tremor has `_ARM_ASSEM_` paths for ARMv6).
- A `launchd` plist so it starts at boot in the dock.
- AirPlay/RAOP is the alternative path if Spotify Connect proves too heavy; the same toolchain, dns_sd and AudioQueue sink apply (shairport-sync needs libdispatch-free config; owntone's RAOP code or the older shairport are simpler targets).

## Audio output since audiod Milestone B1 (rev 8)

cspot-ios3 no longer opens the AudioQueue itself: the player thread feeds `fidelio-audiod`
over `/var/mobile/fidelio/audiod.sock` (`--audiod-sock PATH`, env `AUDIOD_SOCK`), which
arbitrates between Spotify and AirPlay. `--direct-audio` restores the local AudioQueue
(`--audio-route` / `--on-route-change` / `--audio-session auto|on|off` apply to it; B1.8: `auto`
retries a failed AudioSessionInitialize forever and plays bare meanwhile, `on` waits for the session,
`off` is a bare queue with no AudioSession calls, for A/B only);
`--no-audio` is unchanged.

**B1.9 (absolute volume).** The audiod sink speaks protocol v2: it connects at startup, reads audiod's
WELCOME (does the slider drive the hardware? current level) and HWVOLUME (the level changed: phone
buttons), and sends every Connect volume as gain + raw slider. With audiod in `--spotify-volume
absolute` (the default) the slider sets the device output volume, Spotify plays at unity, the
reported `device_state.volume` is the true hardware level (`--initial-volume` ignored) and button
presses are pushed to the controller with `SpircHandler::setRemoteVolume`. Against an old audiod or
`--spotify-volume software` everything is B1.7. `--sink-test-volume PCT` moves the slider mid-tone for
the host test.
`--sink-test N` plays N seconds of a 440 Hz tone through the selected sink and exits (no
Spotify login needed): run it on the device to prove the audiod path. The CLI now also builds
on a Linux host (`-DCSPOT_DIR=<cspot tree>`, no AudioQueue) for the audiod host tests.
Since B1.1 the volume goes to audiod as a VOLUME message and the status files are
`status-spotify.json` / `status-spotify.plist` / `cover-spotify.jpg`. See `audiod/README.md`.

## Volume (B1.7, rev 8.7)

- The Connect slider is the receiver's software volume (the protocol cannot drive the dock's
  amplifier). cspot-ios3 now reports `--initial-volume PCT` (default 50) in its device state and
  plays at that gain from the start, so the controller's slider no longer sits at 0 and jumps on
  the first touch (the library's default was 0).
- Slider -> gain uses a perceptual curve, `--volume-curve log|cubic|linear` (default log over
  60 dB like librespot: 50 % = -30 dB), computed once in the CLI and applied as a linear gain to
  both the local AudioQueue (`--direct-audio`) and the VOLUME message to audiod. RAOP's volume
  path is unchanged (true dB, -144 = unity).
