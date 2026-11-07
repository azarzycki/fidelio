# armv6-ios3 cross-toolchain: build notes

Built on 2026-11-06 in an Ubuntu 24.04.5 x86_64 container. The host compiler is Ubuntu's `clang 18.1.3 (1ubuntu1)`. The target is ARMv6 (ARM1176JZF-S) iPhone OS 3.1.3, using the iPhoneOS3.1.2 SDK.

**Verified:** everything here is checked statically on Linux: Mach-O structure, load commands, symbol resolution against the SDK dylibs, instruction-set scan, and code-signature layout.
**Not verified:** nothing has been run on an iPhone or under an emulator. Whether the binaries start, play audio or browse Bonjour on the device is untested.

---

## 0. Contents of the toolchain tarball (`armv6-ios3-toolchain-linux-x86_64.tar.xz`)

```
armv6-ios3-toolchain/
  bin/armv6-ios3-clang, armv6-ios3-clang++   compiler wrappers (target, sysroot and flags baked in)
  bin/armv6-ios3-ldid                        fake-signer wrapper (SHA-1, CodeDirectory v0x20001)
  bin/armv6-ios3-check                       static iOS 3 compatibility checker (python3)
  bin/armv6-ios3-{ld,ar,ranlib,lipo,otool,nm,strip,libtool,install_name_tool,size,strings,...}
  bin/arm-apple-darwin9-*                    cctools-port 1030.6.3 / ld64-956.6 (the actual binaries)
  bin/ldid                                   ProcursusTeam ldid (patched)
  lib/libdispatch.so, libBlocksRuntime.so, libplist-2.0.so.4   host runtime libraries (found via RUNPATH $ORIGIN/../lib)
  armv6-ios3/lib/libc++.a, libc++abi.a       static LLVM 18.1.3 libc++ / libc++abi for armv6 iOS 3 (C++17/20, incl. <fstream>/<filesystem>)
  armv6-ios3/lib/libios3compat.a             functions iOS 3 lacks (clock_gettime, getentropy, emutls, atomics, ...)
  armv6-ios3/include/c++/v1                  libc++ headers
  armv6-ios3/include/ios3compat.h, ios3_tls.hpp
  share/armv6-ios3/toolchain.cmake           CMake toolchain file
  share/armv6-ios3/patches/*.patch           every source change made to upstream code
  share/armv6-ios3/src/ios3compat/           source of libios3compat
```

The tarball does not include the SDK or a host clang.

**Host requirements** (Ubuntu 24.04 / WSL):
- `apt install clang-18` (the stock Ubuntu `clang`)
- `python3`, only for `armv6-ios3-check`
- libuuid1, libssl3 and libstdc++6, which every Ubuntu install has

ldid links libcrypto statically. libplist is bundled.

### Usage
```sh
tar xf armv6-ios3-toolchain-linux-x86_64.tar.xz -C ~/opt
export IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk
export PATH=~/opt/armv6-ios3-toolchain/bin:$PATH

armv6-ios3-clang   -O2 hello.c -o hello                    # C / Objective-C
armv6-ios3-clang++ -O2 hello.cpp -o hello                  # C++98 + TR1, SDK's libstdc++ 4.2.1 (dynamic)
armv6-ios3-clang++ -std=c++17 -stdlib=libc++ -O2 x.cpp -o x  # C++17, static libc++ 18
armv6-ios3-clang -O2 a.c -o a -framework AudioToolbox -framework CoreFoundation
armv6-ios3-ldid -S hello                                   # fake-sign (always do this last, after strip)
armv6-ios3-check hello                                     # static sanity check
cmake -DCMAKE_TOOLCHAIN_FILE=~/opt/armv6-ios3-toolchain/share/armv6-ios3/toolchain.cmake ...
```

Wrapper environment variables:

| Variable | Meaning |
|---|---|
| `IOS3_SDK` | **Required.** Path to `iPhoneOS3.1.2.sdk`. |
| `IOS3_CXX_RUNTIME=libc++` | Same as passing `-stdlib=libc++` on every invocation. `none` adds no C++ runtime flags at all (used to bootstrap libc++ itself). |
| `IOS3_COMPAT_INCLUDE=1` | Force-includes `ios3compat.h` (declarations for `clock_gettime`, `strnlen`, `getline`, …). |
| `IOS3_HOST_CLANG`, `IOS3_HOST_CLANGXX` | Choose a different host clang. |
| `IOS3_MIN_VERSION` | Leave it at 3.0 (see §3d). |

