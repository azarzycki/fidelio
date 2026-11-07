# fidelio-raop — AirPlay v1 / RAOP receiver for the ARMv6 iPhone (iOS 3.1.3)

A standalone AirPlay-1 audio receiver, built with the armv6-ios3 toolchain, that advertises itself over the system mDNSResponder, speaks RTSP/RAOP, receives RTP audio (ALAC or L16 PCM, optionally AES-128-CBC), plays through the same AudioQueue sink cspot-ios3 uses, and writes the same status files the Fidelio now-playing app reads (with `"source":"airplay"`). It is its own tree (`/home/user/airplay-ios3`), separate from cspot and the app. ~820 KB stripped, static mbedTLS 3.6.7, Apple's open-source ALAC decoder.

**Primary interop target: CenturyPlay** (g8row/centuryplay `e241344`, 2026-10-27). Its RAOP sink was read line by line and the receiver matches what it does: with `et=0,1` it sends **clear** ALAC (352 frames/packet, `fmtp 352 0 16 40 10 14 2 255 0 0 44100`), needs `server_port`+`control_port` in the SETUP Transport and a `Session` header, keeps the RTSP TCP open as its health probe, sends 0xD4 sync packets to our control port, answers 0x52 timing requests on its timing port, answers 0x55 resend requests with 0x56, and sends DMAP / JPEG / `progress:` only if `md` is advertised (it is). Also built to shairport-sync's conventions (rtsp.c / rtp.c / bonjour_strings.c at `0a34fbd`).

## Verified / not verified
**Verified on the Linux host** (same sources, `--pcm-out` file output, scripted sender in `hosttest/raop_sender.py` + Apple's ALAC encoder as the test encoder), four cases: PCM and ALAC, each plain and AES-encrypted:
- OPTIONS → `Apple-Response` recovers (with the public key) to `challenge | IPv4 | hwaddr` padded to 32 bytes, PKCS#1 v1.5 type 1 — the shairport algorithm.
- ANNOUNCE (SDP parse, RSA-OAEP/SHA-1 `rsaaeskey` unwrap, `aesiv`), SETUP (three UDP ports), RECORD (`Audio-Latency: 11025`), GET/SET_PARAMETER (volume, DMAP, image/jpeg, progress), FLUSH, TEARDOWN all 200.
- RTP with a deliberate 3-packet hole: the resend request (0x55) goes out, the 0x56 replies are spliced in, and the PCM the receiver wrote is **byte-identical** to what was sent (500 packets incl. the post-FLUSH segment).
- status.json/plist and cover.jpg written as specified. Peak RSS on the host ≈ 5 MB (incl. host libc); on the phone expect ≈ 2–3 MB like cspot.

**Not verified** (needs the phone): mDNS registration through iOS 3's mDNSResponder with these TXT records; AudioQueue pacing of the jitter buffer (on the host the file output does not pace, so the player drains and re-primes in 1 s chunks — on the device the sink blocks, as it does for cspot); clock drift over long sessions; CenturyPlay itself; iTunes/Music.app senders (they would use `et=1`, the tested path, but also send `Apple-Challenge` on ANNOUNCE — handled on every method); behaviour with two senders (second ANNOUNCE gets 453).

## Advertisement
Instance `<MAC>@Fidelio (AirPlay)` on `_raop._tcp`, SRV port = the RTSP port, TXT:
`txtvers=1 ch=2 cn=0,1 et=0,1 sr=44100 ss=16 tp=UDP vs=105.1 am=Fidelio1,1 sv=false da=true sf=0x4 md=0,1,2 vn=65537 pw=false`.
The MAC is a stable locally-administered fake derived from the name (FNV-1a), shown in the log at start; it is also the `hwaddr` signed into Apple-Response, as RAOP requires.

## RTSP
Server header `AirTunes/105.1`, `CSeq` echoed, `Apple-Challenge` answered on any method.
| Method | Behaviour |
|---|---|
| OPTIONS | `Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET` |
| ANNOUNCE | SDP: `rtpmap` AppleLossless → ALAC (fmtp → magic cookie) or `L16/44100/2` → PCM; `rsaaeskey` (RSA-OAEP SHA-1 with the AirPort Express key) + `aesiv` → AES-128-CBC per packet, trailing partial block in the clear. 453 if another sender holds the session; re-ANNOUNCE on the same connection restarts it. Status → `connected`, codec, peer (`ip User-Agent`). |
| SETUP | Parses `control_port=`/`timing_port=`; binds audio/control/timing UDP sockets (kernel-chosen ports); replies `Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=C;timing_port=T;server_port=A`, `Session: 1`, `Audio-Jitter-Latency: 0`. |
| RECORD | `RTP-Info: seq=…;rtptime=…` → first expected seq; reply `Audio-Latency: 11025`. Status → `playing`. |
| FLUSH | drops the jitter buffer and the sink; `RTP-Info seq` → next expected. Status → `paused`. PAUSE is treated the same (without a seq). |
| SET_PARAMETER | `text/parameters`: `volume: dB` (−30..0, −144 mute → `10^(dB/20)` linear gain on the AudioQueue; -30 dB = 0.03) and `progress: start/current/end` (rtptime → ms, 44.1 per ms). `application/x-dmap-tagged`: `minm`→title, `asar`→artist, `asal`→album (containers walked). `image/jpeg`|`image/png` → `cover.jpg` (atomic), `image/none` → cover cleared. |
| GET_PARAMETER | body `volume` → `volume: dB`. |
| TEARDOWN | tears the session down; status → `stopped`; the connection is closed. A dropped TCP connection also ends the session. |
| POST/GET /auth-setup | 200 with empty body (we never advertise `et=4`). Other paths 404. |

Every request is logged with its method, URI, CSeq, body size and headers; every response with its code and headers (`T [rss …]` lines, flushed).

## RTP / UDP
- Audio port: RTP type 0x60 (first packet 0xE0), seq at bytes 2–3. Decrypt → decode → stored in a 256-slot jitter buffer indexed by seq (≈360 KB, 2.0 s). Playback starts once `--latency-ms` (default 1000) of packets is buffered, then plays in seq order; a missing packet with newer ones present is concealed with one packet of silence; nothing newer → wait (re-prime), so a stalled sender resumes cleanly; sender >2 s ahead → jump forward. Gaps ≤64 packets trigger a resend request (0x55) to the sender's control port; 0x56 replies are decoded into the buffer.
- Control port: receives 0xD4 sync (logged once; we run on the local clock, no multiroom sync) and 0x56 resends.
- Timing port: every 3 s we send a 0x52 request to the sender's timing port and log the RTT every 20th reply (keeps senders like CenturyPlay's responder exercised); a 0x52 arriving at us is answered with 0x53.
- Threads: RTSP accept + one per connection (128 KB), per session audio-rx (128 KB), control-rx, timing, player, status writer (64 KB). The AudioQueue sink is the shared one (128 KB ring + 3×8 KB).

