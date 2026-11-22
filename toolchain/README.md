# armv6-ios3 cross toolchain

A cross toolchain that runs on Linux x86_64 and builds ARMv6 Mach-O binaries for iPhone OS 3.x (the
2007 iPhone, ARM1176JZF-S). It is a set of wrappers around the host's stock clang 18, plus a linker,
a signer, a C++17 runtime and start-up objects for the target. Every ARM binary of this repository is
built with it (`tools/build-arm.sh`).

This directory holds the toolchain's own source files and the script that packs the release tarball,
`armv6-ios3-toolchain-linux-x86_64.tar.xz`. The file `VERSION` holds the toolchain revision: **3**.

Apple's iPhone SDK is **not** part of the toolchain, of the tarball or of this repository. To compile
and link you supply your own copy of `iPhoneOS3.1.2.sdk` and point `IOS3_SDK` at it. `spotify/BUILD.md`
§1 says which SDK it is and how a copy was checked. A host clang 18 is not included either
(`apt install clang-18`).

<!-- README.txt: pack.sh copies the text block below into the tarball as README.txt -->
```text
armv6-ios3 cross toolchain (Linux x86_64 host -> ARMv6 iPhone OS 3.x). Revision: see VERSION.

  export IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk   # Apple's SDK is NOT included: supply your own copy
  export PATH=$PWD/bin:$PATH                   # needs the host's clang 18 (apt install clang-18)
  armv6-ios3-clang -O2 hello.c -o hello && armv6-ios3-ldid -S hello && armv6-ios3-check hello
  armv6-ios3-clang++ -std=c++17 -stdlib=libc++ ...   (static libc++ 18)   |  default C++: the SDK's libstdc++ 4.2.1
  CMake: -DCMAKE_TOOLCHAIN_FILE=$PWD/share/armv6-ios3/toolchain.cmake
  armv6-ios3-check needs python3.

Full notes:  share/armv6-ios3/BUILD.md
Licences:    LICENSE (MIT) covers this project's own files: the wrapper scripts, toolchain.cmake,
             ios3compat, ios3_tls.hpp and the build notes. Everything else is third-party software:
             THIRD-PARTY-NOTICES.md lists each component with its version, licence and source, and
             LICENSES/ holds the licence texts.
Start-up objects (armv6-ios3/lib/startfiles/crt1.o, dylib1.o, bundle1.o): built from Apple's
             open-source Csu release, tag Csu-79. Their source code is available under the Apple
             Public Source License (version 1.1 for start.s and crt.c, version 2.0 for dyld_glue.s)
             at https://github.com/apple-oss-distributions/Csu
             Each of the three objects carries a notice that says so, as a text in its section
             __TEXT,__csu_notice, and the linker copies that section into every executable, dylib
             and bundle built with this toolchain ("strings -a FILE | grep Csu-79" prints it).
             share/armv6-ios3/src/startfiles/ has the script that builds them, the source of the
             notice (notice.s) and both licence texts.
```

## Layout of the tarball

```
armv6-ios3-toolchain/
  README.txt, VERSION                        the text block above; the revision number
  LICENSE                                    MIT, for this project's own files (the repo's top-level LICENSE)
  THIRD-PARTY-NOTICES.md                     every third-party component: version, licence, source, patch
  LICENSES/                                  25 licence texts and MANIFEST.tsv (origin, size, sha256 of each)
  bin/armv6-ios3-clang, armv6-ios3-clang++   compiler wrapper (clang++ is a symlink to it)
  bin/armv6-ios3-ldid                        fake-signer wrapper
  bin/armv6-ios3-check                       static iOS 3 compatibility checker (python3)
  bin/armv6-ios3-{ar,ld,lipo,nm,otool,strip,...}   13 symlinks to the arm-apple-darwin9-* tools
  bin/arm-apple-darwin9-*                    cctools-port, 31 programs (ELF)
  bin/ldid                                   ProcursusTeam ldid, patched (ELF)
  lib/libdispatch.so, libBlocksRuntime.so, libplist-2.0.so.4   what the host tools load ($ORIGIN/../lib)
  armv6-ios3/lib/libc++.a, libc++abi.a, libc++experimental.a   static LLVM 18.1.3 C++ runtime for the target
  armv6-ios3/lib/libios3compat.a             functions iPhone OS 3 lacks; linked last by the wrapper
  armv6-ios3/lib/startfiles/crt1.o, dylib1.o, bundle1.o        start-up objects, built from Apple's Csu,
                                             each with the source-availability notice inside
  armv6-ios3/include/c++/v1/                 libc++ headers (1,017 files)
  armv6-ios3/include/ios3compat.h, ios3_tls.hpp
  share/armv6-ios3/toolchain.cmake           CMake toolchain file
  share/armv6-ios3/BUILD.md                  the build notes (spotify/BUILD.md of this repo)
  share/armv6-ios3/patches/                  the three patches to cctools-port, ldid and LLVM
  share/armv6-ios3/src/ios3compat/           ios3compat.c, ios3compat.h
  share/armv6-ios3/src/startfiles/           build-startfiles.sh, notice.s, APSL-1.1.txt, APSL-2.0.txt
```