---

## 1. SDK

The `iPhoneOS3.1.2.sdk.tar.xz` you shared on Google Drive was fetched with the Drive "download anyway" confirm URL:
```sh
curl -sSL -o page.html "https://drive.usercontent.google.com/download?id=1YQOttK5BEXLbSayM-lJLN152TK-PaEGX&export=download"
# extract the uuid from the confirm form, then:
curl -sSL -o iPhoneOS3.1.2.sdk.tar.xz "https://drive.usercontent.google.com/download?id=1YQOttK5BEXLbSayM-lJLN152TK-PaEGX&export=download&confirm=t&uuid=<uuid>"
stat -c %s  iPhoneOS3.1.2.sdk.tar.xz   # 97995312
sha256sum   iPhoneOS3.1.2.sdk.tar.xz   # d5122c3bd2ddd217de918ff3635c590261424dc7f3a5e63d4cbd8417bec2d5e2  (matches)
tar -xJf iPhoneOS3.1.2.sdk.tar.xz      # 557 MB, 75 symlinks, fat armv5/armv6/armv7 dylibs
```
As instructed, the archive.org DMG download (`iphone_sdk_3.1.3_with_xcode_3.2.1__snow_leopard__10m2003a.dmg`, 3,026,214,216 bytes, sha256 `9b2d24d0…` of the first 2.87 GB only; the transfer was cut once and resumed with `curl -C -`) was started at the beginning and kept running in parallel. Cross-check after the build was finished:
```sh
7z x xcode.dmg "iPhone SDK/Packages/iPhoneSDK3_1_2.pkg"     # 160,839,223 bytes
7z x iPhoneSDK3_1_2.pkg                                     # -> Payload~ (577,215,488 bytes)
bsdtar -xf Payload~                                         # -> Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS3.1.2.sdk
```
The two trees were compared entry by entry (2303 files and symlinks each; sha256 of every regular file, target of every symlink): **0 missing, 0 extra, 0 differing**. The Drive archive is exactly the SDK from the official DMG. The DMG and the other packages were deleted afterwards.

`apt-get install p7zip-full libarchive-tools libssl-dev uuid-dev libxml2-dev libplist-dev patchelf` (build dependencies).

## 2. Linker and signing tools

| Component | Repository | Commit | Notes |
|---|---|---|---|
| cctools-port | github.com/tpoechtrager/cctools-port | `904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb` (default branch `1030.6.3-ld64-956.6`, 2026-04-03) | +1 small patch |
| apple-libdispatch | github.com/tpoechtrager/apple-libdispatch | `323b9b4e0ca05d6c56a0c2f2d7d8d47363e612b7` | needed by current ld64 |
| ldid | github.com/ProcursusTeam/ldid | `af86971ae72ec3ed3d0a699107c4e882324c941b` (v2.1.5-procursus7-23) | +1 small patch |
| apple-libtapi | not built | | Only needed for `.tbd` stub SDKs; the 3.1.2 SDK ships real Mach-O dylibs. |

**ld64 version choice.** The current branch still supports armv6, and with `-iphoneos_version_min 3.0` it produces classic, uncompressed dyld info. In ld64, compressed info starts at "version2009", which is iOS 3.1. So no older branch was needed. Old branches such as 134.9 or 253.9 would also need an old host compiler to build.

```sh
# libdispatch (host dependency of ld64)
cd apple-libdispatch && mkdir build && cd build
CC=clang CXX=clang++ cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$TC -DCMAKE_INSTALL_RPATH='$ORIGIN'
ninja && ninja install

# cctools-port / ld64
cd cctools-port && patch -p1 < cctools-port-0001-no_version_load_command.patch
cd cctools
./configure --prefix=$TC --target=arm-apple-darwin9 --enable-tapi-support=no \
  --with-libdispatch=$TC --with-libblocksruntime=$TC --with-llvm-config=no CC=clang CXX=clang++
make -j4 && make install          # LTO support disabled (no libLTO), not needed

# ldid (libcrypto static, libplist shared and bundled)
cd ldid && patch -p1 < ldid-0001-LDID_CD_VERSION.patch
make CXX=clang++ CC=clang LIBPLIST_LIBS="-lplist-2.0" \
     LIBCRYPTO_LIBS="/usr/lib/x86_64-linux-gnu/libcrypto.a -ldl -lpthread"
cp ldid $TC/bin/; cp -L /usr/lib/x86_64-linux-gnu/libplist-2.0.so.4 $TC/lib/

# relocatability: all host binaries look for their .so files next to themselves
for f in $TC/bin/arm-apple-darwin9-* $TC/bin/ldid; do patchelf --set-rpath '$ORIGIN/../lib' $f; done
strip $TC/bin/arm-apple-darwin9-* $TC/bin/ldid
```