## Status files (`--status-dir`, default `/var/mobile/fidelio`)
Same keys as cspot rev 4, written atomically once a second and on every change: `source:"airplay"`, `state` (`idle` advertising → `connected` after ANNOUNCE → `playing` after RECORD → `paused` on FLUSH/PAUSE → `stopped` on TEARDOWN/disconnect), `title/artist/album` from DMAP, `duration_ms`/`position_ms` from `progress:` plus a local clock that runs while playing, `bitrate_kbps: 0`, `codec: "ALAC"|"PCM"`, `cover` (`cover.jpg` or `""`), `device` (the advertised name), `ap` (sender IP + User-Agent), `country: ""`, `tls: ""`, `rss_kb`, `updated`. cspot rev 5 writes `source:"spotify"`; app 1.1 shows the source in the state line and in the Technical tab. (The two receivers write the same files; whichever wrote last wins — PHASE 2 arbitration will fix that.)

## Running it
```sh
scp fidelio-raop root@phone:/var/mobile/fidelio/
ssh mobile@phone   # or root: su mobile
cd /var/mobile/fidelio && ./fidelio-raop 2>&1 | tee raop.log
./fidelio-raop --name "Fidelio (AirPlay)" --port 5000 --latency-ms 1000           # defaults
./fidelio-raop --no-audio            # decode and discard (isolation test)
./fidelio-raop --pcm-out /var/mobile/fidelio/capture.pcm   # write what would be played (44.1k/16/stereo LE)
./fidelio-raop --no-status --port 0   # no status files, any free RTSP port
```
Then pick "Fidelio (AirPlay)" in CenturyPlay (or any AirPlay-1 sender). If nothing finds it, the log shows the mDNS registration callback result; `tls_test`'s dns_sd and cspot's `_spotify-connect` registration already work on the device, so `_raop._tcp` should too.

launchd (same pattern as cspot): a plist in `/Library/LaunchDaemons/com.fidelio.raop.plist` with `UserName mobile`, `WorkingDirectory /var/mobile/fidelio`, `ProgramArguments [/var/mobile/fidelio/fidelio-raop]`, `KeepAlive true`, `StandardOutPath /var/mobile/fidelio/raop.log`. Both receivers can run at once in PHASE 1 (different ports/services); they will fight over the AudioQueue only when both actually play.

## PHASE 2 (not built): "most-recent-source-wins" arbiter
`fidelio-audiod` owns the single AudioQueue; cspot-ios3 and fidelio-raop become PCM producers over a local Unix-domain socket (`/var/mobile/fidelio/audio.sock`), each frame tagged `{source_id, seq, pcm}`. The RAOP code is already behind `AudioOutput` (`src/AudioOutput.h`): a `SocketOutput` implementing `write/flush/setVolume/bufferedSeconds` is the only change on this side; cspot's `AudioQueueSink` gets the same treatment. Arbiter rules as you specified: a source takes over after ~400 ms of continuous frames, ≥1.5 s hold after any switch, release when silent ~1 s; the loser is told to pause (cspot → Spirc pause; RAOP → stop feeding / optional DACP pause via the sender's `Active-Remote`/`DACP-ID`, which we already log). Status files would then be written by the arbiter from whichever source is live.

## Files
`dist/fidelio-raop` (signed), `src/` (receiver), `alac/` (Apple ALAC decoder, Apache-2.0), `hosttest/` (Linux protocol test: `raop_sender.py`, `compare.py`, `alac_enc.cpp`), `airport_express_key.pem`, `CMakeLists.txt` (device and host builds).
Build: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<toolchain>/share/armv6-ios3/toolchain.cmake -DMBEDTLS_PREFIX=<mbedtls install>` with `IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1`; host test: `cmake -S . -B build-host -DMBEDTLS_PREFIX=<host mbedtls 3.6.7>`.

## Audio output since audiod Milestone B1

fidelio-raop no longer opens the AudioQueue itself: it streams the decoded PCM to
`fidelio-audiod` over `/var/mobile/fidelio/audiod.sock` (`--audiod-sock PATH`, env
`AUDIOD_SOCK`), which arbitrates between AirPlay and Spotify. `--direct-audio` restores the
local AudioQueue for standalone debugging; `--pcm-out` / `--no-audio` are unchanged. The
volume from SET_PARAMETER is applied as gain before sending; TEARDOWN sends STOP so audiod
releases the output immediately. See `audiod/README.md`.
