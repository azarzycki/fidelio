# fidelio-ios3-airplay

An **AirPlay v1 / RAOP audio receiver for the original 2007 iPhone** (ARMv6, iOS 3.1.3), built with the same cross-toolchain as [fidelio-ios3-spotify](https://github.com/azarzycki/fidelio-ios3-spotify). It advertises itself over the system mDNSResponder, speaks RTSP/RAOP, receives RTP audio (ALAC or L16 PCM, optionally AES-128-CBC), plays through an AudioQueue sink, and writes the same status files the [now-playing app](https://github.com/azarzycki/fidelio-ios3-nowplaying) reads (with `"source":"airplay"`). ~820 KB stripped.

So the docked iPhone can play **anything** streamed to it as an AirPlay speaker — not just Spotify.

**Primary interop target:** [CenturyPlay](https://github.com/g8row/centuryplay), the open-source Android app that captures system audio and streams RAOP (its sink was read line-by-line and the receiver matches it); also built to shairport-sync's conventions. Any AirPlay-1 sender (iTunes/Music, AirMusic, Airvia's RAOP fallback) should work.

## Status
Host-verified (Linux, scripted RAOP sender + Apple's ALAC encoder): OPTIONS/`Apple-Response`, ANNOUNCE (RSA-OAEP key unwrap + AES), SETUP/RECORD/FLUSH/TEARDOWN, SET/GET_PARAMETER (volume, DMAP metadata, JPEG artwork, progress), and RTP with packet loss + resend — byte-identical output for PCM and ALAC, plain and encrypted. **Not yet verified on the phone** (mDNS registration, AudioQueue pacing, CenturyPlay itself). See [AIRPLAY.md](AIRPLAY.md) for the full protocol notes and the exact TXT records / RTSP behaviour.

## Layout
- `src/` — the receiver: `RtspServer`, `RaopSession`, `RaopCrypto`, `Dmap`, `Mdns`, `StatusFile`, `AlacWrapper`, `AudioQueueSink`, `AudioOutput.h` (abstraction for the planned most-recent-source-wins arbiter), `main.cpp`.
- `alac/` — Apple's open-source ALAC decoder (Apache-2.0; see `alac/APPLE_LICENSE.txt`).
- `hosttest/` — Linux protocol test harness (`raop_sender.py`, `compare.py`) + Apple's ALAC encoder used as the test encoder.
- `airport_express_key.pem` — the long-public AirPort Express RSA key that every RAOP receiver (shairport et al.) uses to answer `Apple-Challenge` and unwrap `rsaaeskey`. Not a secret; required for the protocol.

## Build & run
```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<toolchain>/share/armv6-ios3/toolchain.cmake -DMBEDTLS_PREFIX=<mbedtls install>
# IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1 in the environment
./fidelio-raop --name "Fidelio (AirPlay)"      # defaults: RTSP port 5000, 1 s jitter buffer
./fidelio-raop --no-audio                      # decode and discard (isolation test)
```
Then pick "Fidelio (AirPlay)" in CenturyPlay or any AirPlay-1 sender on the same Wi-Fi.

## Credits & licenses
ALAC decoder/encoder: Apple, Apache-2.0 (`alac/APPLE_LICENSE.txt`). RAOP/`Apple-Response` algorithm and TXT conventions follow [shairport-sync](https://github.com/mikebrady/shairport-sync). TLS/RSA/AES via [Mbed-TLS](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0). Original receiver code (`src/`, `hosttest/*.py`) is MIT — see [LICENSE](LICENSE).