### Patches
- **`cctools-port-0001-no_version_load_command.patch`** adds `-no_version_load_command`. Modern ld64 always writes `LC_VERSION_MIN_IPHONEOS` and has no switch to turn it off. iOS 3's dyld would probably ignore it, since it lacks the LC_REQ_DYLD bit. A 2009 ld64 did not emit it, though, so it is suppressed to match.
- **`ldid-0001-LDID_CD_VERSION.patch`** lets the environment variable `LDID_CD_VERSION` set the CodeDirectory version. `armv6-ios3-ldid` uses `-Hsha1` with `LDID_CD_VERSION=0x20001`. The result is one SHA-1 CodeDirectory, 4 KiB pages and version 0x20001, which is what a 2009-era signer produced. Stock Procursus ldid would add a SHA-256 CodeDirectory plus version 0x20400 fields. The iOS 3 kernel only hashes with SHA-1, and it is unknown whether it accepts the newer version. Without this change a fake-signed binary could be killed on page-in even on a jailbroken device. **Still unverified on hardware.**

## 3. Proof

The test sources are in `hello-armv6-ios3/src/`. Every binary is stripped with `strip -x`, then signed with `armv6-ios3-ldid -S`. `check-report.txt` and `otool-report.txt` sit next to the binaries.

### 3a. C (crt1 + libSystem)
`armv6-ios3-clang -O2 hello.c -o hello_c`
- The underlying link line is `ld -dynamic -arch armv6 -iphoneos_version_min 3.0.0 -syslibroot $SDK -lcrt1.o hello.o -lSystem -lgcc_s.1`. Clang picks `crt1.o` (not `crt1.3.1.o`) and `libgcc_s.1` because the target is below 3.1. This is the same as Xcode 3.1.
- `otool -hv`: `MH_MAGIC ARM V6 EXECUTE ... NOUNDEFS DYLDLINK TWOLEVEL`. MH_PIE is not set, which is correct because PIE arrived in iOS 4.3.
- Load commands: `LC_SEGMENT×4 (__PAGEZERO, __TEXT, __DATA, __LINKEDIT), LC_SYMTAB, LC_DYSYMTAB, LC_LOAD_DYLINKER (/usr/lib/dyld), LC_UUID, LC_UNIXTHREAD (pc=start), LC_ENCRYPTION_INFO (cryptid 0), LC_LOAD_DYLIB libSystem.B.dylib, LC_LOAD_DYLIB libgcc_s.1.dylib, LC_CODE_SIGNATURE`.
- Dyld binding is classic: a `__DATA,__dyld` section from crt1, `dyld_stub_binding_helper`, `__TEXT,__symbol_stub4` and `__la_symbol_ptr`/`__nl_symbol_ptr` with an indirect symbol table. There is no `LC_DYLD_INFO`, no `dyld_stub_binder`, no LC_MAIN and no chained fixups.

### 3b. Frameworks
`hello_audio.c` plays one second of 440 Hz through an AudioQueue on the CFRunLoop. It then calls `DNSServiceBrowse` for `_raop._tcp` and `_spotify-connect._tcp` and pumps results with `select()` and `DNSServiceProcessResult`.
`armv6-ios3-clang -O2 hello_audio.c -o hello_audio -framework AudioToolbox -framework CoreFoundation`
- It links against AudioToolbox (current 246.0.0), CoreFoundation (478.52.0), libSystem and libgcc_s. All 20 undefined symbols resolve in those SDK dylibs. dns_sd is part of libSystem on iOS.

`hello_objc.m` uses Foundation, NSAutoreleasePool, a custom class and manual retain/release. It also passes all checks.

### 3c. C++