## Where each part comes from

| Part | Source | By revision |
|---|---|---|
| Host tools: `bin/arm-apple-darwin9-*`, `bin/ldid`, `lib/*.so*` | Built from pinned upstream commits of cctools-port, apple-libdispatch and ldid, with the patches in `spotify/patches/`, as `spotify/BUILD.md` §2 documents. libplist is Ubuntu's library, copied. | The files of revision 1, unchanged in revisions 2 and 3. |
| C++ runtime: `libc++.a`, `libc++abi.a`, `libc++experimental.a`, the libc++ headers | Built from LLVM `llvmorg-18.1.3` with `spotify/patches/llvm-project-18.1.3-0001-libcxx-libcxxabi-ios3.patch`, as `spotify/BUILD.md` §3c documents. | The files of revision 1, unchanged in revisions 2 and 3. |
| `libios3compat.a` | `src/ios3compat/ios3compat.c` here, plus builtins from LLVM compiler-rt 18.1.3 (`spotify/BUILD.md`, "libios3compat.a"). | The file of revision 1, not rebuilt. `ios3compat.c`, `ios3compat.h` and `ios3_tls.hpp` here are byte-identical to revision 1's. |
| Wrappers, CMake file, headers: `bin/`, `share/`, `src/`, `include/` here | This project. | Revision 2 changed `bin/armv6-ios3-clang` (start-up objects, link maps); the rest is byte-identical to revision 1. Revision 3 changed none of them. |
| Start-up objects | Apple's open-source Csu release, built by `startfiles/build-startfiles.sh`; the notice inside them is `startfiles/notice.s`, this project's file. | New in revision 2. Revision 1 had none: its links took `crt1.o`, `dylib1.o` and `bundle1.o` from the SDK. Since revision 3 each object carries the source-availability notice. |
| `LICENSES/`, `THIRD-PARTY-NOTICES.md`, `LICENSE` | `licenses/` and `THIRD-PARTY-NOTICES.md` here; the repo's top-level `LICENSE`. | New in revision 2. Revision 3 changed `THIRD-PARTY-NOTICES.md` (§9 and §10: the notice); the licence texts are the same. |

- **Revision 1** (2026-11-06) is the tarball the project was built with until 2026-11-21. It is not
  meant for publication: it has no licence texts and carries an older copy of the build notes.
- **Revision 2** (2026-11-21) added the start-up objects, the licence texts and the pack script. Its
  start-up objects carry no notice text, and `tools/build-arm.sh` no longer accepts it.
- **Revision 3** (2026-11-22) puts the source-availability notice into the three start-up objects.
  Against revision 2 the tarball differs in those three objects, in the new `notice.s` next to their
  build script, and in the texts that describe them (`README.txt`, `VERSION`,
  `THIRD-PARTY-NOTICES.md`, `BUILD.md`, `build-startfiles.sh`). Every other file is the same bytes.
  Every binary linked with revision 3 differs from the same binary linked with revision 2: it
  contains the notice.

## Start-up objects

The clang driver puts `-lcrt1.o`, `-ldylib1.o` or `-lbundle1.o` on every link line, and the linker
copies that object into the output: it holds the entry point `start` and the glue that binds lazy
symbols through dyld. The SDK has its own copies in `usr/lib`. So that no object file of the SDK ends
up in a binary, the toolchain brings its own three objects, and `armv6-ios3-clang` passes
`-L<toolchain>/armv6-ios3/lib/startfiles` before the caller's arguments; the linker searches the `-L`
directories before the SDK. The wrapper refuses to link if one of the three files is missing.

`startfiles/build-startfiles.sh OUTDIR` builds them:

- It fetches Apple's Csu at tag `Csu-79`, commit `fd4097d7a8b07792f55d74271d09adedb4748b94`, from
  https://github.com/apple-oss-distributions/Csu. The source is not kept in this repository.
- It checks the sha256 of `start.s`, `crt.c` and `dyld_glue.s`, compiles them with the host clang 18,
  and checks the sha256 of the three objects. It needs no SDK and runs no linker.
- It assembles `startfiles/notice.s` into each object, as the last part of the same assembly unit.
  The Csu sources are used as published.
- The script's header explains the recipe, and why the `ld -r` step of the release's Makefile must not
  be used with this toolchain's linker.

The three source files state their licences in their headers: Apple Public Source License 1.1
(`start.s`, `crt.c`) and 2.0 (`dyld_glue.s`). Both texts are in `licenses/`, and
`THIRD-PARTY-NOTICES.md` §9 has the details.

