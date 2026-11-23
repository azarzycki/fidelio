# Third-party notices: the device binaries

This file lists the third-party code that is linked into the ARMv6 device binaries of this
repository, and where the licence texts are. It states facts: what is linked, which version, and
which licence the component's own files state. It is not legal advice and has not had a legal
review. Written 2026-11-22 from the build of `tools/build-arm.sh` with toolchain revision 3: the
link maps (`arm/maps/<step>/<binary>.map`, table "# Object files:", which lists every object,
archive member and dylib the linker used), the compiler's dependency files and the source trees.

The packages and the binaries they hold:

- `dock-receiver`: `dock-audiod`, `dock-lockctl`, `dock-raop`, `cspot-ios3`,
  `NowPlaying.app/NowPlaying`, and the lock plugin as `NowPlaying.bundle/NowPlaying` and
  `NowPlaying.lockbundle/NowPlaying` (the same sources, linked once per bundle name).
- `dock-receiver-dockremote`: `DockRemote.dylib`.

The project's own code is under the MIT licence in the top-level `LICENSE`. That file also states
that `cspot-ios3` statically links the cspot library, which its upstream publishes under the GNU
General Public License version 3 or later, and that `cspot-ios3` as a whole is distributed under
GPL-3.0-or-later. The text of the GPL version 3 is `toolchain/licenses/gcc.COPYING3.GPL-3.0.txt`
(GCC 13.3.0's `COPYING3`: the plain GPL version 3 text; it differs from the text the FSF publishes
today only in four links, `http` instead of `https` and one path).

Where the texts are: in the repository, `packaging/licenses/` holds the texts copied for this file
and `toolchain/licenses/` the texts that already exist for the toolchain. The packages install
them under the same file names in `/usr/share/doc/dock-receiver/licenses/<file>` and
`/usr/share/doc/dock-receiver-dockremote/licenses/<file>`. `packaging/licenses/MANIFEST.tsv`
gives, for every text named here, its repository path, the component, its source path in the
upstream tree, and its sha256.

## Components and binaries