**SDK libstdc++ 4.2.1 (the default for `armv6-ios3-clang++`).**
- Problem: clang 18 finds `usr/include/c++/4.2.1`, but not the arch directory that holds `bits/c++config.h`, so you get `'bits/c++config.h' file not found`.
- Fix (in the wrapper): `-stdlib=libstdc++ -cxx-isystem $SDK/usr/include/c++/4.2.1/armv6-apple-darwin9`.
- With that, `<bits/stdc++.h>` and `<bits/stdtr1c++.h>` (every header) parse cleanly with clang 18 under `-std=gnu++98`, `-std=c++11` and the default `gnu++17`. Language features follow the `-std` flag. The library itself is C++98 plus TR1 only: no `<thread>`, `<mutex>`, `<atomic>`, `<memory>` smart pointers beyond auto_ptr/tr1::shared_ptr, `<chrono>`, and so on.
- Exceptions are **SjLj** (`__gxx_personality_sj0`, `_Unwind_SjLj_*`), which is the ABI of the SDK's libstdc++ and libgcc_s. Clang selects it automatically for armv6-apple-ios.
- `hello_cpp` (iostream, string, vector, map, sstream, throw/catch) links against `/usr/lib/libstdc++.6.dylib` (compat 7.0.0). All 42 undefined symbols resolve.

**C++17: done. Static libc++ 18.1.3 + libc++abi for armv6 iOS 3 builds and links.**
- Source: llvm-project tag `llvmorg-18.1.3` (`c13b7485b87909fcf739f62cfa382b55407433c0`), the same version as the host clang. Sparse checkout of `runtimes libcxx libcxxabi libunwind cmake llvm/cmake llvm/utils/llvm-lit compiler-rt/lib/builtins`.

```sh
export IOS3_SDK=... PATH=$TC/bin:$PATH IOS3_CXX_RUNTIME=none   # 'none': do not inject an installed libc++ into its own build
cmake -S runtimes -B build-armv6 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC/share/armv6-ios3/toolchain.cmake \
 -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=$TC/armv6-ios3 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
 -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
 -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_STATIC=ON -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
 -DLIBCXX_ENABLE_FILESYSTEM=ON -DLIBCXX_ENABLE_VENDOR_AVAILABILITY_ANNOTATIONS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
 -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_DOCS=OFF -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
 -DLIBCXX_HERMETIC_STATIC_LIBRARY=OFF -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF \
 -DLIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF -DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=OFF
ninja -C build-armv6 cxx cxxabi install-cxx install-cxxabi
```

Only six compile errors came up. `llvm-project-18.1.3-0001-libcxx-libcxxabi-ios3.patch` fixes them:
1. `__config`: `aligned_alloc` is missing before iOS 13. Upstream only guards macOS < 10.15, so I added the matching iOS guard. libc++ then uses `posix_memalign`, which iOS 3 has.
2. `chrono.cpp` and `filesystem_clock.cpp`: libc++ assumes Apple always has `clock_gettime` / `CLOCK_MONOTONIC_RAW` (true from iOS 10). On older SDKs, `system_clock` now uses `gettimeofday` and `steady_clock` uses `mach_absolute_time` scaled by `mach_timebase_info`.
3. `cxa_exception.cpp`: clang's `<unwind.h>` hides `_Unwind_DeleteException` on Darwin/ARM, but iOS 3's libSystem exports it. I added the declaration.
4. `filesystem/operations.cpp`: `remove_all` wants `openat`/`unlinkat`/`fdopendir` (iOS 8+). The `directory_iterator` fallback libc++ keeps for Windows/z/OS is now also selected for iOS < 8.

Note: in libc++ 18, `LIBCXX_ENABLE_FILESYSTEM=OFF` removes **`<fstream>` as well** (its classes are guarded by `_LIBCPP_HAS_NO_FILESYSTEM`). cspot's bell library uses `std::ofstream`, so the shipped libc++ is the filesystem-enabled build (patch 4). `std::filesystem` works, using `stat`/`utimes`-era calls on this target.

Result:
- libc++.a is 1.6 MB and libc++abi.a 0.26 MB.
- Every external symbol they reference, except linker-provided `___dso_handle`, is exported by iOS 3.1.2's libSystem or libgcc_s.
- There are no TLV sections.
- Exceptions use the device's own SjLj unwinder in libSystem and libgcc_s, so no libunwind is needed.

