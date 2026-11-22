# Third-party notices: armv6-ios3 cross toolchain (Linux x86_64 host)

For `armv6-ios3-toolchain-linux-x86_64.tar.xz`, toolchain revision 2 (the file `VERSION`). Written
2026-11-21. This file states facts: what each part of the tarball is, which licence its upstream
states for it, and where its source is. It is not legal advice and has not had a legal review.

This toolchain contains software from the projects listed below. Each entry gives the version or
commit that was built, the licence, the file in `LICENSES/` that holds the licence text, where the
source is, and the patch that was applied, if any. The patches are in `share/armv6-ios3/patches/`
of this tarball and in `spotify/patches/` of the project repository. Build commands are in
`share/armv6-ios3/BUILD.md`.

Paths are those of the tarball. In the project repository this file is
`toolchain/THIRD-PARTY-NOTICES.md` and the directory `LICENSES/` is `toolchain/licenses/`.
`LICENSES/MANIFEST.tsv` gives the origin, the size and the sha256 of every licence text.

The Apple iPhoneOS SDK and the host clang are not part of this tarball.

## 1. cctools-port (Apple cctools 1030.6.3 and ld64 956.6, ported to Linux)

- Files: `bin/arm-apple-darwin9-*` (31 programs) and the `bin/armv6-ios3-*` symlinks to them.
- Version: commit `904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb`.
- Licence: Apple Public Source License 2.0 (APSL-2.0) for most files. Other files carry other terms:
  - `otool` contains `otool/arm_disasm.c` from GNU libopcodes, GPL-2.0-or-later, with `include/opcode/arm.h`, GPL-1.0-or-later. Its other sources carry Apple's three-clause BSD licence and an Intel permission notice.
  - `ar`, `nm`, `size`, `strings` and `libstuff/crc32.c` carry the four-clause BSD licence of the University of California.
  - `mtoc`, `makerelocs` and `otool` are compiled with the header files `include/coff/*.h`, GPL-2.0-or-later.
  - `ld` is built from sources that include LLVM-derived headers (University of Illinois/NCSA licence; one file Apache-2.0 WITH LLVM-exception), an MD5 implementation (Aladdin Enterprises notice), public-domain SHA-1 and SHA-256, and two BSD-licensed files.
- Licence texts: `LICENSES/cctools-port.APSL-2.0.txt`, `LICENSES/GPL-2.0.txt`, `LICENSES/GPL-1.0.txt`, `LICENSES/llvm-project.LICENSE.txt`; every other per-file notice is reproduced in `LICENSES/cctools-port.file-notices.txt`. Two more licences appear only on header files in the source tree (APSL 1.1 on two files, CDDL 1.0 on six); whether those headers are compiled into any program was not established. Their texts are in `LICENSES/APSL-1.1.txt` and `LICENSES/CDDL-1.0.txt`.
- Source: https://github.com/tpoechtrager/cctools-port at the commit above.
- Patch: `cctools-port-0001-no_version_load_command.patch` (changes `cctools/ld64/src/ld/Options.cpp`, an APSL-2.0 file).

## 2. apple-libdispatch (libdispatch and BlocksRuntime)

- Files: `lib/libdispatch.so`, `lib/libBlocksRuntime.so`.
- Version: commit `323b9b4e0ca05d6c56a0c2f2d7d8d47363e612b7`.
- Licence: Apache-2.0. The upstream `LICENSE` file ends with a "Runtime Library Exception" paragraph.
- Licence text: `LICENSES/apple-libdispatch.LICENSE.txt`
- Source: https://github.com/tpoechtrager/apple-libdispatch at the commit above.
- Patch: none.

## 3. ldid (ProcursusTeam fork)

- File: `bin/ldid` (run through the wrapper `bin/armv6-ios3-ldid`).
- Version: commit `af86971ae72ec3ed3d0a699107c4e882324c941b`; the program reports `v2.1.5-procursus7-23-gaf86971`.
- Licence: GNU Affero General Public License, version 3 (`ldid.cpp`, `ldid.hpp`). `machine.h` is APSL-2.0.
- Licence texts: `LICENSES/ldid.COPYING.AGPL-3.0.txt`, `LICENSES/cctools-port.APSL-2.0.txt`
- Source: https://github.com/ProcursusTeam/ldid at the commit above.
- Patch: `ldid-0001-LDID_CD_VERSION.patch` (changes `ldid.cpp`).