| Component | Version | Licence as stated | Linked into | Texts |
|---|---|---|---|---|
| cspot (Spotify library) | squeezelite-esp32 `ee1720c`, `components/spotify/cspot`, + project patch | no licence file in the tree (see below) | cspot-ios3 | (GPL-3.0 text for the binary: `gcc.COPYING3.GPL-3.0.txt`) |
| bell (cspot's support library, its own code) | same tree | no licence file in the tree (see below) | cspot-ios3 | none exists |
| civetweb | 1.16 (in bell) | MIT text in file headers; MD5 part zlib-style notice; SHA-1 part "100% Public Domain"; no licence file | cspot-ios3 | `civetweb.file-notices.txt` |
| picohttpparser | not stated (in bell) | header: "either the MIT License (below) or the Perl license"; no licence file | cspot-ios3 | `picohttpparser.file-notices.txt` |
| cJSON | 1.7.15 (in bell) | `LICENSE`; README: "MIT License" | cspot-ios3 | `cJSON.LICENSE.txt` |
| nanopb runtime | `nanopb-0.4.6-dev` / `0.4.6.3` (in bell) | `LICENSE.txt` (zlib licence wording, not named) | cspot-ios3 | `nanopb.LICENSE.txt` |
| Tremor (integer Ogg Vorbis decoder) | see its section (in bell) | `COPYING`; headers: "BSD-STYLE SOURCE LICENSE" | cspot-ios3 | `tremor.COPYING.txt` |
| Mbed TLS | 3.6.7, commit `068ff08` | `Apache-2.0 OR GPL-2.0-or-later` | cspot-ios3 (TLS, X.509, crypto), dock-raop (crypto) | `mbedtls.LICENSE.txt` |
| Apple ALAC decoder | not recorded (`airplay/alac/`) | headers: Apache License 2.0; file beside them: APSL 2.0 text | dock-raop | `alac.LICENSE.txt`, `alac.APPLE_LICENSE.txt` |
| LLVM libc++ / libc++abi | 18.1.3 (toolchain r3) | Apache-2.0 WITH LLVM-exception (legacy NCSA or MIT) | dock-audiod, dock-lockctl, dock-raop, cspot-ios3 | `llvm-libcxx.*`, `llvm-libcxxabi.*`, `llvm-runtimes.embedded-third-party-notices.txt` |
| Apple Csu start-up objects | Csu-79 (toolchain r3) | `start.s`, `crt.c`: APSL 1.1; `dyld_glue.s`: APSL 2.0 | every binary (`crt1.o`: the five executables; `bundle1.o`: both lock plugin files; `dylib1.o`: DockRemote.dylib) | `APSL-1.1.txt`, `cctools-port.APSL-2.0.txt` |

Per binary (third-party parts only; each also holds the project's own objects):

| Binary | Package | Third-party code linked |
|---|---|---|
| `dock-audiod` | dock-receiver | Csu `crt1.o`; libc++/libc++abi (17 members of `libc++.a`) |
| `dock-lockctl` | dock-receiver | Csu `crt1.o`; libc++/libc++abi (16 members) |
| `dock-raop` | dock-receiver | Csu `crt1.o`; ALAC decoder (6 objects, `libalacdec.a`); Mbed TLS `libmbedcrypto.a` (62 members); libc++/libc++abi (27 members) |
| `cspot-ios3` | dock-receiver | Csu `crt1.o`; cspot `libcspot.a` (22 members); bell `libbell.a` (33 members: bell's own code, picohttpparser, civetweb, cJSON, nanopb, Tremor); Mbed TLS `libmbedtls.a` (13), `libmbedx509.a` (2), `libmbedcrypto.a` (63); libc++/libc++abi (38 members) |
| `NowPlaying.app/NowPlaying` | dock-receiver | Csu `crt1.o` |
| `NowPlaying.bundle/NowPlaying`, `NowPlaying.lockbundle/NowPlaying` | dock-receiver | Csu `bundle1.o` |
| `DockRemote.dylib` | dock-receiver-dockremote | Csu `dylib1.o` |

Texts per package: `dock-receiver` carries every text listed in `MANIFEST.tsv`;
`dock-receiver-dockremote` needs only `cctools-port.APSL-2.0.txt` (its one third-party part is
`dylib1.o`, built from `dyld_glue.s`).

Not shipped (the device's own): the dynamic system libraries and frameworks the binaries link
against (`libSystem.dylib`, `libgcc_s.1.dylib`, `libobjc.A.dylib`/`libobjc.dylib`, UIKit,
Foundation, CoreFoundation, CoreGraphics, AudioToolbox).

## The components

### cspot (the Spotify library)

- Version: the tree `components/spotify/cspot` of squeezelite-esp32
  (https://github.com/sle118/squeezelite-esp32) at commit
  `ee1720c7556ffec5f43329aadd47c20546bdbb79` (branch `master-v4.3`), with the project's patch
  `spotify/patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch` (sha256 `506848c1...` per the
  build's `deps/cspot.stamp`). Both are pinned in `tools/_common.sh`.
- Licence as stated: the tree has no licence file, neither at its root nor in `src/`, `include/`
  or `protobuf/`, and its source files carry no copyright or licence header. The top-level
  `LICENSE` states that cspot's upstream publishes it under GPL-3.0-or-later (`spotify/CSPOT.md`
  names that upstream: feelfreelinux/cspot, commit `1b07a9c`); this was not re-checked for this
  file.
- Linked: `libcspot.a`, 22 members: 17 C++ files from `src/` and 5 `.pb.c` files that the nanopb
  generator (`nanopb-0.4.6-dev`, per their header) writes from the tree's `protobuf/*.proto`.
- Text: the GPL version 3 text for `cspot-ios3` as a whole,
  `toolchain/licenses/gcc.COPYING3.GPL-3.0.txt`.
- Notes: two files in `spotify/cli-src/` (this repository) state their origin in this code's
  upstream: `main.cpp` "Based on feelfreelinux/cspot targets/cli/main.cpp (1b07a9c)" and
  `CliPlayer.h` "Adapted from feelfreelinux/cspot targets/cli/CliPlayer (1b07a9c)". Neither carries
  a licence statement.

### bell (cspot's support library)

- Version: the same tree, directory `bell/` (no separate version or commit of its own in the tree).
- Licence as stated: none. There is no licence file in `bell/`, and none anywhere in the tree
  outside `bell/external/` (checked in the patched build tree and in the pristine sparse checkout of
  squeezelite-esp32 at the pinned commit). The bell source files that are linked carry no copyright
  or licence header.
- Linked: 15 members of `libbell.a` from `bell/main`: `BellHTTPServer`, `CircularBuffer`,
  `HTTPClient`, `SocketStream`, `TLSSocket`, `URLParser`, `X509Bundle`, `BellLogger`, `Crypto`,
  `NanoPBExtensions`, `NanoPBHelper`, `MDNSService` (Apple variant, through the system's dns_sd),
  `WrappedSemaphore`, `utilities/aes.c`, and `io/picohttpparser.c` (its own section below); and 18
  members from `bell/external` (civetweb, cJSON, nanopb, Tremor; sections below).
- `bell/main/utilities/aes.c` (AES in ECB/CTR/CBC mode) has no copyright or licence statement, and
  the tree does not state where it comes from.
- Not linked into `cspot-ios3` (no member in the link map, and no header of theirs read by a linked
  object): bell's copies of libhelix-mp3, opencore-aacdec, ALAC, opus, fmt, nlohmann_json, mdnssvc.
  (`spotify/CSPOT.md` says the MP3/AAC decoders "are still linked"; the current link map lists no
  member from them.)
- Text: none exists.

### civetweb

- Version: 1.16 (`CIVETWEB_VERSION` in `bell/external/civetweb/include/civetweb.h`).
- Path: `components/spotify/cspot/bell/external/civetweb/`. `civetweb.c` is changed by the
  project's patch (a listening socket option, a thread stack size fix); its header is unchanged.
- Licence as stated: the directory has no licence file. `civetweb.c` and `civetweb.h`: copyright of
  "the Civetweb developers" and "Sergey Lyubka" with the MIT permission text. `CivetServer.cpp` and
  `CivetServer.h`: "License http://opensource.org/licenses/mit-license.php MIT License".
  `handle_form.inl`: the MIT permission text. `md5.inl` (included by `civetweb.c`): copyright
  "Aladdin Enterprises", zlib-style permission text. `sha1.inl` (included by `civetweb.c`, per the
  dependency file): "100% Public Domain".
- Linked into: `cspot-ios3` (`civetweb.c`, `CivetServer.cpp`).
- Text: `packaging/licenses/civetweb.file-notices.txt` (not an upstream file: the header blocks
  above, copied verbatim with their file names and line numbers).

### picohttpparser

- Version: not stated in the tree.
- Path: `components/spotify/cspot/bell/main/io/picohttpparser.c` (unchanged by the patch).
- Licence as stated: no licence file. The header: copyright "Kazuho Oku, Tokuhiro Matsuno, Daisuke
  Murase, Shigeo Mitsunari", "licensed under either the MIT License (below) or the Perl license",
  followed by the MIT permission text.
- Linked into: `cspot-ios3`.
- Text: `packaging/licenses/picohttpparser.file-notices.txt` (not an upstream file: the header
  block, copied verbatim).

### cJSON

- Version: 1.7.15 (`CJSON_VERSION_*` in `cJSON.h`).
- Path: `components/spotify/cspot/bell/external/cJSON/`.
- Licence as stated: `LICENSE`, copyright "Dave Gamble and cJSON contributors", MIT permission
  text; the README's section "License" names it "MIT License". `cJSON.c` carries the same text.
- Linked into: `cspot-ios3` (`cJSON.c`).
- Text: `packaging/licenses/cJSON.LICENSE.txt` (copy of `bell/external/cJSON/LICENSE`).

### nanopb (runtime)

- Version: `pb.h` says `nanopb-0.4.6-dev`; `library.json` says `0.4.6.3`.
- Path: `components/spotify/cspot/bell/external/nanopb/`.
- Licence as stated: `LICENSE.txt`, copyright "Petteri Aimonen"; the file does not name the licence
  (its wording is that of the zlib licence). The linked source files carry an author line only.
- Linked into: `cspot-ios3` (`pb_common.c`, `pb_decode.c`, `pb_encode.c`).
- Text: `packaging/licenses/nanopb.LICENSE.txt` (copy of `bell/external/nanopb/LICENSE.txt`).

### Tremor (integer Ogg Vorbis decoder, with its Ogg framing code)

- Version: the tree states several: `README` "as of date 2002 09 02, version 1.0.0"; the top entry
  of `CHANGELOG` "20020517: 1.0.2"; `debian/changelog` "libvorbisidec (1.2.0-1)".
- Path: `components/spotify/cspot/bell/external/tremor/`.
- Licence as stated: `COPYING`, "Copyright (c) 2002, Xiph.org Foundation", three-condition BSD
  form. Every linked source file says its licence is "A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS
  SOURCE IN 'COPYING'". `debian/copyright` repeats the same text.
- Linked into: `cspot-ios3` (`bitwise`, `codebook`, `dsp`, `floor0`, `floor1`, `floor_lookup`,
  `framing`, `info`, `mapping0`, `mdct`, `res012`, `vorbisfile`).
- Text: `packaging/licenses/tremor.COPYING.txt` (copy of `bell/external/tremor/COPYING`).

### Mbed TLS

- Version: 3.6.7, tag `v3.6.7`, commit `068ff080b369adfac81509f9b57b2afabaf82dc5`
  (https://github.com/Mbed-TLS/mbedtls; pinned in `tools/_common.sh`). No patch.
- Licence as stated: `LICENSE`: "Mbed TLS files are provided under a dual Apache-2.0 OR
  GPL-2.0-or-later license", followed by both full texts. Every file in `library/` carries
  `SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later`.
- Linked into: `cspot-ios3` (`libmbedtls.a` 13 members, `libmbedx509.a` 2, `libmbedcrypto.a` 63)
  and `dock-raop` (`libmbedcrypto.a` 62 members). No member built from `3rdparty/` is linked.
- Text: `packaging/licenses/mbedtls.LICENSE.txt` (copy of `LICENSE`).

### Apple ALAC decoder (`airplay/alac/`)

- Version: not recorded. The files came into the repository with the first commit of the AirPlay
  receiver (2026-11-07, "Includes Apple's ALAC decoder (Apache-2.0)"). All 14 files in
  `airplay/alac/` are byte-identical to the same-named files in `bell/external/alac/codec/` of the
  cspot tree at the pinned commit.
- Licence as stated: every source file: "Copyright (c) 2011 Apple Inc. All rights reserved." with
  an Apple Apache License 2.0 header. The licence file beside them, `airplay/alac/APPLE_LICENSE.txt`,
  is the text of the Apple Public Source License 2.0. `airplay/README.md` says "Apache-2.0 (see
  `alac/APPLE_LICENSE.txt`)". In the cspot tree's copy of the ALAC project, the root file `LICENSE`
  is the Apache License 2.0 text and `codec/APPLE_LICENSE.txt` is the same APSL 2.0 file.
- Linked into: `dock-raop` (`libalacdec.a`: `ALACBitUtilities.c`, `ALACDecoder.cpp`,
  `EndianPortable.c`, `ag_dec.c`, `dp_dec.c`, `matrix_dec.c`). The encoder in `airplay/hosttest/`
  is not part of any device binary.
- Texts: `packaging/licenses/alac.LICENSE.txt` (copy of `bell/external/alac/LICENSE`, Apache
  License 2.0) and `packaging/licenses/alac.APPLE_LICENSE.txt` (copy of
  `airplay/alac/APPLE_LICENSE.txt`).

### LLVM libc++ and libc++abi (C++ runtime, static)

- Version: LLVM 18.1.3, commit `c13b7485b87909fcf739f62cfa382b55407433c0`, as built into toolchain
  revision 3 (`toolchain/THIRD-PARTY-NOTICES.md` §6, which also names the toolchain's patch). The
  toolchain's `libc++.a` holds the libc++abi objects as well.
- Licence as stated: Apache-2.0 WITH LLVM-exception; the licence files also carry the legacy LLVM
  terms (University of Illinois/NCSA or MIT).
- Linked into: `dock-audiod` (17 members), `dock-lockctl` (16), `dock-raop` (27), `cspot-ios3`
  (38). In each, 15 members are libc++abi's (`cxa_*`, `stdlib_*`, `abort_message`,
  `fallback_malloc`, `private_typeinfo`). NowPlaying, the lock plugin and DockRemote.dylib link no
  member of `libc++.a`.
- Texts: `toolchain/licenses/llvm-libcxx.LICENSE.txt`, `llvm-libcxx.CREDITS.txt`,
  `llvm-libcxxabi.LICENSE.txt`, `llvm-libcxxabi.CREDITS.txt`,
  `llvm-runtimes.embedded-third-party-notices.txt`.
- On the per-file notices file: of the source files it lists, `charconv.cpp`, `ryu/*.cpp` (Ryu)
  and `tz.cpp` belong to `libc++.a` members (`charconv`, `d2s`, `f2s`, `d2fixed`) or sources that no
  link map lists. Headers it lists under `__format/` and `__mdspan/` appear in the dependency files
  of objects of `cspot-ios3`, `dock-raop` and `dock-audiod`; whether code from them is in the
  binaries was not determined.
- LLVM compiler-rt builtins: the toolchain's `libios3compat.a` holds 15 of them; the link maps list
  only its member `ios3compat.o` (the project's own file, MIT, `toolchain/THIRD-PARTY-NOTICES.md`
  §10), in `dock-audiod`, `dock-raop` and `cspot-ios3`. No compiler-rt object is linked.

### Apple Csu (start-up objects)

- Version: Apple's open-source release Csu-79, commit `fd4097d7a8b07792f55d74271d09adedb4748b94`,
  built by toolchain revision 3 (`toolchain/THIRD-PARTY-NOTICES.md` §9). sha256 of the objects as
  pinned in `tools/_common.sh`: `crt1.o` `29699d8e...`, `dylib1.o` and `bundle1.o` (same bytes)
  `e07e529b...`.
- Licence as stated (headers of the source files): `start.s` and `crt.c`: Apple Public Source
  License 1.1; `dyld_glue.s`: Apple Public Source License 2.0. The release has no licence file.
- Linked into: `crt1.o` (all three sources): `dock-audiod`, `dock-lockctl`, `dock-raop`,
  `cspot-ios3`, `NowPlaying.app/NowPlaying`. `bundle1.o` (`dyld_glue.s`): both lock plugin files.
  `dylib1.o` (`dyld_glue.s`): `DockRemote.dylib`. The notice string of the last section is present
  in each of the eight built files (`strings -a`: one match for `Csu-79` in each).
- Texts: `toolchain/licenses/APSL-1.1.txt`, `toolchain/licenses/cctools-port.APSL-2.0.txt`.

### The AirPort Express key (`airplay/`)

Not a code component; recorded because it is third-party material in `dock-raop`.
`airplay/src/airport_key.h` holds an RSA private key as a string, which `RaopCrypto.cpp` uses, so it
is compiled into `dock-raop`; `airplay/airport_express_key.pem` holds the key as a file and is not
read by the build. What the repository says about its origin: `airport_key.h`: "The well-known
AirPort Express RSA private key (as published in shairport / shairport-sync)"; `airplay/README.md`:
"the long-public AirPort Express RSA key that every RAOP receiver (shairport et al.) uses to answer
`Apple-Challenge` and unwrap `rsaaeskey`. Not a secret; required for the protocol." No licence
statement accompanies it.

## Components without a licence file

- cspot (the library): no licence file in the tree that is built, and no licence header in its
  files. The top-level `LICENSE` states its upstream's licence (GPL-3.0-or-later).
- bell: no licence file in the tree that is built (verified in the patched tree and in the
  pristine checkout at the pinned commit), and no licence header in its linked files.
  `docs/HANDOFF.md` §4 item 3 records that bell's upstream repository has none either (checked
  2026-11-22; not re-checked here).
- `bell/main/utilities/aes.c`: no licence statement, no copyright line, origin not stated.
- civetweb: no licence file; the file headers state MIT (and a zlib-style notice and a
  public-domain statement for the included MD5 and SHA-1 code). Notices collected in
  `civetweb.file-notices.txt`.
- picohttpparser: no licence file; the file header states MIT or the Perl license. Notice collected
  in `picohttpparser.file-notices.txt`.
- The five nanopb-generated `.pb.c` files of cspot: a "generated by nanopb" header only.
- Apple Csu-79: the release has no licence file; the source file headers state APSL 1.1 / 2.0, and
  the toolchain supplies the texts.
- `spotify/cli-src/main.cpp` and `CliPlayer.h` (this repository) state that they are based on or
  adapted from feelfreelinux/cspot's CLI target; they carry no licence statement.

## The start-up code notice

`toolchain/THIRD-PARTY-NOTICES.md` §9 ("Apple Csu (start-up objects of the ARMv6 target)") is the
documentation notice for the start-up code. Its "Source:" item reads:

> the source code of these three files is available, under the licences named above, at
> https://github.com/apple-oss-distributions/Csu (tag `Csu-79`, the commit above). The tarball
> holds the three object files and not the source files; the build script fetches them from that
> address.

("these three files" are `start.s`, `crt.c` and `dyld_glue.s`; "the licences named above" are the
Apple Public Source License version 1.1 for `start.s` and `crt.c` and version 2.0 for
`dyld_glue.s`; "the commit above" is `fd4097d7a8b07792f55d74271d09adedb4748b94`.)

The notice that the start-up objects put into each binary (section `__TEXT,__csu_notice`;
`strings -a FILE | grep Csu-79` prints it), quoted from §9:

- In every executable (`crt1.o`): "This program contains start-up code built from Apple's
  open-source Csu release, tag Csu-79: start.s and crt.c under the Apple Public Source License
  version 1.1, dyld_glue.s under the Apple Public Source License version 2.0. The source code is
  available under the terms of those licenses at https://github.com/apple-oss-distributions/Csu
  (commit fd4097d7a8b07792f55d74271d09adedb4748b94)."
- In every dylib and bundle (`dylib1.o`, `bundle1.o`): "This library contains glue code built from
  Apple's open-source Csu release, tag Csu-79: dyld_glue.s under the Apple Public Source License
  version 2.0. The source code is available under the terms of that license at
  https://github.com/apple-oss-distributions/Csu (commit fd4097d7a8b07792f55d74271d09adedb4748b94)."

The licence texts: `toolchain/licenses/APSL-1.1.txt` and
`toolchain/licenses/cctools-port.APSL-2.0.txt`, installed as
`/usr/share/doc/dock-receiver/licenses/APSL-1.1.txt` and
`/usr/share/doc/dock-receiver/licenses/cctools-port.APSL-2.0.txt`; for `DockRemote.dylib`,
`/usr/share/doc/dock-receiver-dockremote/licenses/cctools-port.APSL-2.0.txt`.