The libc++ runtime is linked statically, as the brief required. The wrapper adds `-nostdinc++ -isystem .../c++/v1` and `-nostdlib++ libc++.a libc++abi.a`. Do not mix libc++ and the SDK libstdc++ in one binary.

`hello_cxx17` exercises std::thread×4, mutex, condition_variable, 32/64-bit `std::atomic`, a thread_local replacement, exceptions, iostream/sstream, variant/optional/string_view, structured bindings, shared_ptr, function, mt19937 + random_device, and steady/system clocks. It is 660 KB stripped, depends only on libSystem and libgcc_s, and all 130 undefined symbols resolve.

**Limits of libc++ mode:**
- No time-zone database.
- `std::random_device` uses `arc4random()`.
- `thread_local`: see the next point.

**`thread_local` / `__thread` do not work.** Clang 18 rejects them at the front end for 32-bit iOS before 9.0 (`thread-local storage is not supported for the current target`), even with `-femulated-tls`. iOS 3's dyld has no TLV support either. Workaround: `armv6-ios3/include/ios3_tls.hpp` provides `ios3::thread_local_var<T>`, built on pthread keys. Code that uses `thread_local` needs a small source patch. `libios3compat.a` also contains compiler-rt's `__emutls_get_address`, for objects built by other compilers with emulated TLS.

### 3d. Run-time sanity (static)

`armv6-ios3-check` (python3, uses `nm` from the toolchain) checks the following and exits non-zero on any error:
- Armv6 slice, file type, and that MH_PIE is not set.
- Every load command is in iOS 3's vocabulary. `LC_DYLD_INFO[_ONLY]`, `LC_MAIN`, `LC_DYLD_CHAINED_FIXUPS`, `LC_DYLD_EXPORTS_TRIE`, `LC_BUILD_VERSION` and `LC_RPATH` are errors. `LC_FUNCTION_STARTS`, `LC_DATA_IN_CODE`, `LC_VERSION_MIN_*` and `LC_SOURCE_VERSION` are warnings.
- `LC_UNIXTHREAD` and `LC_LOAD_DYLINKER` are present.
- No thread-local sections, and `cryptid` is 0.
- Every `LC_LOAD_DYLIB` exists in the SDK with a high enough current version.
- Every undefined symbol is exported by an SDK dylib the binary links to.

All five hello binaries pass. Negative tests:
- A binary referencing `clock_gettime` via `-Wl,-U` fails.
- A plain clang link without the wrapper flags gives warnings for LC_VERSION_MIN/LC_FUNCTION_STARTS/LC_DATA_IN_CODE.
- `IOS3_MIN_VERSION=3.1` produces `LC_DYLD_INFO_ONLY` and fails. **Keep the deployment target at 3.0.**

Linker flags the wrapper adds to match a 2009 toolchain, and why:

| Flag | Why |
|---|---|
| `-iphoneos_version_min 3.0` (from `-target armv6-apple-ios3.0`) | Gives classic `LC_DYSYMTAB` indirect symbols, lazy pointers and `dyld_stub_binding_helper`. No compressed dyld info, no LC_MAIN (LC_UNIXTHREAD instead), no PIE, no chained fixups. |
| `-no_function_starts`, `-no_data_in_code_info` | Modern ld64 adds these by default. |
| `-no_version_load_command` | New option from the patch in §2. |

**Not changed:** segment alignment is 16 KB (`__PAGEZERO` 0x4000, `__TEXT` at 0x4000), whereas Xcode 3.1 used 4 KB. ld64 forces 16 KB for any encryptable iOS binary, and `-segalign 0x1000` is overridden. 16 KB is a multiple of the iPhone's 4 KB page, so the kernel maps it fine; it only adds roughly 12–36 KB of padding per binary. The code signature still hashes 4 KB pages.

**ISA check:** the code was disassembled and grepped for movw/movt, Thumb-2, NEON, VFPv3, dmb/dsb, udiv and similar, and none occur.
- Clang turns `armv6-apple-ios` into `armv6k` with `-target-cpu mpcore`, so atomics use `ldrex{b,h,d}`/`strex*` and `clrex`.
- These are ARMv6K instructions, and the iPhone 2G's ARM1176JZF-S implements ARMv6K(Z).
- Memory barriers are `mcr p15, 0, r0, c7, c10, 5`, the ARMv6 form.