## 4. OpenSSL (libcrypto, statically linked into `bin/ldid`)

- Version: 3.0.13, from Ubuntu 24.04's `libssl-dev` package.
- Licence: Apache-2.0.
- Licence text: `LICENSES/openssl.LICENSE.txt`
- Source: https://github.com/openssl/openssl tag `openssl-3.0.13`; the Ubuntu source package `openssl` 3.0.13-0ubuntu3.x at http://archive.ubuntu.com/ubuntu/pool/main/o/openssl/.
- Patch: none from this project.

## 5. libplist

- File: `lib/libplist-2.0.so.4`, loaded by `bin/ldid`. It is an unmodified copy of the library in Ubuntu 24.04's package `libplist-2.0-4` version 2.3.0-1~exp2build2.
- Licence: LGPL-2.1-or-later. Two source files (`src/jsmn.c`, `src/time64.c`) are MIT.
- Licence texts: `LICENSES/libplist.COPYING.LESSER.LGPL-2.1.txt`, `LICENSES/libplist.embedded-MIT-notices.txt`, `LICENSES/libplist.ubuntu-package-copyright.txt`. Upstream also ships `LICENSES/libplist.COPYING.GPL-2.0.txt`.
- Source: Ubuntu source package `libplist` 2.3.0-1~exp2build2 at http://archive.ubuntu.com/ubuntu/pool/main/libp/libplist/; upstream https://github.com/libimobiledevice/libplist tag `2.3.0`.
- Patch: none from this project.

## 6. LLVM libc++ and libc++abi

- Files: `armv6-ios3/lib/libc++.a`, `libc++abi.a`, `libc++experimental.a`; `armv6-ios3/include/c++/v1/` (1,017 header files).
- Version: LLVM 18.1.3, tag `llvmorg-18.1.3`, commit `c13b7485b87909fcf739f62cfa382b55407433c0`.
- Licence: Apache-2.0 WITH LLVM-exception. The licence files also carry the legacy LLVM terms (University of Illinois/NCSA or MIT). `libc++.a` includes the Ryu floating-point code (Boost Software License 1.0). Three headers under `__format/` carry a Unicode data licence notice.
- Licence texts: `LICENSES/llvm-libcxx.LICENSE.txt`, `LICENSES/llvm-libcxx.CREDITS.txt`, `LICENSES/llvm-libcxxabi.LICENSE.txt`, `LICENSES/llvm-libcxxabi.CREDITS.txt`, `LICENSES/llvm-runtimes.embedded-third-party-notices.txt`
- Source: https://github.com/llvm/llvm-project at the tag above.
- Patch: `llvm-project-18.1.3-0001-libcxx-libcxxabi-ios3.patch` (changes `libcxx/include/__config`, `libcxx/src/chrono.cpp`, `libcxx/src/filesystem/filesystem_clock.cpp`, `libcxx/src/filesystem/operations.cpp`, `libcxxabi/src/cxa_exception.cpp`). The shipped header `__config` is the patched one; the other 1,013 upstream headers are unchanged, and three headers are generated by the libc++ build.

## 7. LLVM compiler-rt builtins

- Files: 15 of the 16 objects in `armv6-ios3/lib/libios3compat.a` (`atomic`, `divmoddi4`, `divmodsi4`, `emutls`, `extendhfsf2`, `ffssi2`, `fp_mode`, `int_util`, `mulodi4`, `mulosi4`, `truncdfbf2`, `truncdfhf2`, `truncsfbf2`, `truncsfhf2`, `udivmodsi4`).
- Version: LLVM 18.1.3, same tag and commit as above.
- Licence: Apache-2.0 WITH LLVM-exception; legacy terms University of Illinois/NCSA or MIT.
- Licence texts: `LICENSES/llvm-compiler-rt.LICENSE.txt`, `LICENSES/llvm-compiler-rt.CREDITS.txt`
- Source: https://github.com/llvm/llvm-project at the tag above, directory `compiler-rt/lib/builtins`.
- Patch: none.