### The notice inside the objects (since revision 3)

For code that is distributed in object form, both licences ask for "a prominent notice, in the code
itself as well as in related documentation, stating that Source Code of the Covered Code is available
under the terms of this License with information on how and where to obtain such Source Code" (APSL
1.1 §2.2(d), APSL 2.0 §2.3). The documentation part is `THIRD-PARTY-NOTICES.md` §9 and `README.txt`.
The part in the code is `startfiles/notice.s`:

- It defines one NUL-terminated ASCII string in a section of its own, `__TEXT,__csu_notice`, with a
  local symbol, `__csu_source_notice`. It is this project's own file (MIT), not part of Csu, and
  defines no code.
- There are two texts, because the objects differ in what they contain. `crt1.o` says "This program
  contains start-up code built from Apple's open-source Csu release, tag Csu-79: start.s and crt.c
  under the Apple Public Source License version 1.1, dyld_glue.s under the Apple Public Source
  License version 2.0. ..."; `dylib1.o` and `bundle1.o` say "This library contains glue code built
  from Apple's open-source Csu release, tag Csu-79: dyld_glue.s under the Apple Public Source License
  version 2.0. ...". Both end with the address of the source and the commit.
  `THIRD-PARTY-NOTICES.md` §9 quotes them in full.
- The linker copies the section into every executable, dylib and bundle, because the start-up object
  is part of every link. The section is marked `no_dead_strip`, and so is the symbol: a link with
  `-dead_strip` keeps the text although nothing refers to it (without the marks it is removed;
  tested). `strip -x` removes the local symbol and leaves the text.
- In a linked binary: `strings -a FILE | grep Csu-79` prints the text, `armv6-ios3-otool -l FILE`
  lists the section, and the link map lists the symbol.

A binary linked with revision 3 is therefore not the same bytes as one linked with revision 2 or
with the SDK's copies of these objects: it has one more section in `__TEXT`, and the addresses
around it move. Revision 2's objects, which had no notice, gave binaries that were byte-identical
to ones linked with the SDK's copies; in the revision 3 objects everything that comes from the Csu
sources (sections, contents, relocations, symbols) is the same as in revision 2's.

### What `tools/build-arm.sh` checks

- The toolchain is revision 3 or later; it refuses to build with an older one.
- The three installed objects have the sha256 values pinned in `tools/_common.sh`.
- The link map of every binary: no object file of the SDK was linked, and exactly one start-up
  object, the toolchain's, of the kind that fits the file type.
- `tools/startcheck.py` on every binary: the start-up code inside it, and the notice. The section
  `__TEXT,__csu_notice` must hold, byte for byte, the text for a program (in an executable) or the
  text for a library (in a dylib or a bundle). A binary without it fails.

## Licences

- This project's own files, here and in the tarball, are under the MIT licence: see the top-level
  `LICENSE` of the repository.
- `THIRD-PARTY-NOTICES.md` lists every third-party component of the tarball with its version or
  commit, its licence, where its source is and which patch was applied.
- `licenses/` holds the licence texts (`LICENSES/` in the tarball). `licenses/MANIFEST.tsv` records for
  each text where it was taken from, its size and its sha256.

## Packing a tarball

```sh
toolchain/pack.sh --base /path/to/an/unpacked/armv6-ios3-toolchain [--out DIR]
```

- `--base` provides the parts that are built from upstream sources: the ELF host tools and their
  shared libraries, the libc++ headers and the four ARMv6 archives. They are copied unchanged. Any
  revision of the toolchain can serve as the base.
- Everything else is taken from this repository: the files of this directory, `spotify/BUILD.md`, the
  three toolchain patches from `spotify/patches/`, the top-level `LICENSE`. The start-up objects are
  built on the spot by `startfiles/build-startfiles.sh`, and `README.txt` is the text block above.
- Before it writes the tarball, the script checks the staged tree and stops on any of these: a
  start-up object is missing; a licence file named in the manifest is missing or differs; a staged
  file contains one of the strings on the script's deny list; `IOS3_SDK` is set and a staged file has
  the sha256 of a file of the SDK.
- The result is deterministic: sorted names, one fixed mtime, owner 0, fixed modes, `xz` on one
  thread. Two runs give the same sha256. The script prints the size and the sha256.
- The tarball lands in `toolchain-dist/` next to the repo unless `--out` says otherwise.

Installing it: unpack it anywhere, put its `bin/` in `PATH` and set `IOS3_SDK`.

```sh
mkdir -p ~/opt && tar xf armv6-ios3-toolchain-linux-x86_64.tar.xz -C ~/opt
export PATH=~/opt/armv6-ios3-toolchain/bin:$PATH IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk
```