### Float ABI
The wrapper uses `-mfloat-abi=softfp -mfpu=vfpv2 -marm`, which is also what clang chooses by default for this target:
- VFP instructions are used, because the ARM1176JZF-S has VFPv2.
- Float and double arguments and return values travel in core registers (the APCS soft-float calling convention). That is the calling convention of Apple's armv6 dylibs, and the SDK's libgcc_s exports both `__addsf3` and `__addsf3vfp`-style helpers.

`-mfloat-abi=soft` (your original command) is ABI-compatible but routes every float operation through libgcc_s calls, which is slow for audio DSP. `hard` would be incompatible with the SDK libraries. Code is ARM mode, not Thumb: Thumb-1 on ARMv6 cannot execute VFP instructions.

### Workarounds in the wrappers, summarised
| Problem | Workaround |
|---|---|
| Linux lld cannot write 32-bit ARM Mach-O executables | `--ld-path=arm-apple-darwin9-ld` (ld64-956.6) |
| `bits/c++config.h` not found | `-cxx-isystem $SDK/usr/include/c++/4.2.1/armv6-apple-darwin9` |
| **clang 18 segfault** in the ARM AsmPrinter at `-O0`: FastISel + SjLj EH on functions with several landing pads (reproduced with a 15-line main) | `-mllvm -fast-isel=0` is always passed. FastISel only runs at -O0, so optimised builds are unchanged. (Link-only invocations print a harmless "argument unused" warning for it.) |
| New load commands | `-Wl,-no_function_starts,-no_data_in_code_info,-no_version_load_command` |
| `thread_local` rejected | `ios3_tls.hpp` (pthread keys) |
| Missing libc functions | `libios3compat.a` is always linked last (archive members are only pulled when needed); `ios3compat.h` declares them |

### libios3compat.a
These functions are exported by **neither** iOS 3.1.2's libSystem **nor** its libgcc_s, which was checked against `nm` of the SDK.

**Written for this toolchain** (`ios3compat.c`):
- `clock_gettime`/`clock_getres`: `CLOCK_REALTIME` via `gettimeofday`; the monotonic and uptime clocks via `mach_absolute_time`; the CPU-time clocks via `getrusage`/`thread_info`.
- `getentropy`, `arc4random_buf`, `arc4random_uniform` (from `/dev/urandom`, falling back to `arc4random`).
- `strnlen`, `strndup`, `memmem`, `getdelim`, `getline`, `dprintf`, `vdprintf`.

**From compiler-rt 18.1.3, unmodified:**
- `emutls.c`, which provides `__emutls_get_address`.
- `atomic.c`, which provides generic `__atomic_*` with an OSSpinLock fallback.
- The integer and half-precision builtins iOS 3 lacks: `__divmoddi4 __divmodsi4 __udivmodsi4 __mulodi4 __mulosi4 __ffssi2 __extendhfsf2 __truncsfhf2 __truncdfhf2 __truncsfbf2 __truncdfbf2 __fe_getround`.

Builtins already present in libgcc_s, such as `__divdi3` and `__udivsi3`, were deliberately left out so the system copies win.

## 4. Files delivered for steps 1–3
- `armv6-ios3-toolchain-linux-x86_64.tar.xz`: the toolchain.
- `hello-armv6-ios3.tar.gz`: `hello_c`, `hello_audio`, `hello_cpp`, `hello_cxx17` and `hello_objc`, all fake-signed, plus sources and the check/otool reports.
- `patches/`: the three patches above.
- This file.

To deploy: `scp hello_* root@phone:/var/mobile/` then `chmod +x`. If a binary dies with `Killed: 9`, the signature is being rejected; try re-signing with stock `ldid -S` (SHA-1 + SHA-256, v0x20400) and tell me which one works.

## 5. Not verified, and why
- Nothing has run on hardware or under an emulator. QEMU has no iPhone OS 3 user-mode or system target that can run these binaries.
- Whether the iOS 3.1.3 kernel and your jailbreak accept the ldid signature format: the format was chosen to match the era, but this is untested.
- 16 KB segment alignment on the iOS 3 loader: it should be fine as argued in §3d, but it is untested.
- Behaviour of `hello_audio` (actual sound output, Bonjour results).