## 8. GNU C Library and GCC start-up code

- Files: every x86-64 ELF file in `bin/` and `lib/` that this build linked contains the start-up objects the host linker adds (`Scrt1.o`, `crti.o`, `crtn.o` from glibc; `crtbeginS.o`, `crtendS.o` from GCC). The C++ programs are compiled against GCC's libstdc++ headers.
- Version: glibc 2.39 and GCC 13.3.0 as packaged in Ubuntu 24.04.
- Licence: glibc start files, LGPL-2.1-or-later with an added permission to link them into other programs. GCC files, GPL-3.0-or-later WITH GCC-exception-3.1.
- Licence texts: `LICENSES/glibc.COPYING.LIB.LGPL-2.1.txt`, `LICENSES/gcc.COPYING3.GPL-3.0.txt`, `LICENSES/gcc.COPYING.RUNTIME.txt`, `LICENSES/host-startfiles.glibc-gcc-notices.txt`
- Source: https://sourceware.org/git/glibc.git tag `glibc-2.39`; https://gcc.gnu.org/git/gcc.git tag `releases/gcc-13.3.0`; the Ubuntu source packages `glibc` and `gcc-13`.
- Patch: none from this project.

## 9. Apple Csu (start-up objects of the ARMv6 target)

- Files: `armv6-ios3/lib/startfiles/crt1.o`, `dylib1.o` and `bundle1.o`. The linker puts one of them first into every executable (`crt1.o`), dylib (`dylib1.o`) or bundle (`bundle1.o`) built with this toolchain, so their code becomes part of every such binary.
- Version: Apple's open-source release Csu-79: tag `Csu-79`, commit `fd4097d7a8b07792f55d74271d09adedb4748b94`.
- Source files used, and the licence each one states in its header:
  - `start.s`: Apple Public Source License, version 1.1.
  - `crt.c`: Apple Public Source License, version 1.1.
  - `dyld_glue.s`: Apple Public Source License, version 2.0.

  `crt1.o` is built from all three; `dylib1.o` and `bundle1.o` are built from `dyld_glue.s` alone. The release has no licence file of its own.
- Licence texts: `LICENSES/APSL-1.1.txt` (version 1.1) and `LICENSES/cctools-port.APSL-2.0.txt` (version 2.0). Copies of both are next to the build script, as `share/armv6-ios3/src/startfiles/APSL-1.1.txt` and `APSL-2.0.txt`.
- Source: the source code of these three files is available, under the licences named above, at https://github.com/apple-oss-distributions/Csu (tag `Csu-79`, the commit above). The tarball holds the three object files and not the source files; the build script fetches them from that address.
- How they are built: `share/armv6-ios3/src/startfiles/build-startfiles.sh` (in the repository: `toolchain/startfiles/build-startfiles.sh`) fetches that commit, checks the sha256 of the three source files, compiles them with the host clang and checks the sha256 of the three objects. The source files are used as published. The build differs from the release's own Makefile in three points, which the script's header explains: the objects are not merged with `ld -r` (`crt1.o` is assembled as one unit instead), `crt.c` is compiled with `-fno-zero-initialized-in-bss`, and the deployment target is 3.0.
- Patch: none.
- The three object files hold code, data and symbol names only; they contain no notice text.

## 10. This project's own files

- Files: `bin/armv6-ios3-clang`, `bin/armv6-ios3-ldid`, `bin/armv6-ios3-check`, `share/armv6-ios3/toolchain.cmake`, `share/armv6-ios3/src/ios3compat/ios3compat.c` and `ios3compat.h`, `armv6-ios3/include/ios3compat.h`, `armv6-ios3/include/ios3_tls.hpp`, the object `ios3compat.o` in `libios3compat.a`, `share/armv6-ios3/src/startfiles/build-startfiles.sh`, `share/armv6-ios3/BUILD.md`, `README.txt`, `VERSION`, and this file.
- Licence: MIT. The text is in `LICENSE`, a copy of the `LICENSE` file at the root of the project repository, where these files are kept under `toolchain/` (`BUILD.md` under `spotify/`).
- The three files in `share/armv6-ios3/patches/` are changes to third-party sources and fall under the licence of the files they change.
